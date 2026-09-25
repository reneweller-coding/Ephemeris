/**
 * @file Engine.cpp
 * @brief The engine of Phase 1: rows, voices, gestures, mixer, tape echo.
 */
#include "eph/Engine.h"
#include "eph/Dsp.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
constexpr int kCell = 32;   ///< the parameter raster in samples
}

void Engine::prepare(double sampleRate, int maxBlock)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    maxBlock_ = std::max(1, maxBlock);
    sample_ = 0;
}

void Engine::load(const Score& score)
{
    score_ = score;
    score_.sort();
    sample_ = 0;
    evCursor_ = 0;

    // Notes onto the sample grid. A note whose predecessor on the same source slides into it is legato:
    // the voice glides instead of retriggering.
    events_.clear();
    int id = 0;
    bool slideInto[kSources] = {};
    for (const NoteEvent& n : score_.notes) {
        int src = static_cast<int>(n.part) - static_cast<int>(Part::Row1);
        switch (n.part) {
        case Part::Lead:     src = kSrcLead; break;
        case Part::Drone:    src = kSrcDrone; break;
        case Part::TapeKeys: src = kSrcTape; break;
        case Part::Strings:  src = kSrcStrings; break;
        case Part::Drums:    src = kSrcDrums; break;
        default: if (src < 0 || src >= kRows) continue;
        }
        const int64_t on = std::llround(score_.tempo.secondsAt(n.beat) * sampleRate_);
        const int64_t off = std::max(on + 1, static_cast<int64_t>(std::llround(score_.tempo.secondsAt(n.beat + n.length) * sampleRate_)));
        ++id;
        events_.push_back({ on, 1, static_cast<uint8_t>(src), n.accent, slideInto[src], n.pitch, n.velocity, id });
        events_.push_back({ off, 0, static_cast<uint8_t>(src), false, false, n.pitch, 0.0f, id });
        slideInto[src] = n.slide;
    }
    std::stable_sort(events_.begin(), events_.end(), [](const Ev& a, const Ev& b) {
        return a.sample != b.sample ? a.sample < b.sample : a.on < b.on;
    });

    // Gestures grouped by the knob they move.
    tracks_.clear();
    trackOf_.assign(static_cast<size_t>(params_.count()), -1);
    for (const Gesture& g : score_.gestures) {
        if (g.param < 0 || g.param >= params_.count()) continue;
        int& t = trackOf_[static_cast<size_t>(g.param)];
        if (t < 0) { t = static_cast<int>(tracks_.size()); tracks_.push_back({ g.param, {}, 0, 0.0f }); }
        tracks_[static_cast<size_t>(t)].gestures.push_back(g);
    }
    for (Track& t : tracks_) t.cursor = t.gestures.size();

    // Every source and room from its own seed branch.
    uint64_t voiceSeeds[kModVoices];
    for (int r = 0; r < kModVoices; ++r) voiceSeeds[r] = mixSeed(score_.seed, 100 + static_cast<uint64_t>(r));
    static_assert(kModVoices == kBankVoices, "the voice bank holds the rows, the lead and the drone");
    voices_.prepare(sampleRate_, voiceSeeds);
    echo_.prepare(sampleRate_, 2.5, mixSeed(score_.seed, 200));
    reverb_.prepare(sampleRate_);
    tape_.prepare(sampleRate_, mixSeed(score_.seed, 500));
    atmos_.prepare(sampleRate_, mixSeed(score_.seed, 600));
    strings_.prepare(sampleRate_, mixSeed(score_.seed, 700));
    drums_.prepare(sampleRate_, mixSeed(score_.seed, 800));
    spring_.prepare(sampleRate_);
    comp_.prepare(sampleRate_);
    limiter_.prepare(sampleRate_);
    for (Strip& s : strips_) s.running = false;
    updateCell();
}

float Engine::played(int id) const
{
    const float knob = params_.get(id);
    const int t = id >= 0 && id < static_cast<int>(trackOf_.size()) ? trackOf_[static_cast<size_t>(id)] : -1;
    if (t < 0) return knob;
    const float off = tracks_[static_cast<size_t>(t)].offset;
    if (off == 0.0f) return knob;
    const float norm = std::clamp(params_.toNormalised(id, knob) + off, 0.0f, 1.0f);
    return params_.fromNormalised(id, norm);
}

VoiceSettings Engine::voiceSettings(Module m, int instance, bool vibrato) const
{
    // The voice table and the lead's share their first thirteen entries (Params.h), so one reader does.
    auto v = [&](int index) { return played(params_.id(m, instance, index)); };
    VoiceSettings s;
    s.wave = v(voice::Wave);
    s.detuneCents = v(voice::Detune);
    s.pulseWidth = v(voice::PulseWidth);
    s.driftCents = v(voice::Drift);
    s.driveDb = v(voice::Drive);
    s.cutoffHz = v(voice::Cutoff);
    s.resonance = v(voice::Resonance);
    s.envOctaves = v(voice::EnvAmount);
    s.decayMs = v(voice::Decay);
    s.keyTrack = v(voice::KeyTrack);
    s.accent = v(voice::Accent);
    s.releaseMs = v(voice::AmpDecay);
    s.glideMs = v(voice::Glide);
    if (vibrato) {
        s.vibratoCents = v(lead::Vibrato);
        s.vibratoHz = v(lead::VibratoRate);
    }
    return s;
}

void Engine::setStrip(int s, float levelDb, float pan, float echo, float reverb, float width)
{
    Strip& st = strips_[s];
    const float level = dbToGain(levelDb);
    const float a = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    st.gainL = level * std::cos(a) * width;
    st.gainR = level * std::sin(a) * width;
    st.echo = echo;
    st.reverb = reverb;
}

void Engine::updateCell()
{
    // Gesture offsets at this cell's beat.
    const double beat = score_.tempo.beatAt(seconds());
    for (Track& t : tracks_) {
        // Positions only move forward, so the cursor only moves forward: the latest gesture that has
        // started is the one that counts (Score::gestureOffset), gestures.size() while none has.
        const size_t none = t.gestures.size();
        size_t c = t.cursor;
        size_t next = c == none ? 0 : c + 1;
        while (next < none && t.gestures[next].beat <= beat) c = next++;
        t.cursor = c;
        t.offset = c == none ? 0.0f : gestureValue(t.gestures[c], beat);
    }
    auto knob = [&](Module m, int index) { return played(params_.id(m, 0, index)); };

    // The modular voices: the rows (their strips on the row module), the lead and the drone.
    for (int r = 0; r < kModVoices; ++r) {
        const bool row = r < kRows;
        const Module m = row ? Module::Voice : (r == kSrcLead ? Module::Lead : Module::Drone);
        const int inst = row ? r : 0;
        voices_.set(r, voiceSettings(m, inst, !row));
        strips_[r].running = voices_.active(r) || voices_.held(r);
        auto s = [&](int rowIndex, int leadIndex) {
            return row ? played(params_.id(Module::Row, r, rowIndex)) : played(params_.id(m, 0, leadIndex));
        };
        setStrip(r, s(row::Level, lead::Level), s(row::Pan, lead::Pan), s(row::EchoSend, lead::EchoSend), s(row::ReverbSend, lead::ReverbSend));
    }

    // The tape keys, the string machine, the drums, the atmosphere.
    TapeSettings ts;
    ts.set = static_cast<TapeSet>(std::clamp(static_cast<int>(std::lround(knob(Module::Tape, tape::Set))), 0, 2));
    ts.vowel = knob(Module::Tape, tape::Vowel);
    ts.wowCents = knob(Module::Tape, tape::Wow);
    ts.flutterCents = knob(Module::Tape, tape::Flutter);
    ts.sagCents = knob(Module::Tape, tape::Sag);
    ts.toneHz = knob(Module::Tape, tape::Tone);
    ts.age = knob(Module::Tape, tape::Age);
    tape_.set(ts);
    strips_[kSrcTape].running = tape_.active();
    setStrip(kSrcTape, knob(Module::Tape, tape::Level), knob(Module::Tape, tape::Pan), knob(Module::Tape, tape::EchoSend), knob(Module::Tape, tape::ReverbSend));

    StringSettings ss;
    ss.attackS = knob(Module::Strings, strings::Attack);
    ss.releaseS = knob(Module::Strings, strings::Release);
    ss.feet = knob(Module::Strings, strings::Feet);
    ss.toneHz = knob(Module::Strings, strings::Tone);
    ss.ensemble = knob(Module::Strings, strings::Ensemble);
    strings_.set(ss);
    strips_[kSrcStrings].running = strings_.active();
    // The ensemble spreads the machine over both sides; the pan law's 3 dB come back with sqrt 2.
    setStrip(kSrcStrings, knob(Module::Strings, strings::Level), knob(Module::Strings, strings::Pan),
             knob(Module::Strings, strings::EchoSend), knob(Module::Strings, strings::ReverbSend), 1.41421356f);

    DrumSettings ds;
    ds.kickHz = knob(Module::Drums, drums::KickHz);
    ds.decay = knob(Module::Drums, drums::Decay);
    ds.tone = knob(Module::Drums, drums::Tone);
    drums_.set(ds);
    {
        // The kit pans its instruments itself (Drums.h): the strip only sets the level.
        Strip& d = strips_[kSrcDrums];
        d.running = drums_.active();
        d.gainL = d.gainR = dbToGain(knob(Module::Drums, drums::Level));
        d.echo = knob(Module::Drums, drums::EchoSend);
        d.reverb = knob(Module::Drums, drums::ReverbSend);
    }

    auto gain = [](float db) { return db <= -59.9f ? 0.0f : dbToGain(db); };
    AtmosSettings as;
    as.windGain = gain(knob(Module::Atmos, atmos::Wind));
    as.windHz = knob(Module::Atmos, atmos::WindTone);
    as.sweepsPerMinute = knob(Module::Atmos, atmos::Sweeps);
    as.sweepGain = gain(knob(Module::Atmos, atmos::SweepLevel));
    as.bleepsPerMinute = knob(Module::Atmos, atmos::Bleeps);
    as.bleepGain = gain(knob(Module::Atmos, atmos::BleepLevel));
    // The bleeps take their notes from the root the rows are in.
    as.rootPc = pitchClass(score_.keyRoot + score_.rootAt(beat));
    as.scale = params_.getInt(params_.id(Module::Compose, 0, compose::Scale));
    atmos_.set(as);
    {
        // Stereo already: the strip only sets the level; the echo takes the bleeps (renderSpan).
        Strip& at = strips_[kSrcAtmos];
        at.running = atmos_.active();
        at.gainL = at.gainR = dbToGain(knob(Module::Atmos, atmos::Level));
        at.echo = knob(Module::Atmos, atmos::EchoSend);
        at.reverb = knob(Module::Atmos, atmos::ReverbSend);
    }

    // The rooms and the master.
    EchoSettings es;
    const double bpm = score_.tempo.bpmAt(beat);
    es.delaySeconds = echoTimeBeats(static_cast<EchoTime>(static_cast<int>(knob(Module::Echo, echo::Time)))) * 60.0 / bpm;
    es.feedback = knob(Module::Echo, echo::Feedback);
    es.toneHz = knob(Module::Echo, echo::Tone);
    es.wowMs = knob(Module::Echo, echo::Wow);
    es.flutterMs = knob(Module::Echo, echo::Flutter);
    es.driveDb = knob(Module::Echo, echo::Drive);
    es.pingPong = knob(Module::Echo, echo::PingPong) >= 0.5f;
    echo_.set(es);
    echoReturn_ = dbToGain(knob(Module::Echo, echo::Return));
    reverb_.set(knob(Module::Reverb, reverb::Size), knob(Module::Reverb, reverb::Decay), knob(Module::Reverb, reverb::Damping),
                knob(Module::Reverb, reverb::PreDelay) * 0.001f * static_cast<float>(sampleRate_),
                knob(Module::Reverb, reverb::LowCut), knob(Module::Reverb, reverb::HighCut));
    reverbReturn_ = dbToGain(knob(Module::Reverb, reverb::Return));
    spring_.set(knob(Module::Spring, spring::Decay), knob(Module::Spring, spring::Tone));
    springReturn_ = gain(knob(Module::Spring, spring::Return));
    master_ = dbToGain(knob(Module::Master, master::Level));
    comp_.set(-16.0f, 1.0f + 0.5f * knob(Module::Master, master::Compress), 6.0f, 30.0f, 300.0f);
    limiter_.set(knob(Module::Master, master::Ceiling), 150.0f);
}

const char* Engine::channelName(int c)
{
    static_assert(kChannels == kSources, "a meter per source");
    static const char* const names[kChannels] = { "Row 1", "Row 2", "Row 3", "Row 4", "Row 5", "Row 6", "Row 7", "Row 8",
                                                  "Lead", "Drone", "Tape Keys", "Strings", "Drums", "Atmosphere" };
    return c >= 0 && c < kChannels ? names[c] : "";
}

int Engine::takeMeters(float* peak, double* sumSq)
{
    for (int c = 0; c < kChannels; ++c) {
        peak[c] = meterPeak_[c];
        sumSq[c] = meterSum_[c];
        meterPeak_[c] = 0.0f;
        meterSum_[c] = 0.0;
    }
    const int n = meterCount_;
    meterCount_ = 0;
    return n;
}

void Engine::mix(int source, const float* xl, const float* xr, int n, const Buses& b, bool echoSend)
{
    const Strip& strip = strips_[source];
    for (int i = 0; i < n; ++i) {
        const float l = xl[i] * strip.gainL, r = xr[i] * strip.gainR;
        b.L[i] += l;
        b.R[i] += r;
        if (echoSend) { b.echoL[i] += l * strip.echo; b.echoR[i] += r * strip.echo; }
        b.hallL[i] += l * strip.reverb;
        b.hallR[i] += r * strip.reverb;
    }
    if (metering_) {
        // What the strip put into the mix, read again: the sums above are not touched.
        float peak = meterPeak_[source];
        double sum = meterSum_[source];
        for (int i = 0; i < n; ++i) {
            const float l = xl[i] * strip.gainL, r = xr[i] * strip.gainR;
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
            sum += 0.5 * (static_cast<double>(l) * l + static_cast<double>(r) * r);
        }
        meterPeak_[source] = peak;
        meterSum_[source] = sum;
    }
}

void Engine::renderSpan(float* L, float* R, int n)
{
    float bufL[kCell], bufR[kCell], echoL[kCell], echoR[kCell], hallInL[kCell], hallInR[kCell];
    std::fill(L, L + n, 0.0f);
    std::fill(R, R + n, 0.0f);
    std::fill(echoL, echoL + n, 0.0f);
    std::fill(echoR, echoR + n, 0.0f);
    std::fill(hallInL, hallInL + n, 0.0f);
    std::fill(hallInR, hallInR + n, 0.0f);
    const Buses b{ L, R, echoL, echoR, hallInL, hallInR };
    if (metering_) meterCount_ += n;

    // The sources, each through its strip, in a fixed order (the order of the sums is part of the result).
    bool run[kModVoices];
    for (int r = 0; r < kModVoices; ++r) run[r] = strips_[r].running;
    voices_.process(run, n);
    for (int r = 0; r < kModVoices; ++r)
        if (run[r]) mix(r, voices_.output(r), voices_.output(r), n, b);
    if (strips_[kSrcDrums].running) {
        drums_.process(bufL, bufR, n);
        mix(kSrcDrums, bufL, bufR, n, b);
    }
    if (strips_[kSrcStrings].running) {
        strings_.process(bufL, bufR, n);
        mix(kSrcStrings, bufL, bufR, n, b);
    }
    if (strips_[kSrcAtmos].running) {
        // The whole atmosphere goes to the mix and the hall, only its bleeps to the echo.
        float bleepL[kCell] = {}, bleepR[kCell] = {};
        std::fill(bufL, bufL + n, 0.0f);
        std::fill(bufR, bufR + n, 0.0f);
        atmos_.process(bufL, bufR, bleepL, bleepR, n);
        const Strip& at = strips_[kSrcAtmos];
        mix(kSrcAtmos, bufL, bufR, n, b, false);
        for (int i = 0; i < n; ++i) {
            echoL[i] += bleepL[i] * at.gainL * (0.5f + at.echo);
            echoR[i] += bleepR[i] * at.gainR * (0.5f + at.echo);
        }
    }
    if (strips_[kSrcTape].running) {
        tape_.process(bufL, n);
        mix(kSrcTape, bufL, bufL, n, b);
    }

    // The tape echo, its springs, the hall, the master.
    float wetL[kCell] = {}, wetR[kCell] = {}, sprL[kCell] = {}, sprR[kCell] = {}, hallL[kCell], hallR[kCell];
    echo_.process(echoL, echoR, wetL, wetR, n);
    // The springs take the echo's send, as in a tape echo with springs built in; they return beside it.
    spring_.process(echoL, echoR, sprL, sprR, n);
    for (int i = 0; i < n; ++i) {
        wetL[i] += sprL[i] * springReturn_ / std::max(echoReturn_, 1e-6f);
        wetR[i] += sprR[i] * springReturn_ / std::max(echoReturn_, 1e-6f);
    }
    // The echo's repeats go into the hall too, as they do on a desk where the echo returns to a channel.
    for (int i = 0; i < n; ++i) {
        hallInL[i] += wetL[i] * echoReturn_ * 0.5f;
        hallInR[i] += wetR[i] * echoReturn_ * 0.5f;
    }
    reverb_.process(hallInL, hallInR, hallL, hallR, n);
    for (int i = 0; i < n; ++i) {
        L[i] = (L[i] + wetL[i] * echoReturn_ + hallL[i] * reverbReturn_) * master_;
        R[i] = (R[i] + wetR[i] * echoReturn_ + hallR[i] * reverbReturn_) * master_;
    }
    comp_.process(L, R, n);
    limiter_.process(L, R, n);
}

void Engine::seek(double beat)
{
    sample_ = std::llround(score_.tempo.secondsAt(std::max(0.0, beat)) * sampleRate_);
    evCursor_ = static_cast<size_t>(std::lower_bound(events_.begin(), events_.end(), sample_,
        [](const Ev& e, int64_t s) { return e.sample < s; }) - events_.begin());
    voices_.reset();
    for (Strip& s : strips_) s.running = false;
    tape_.reset();
    strings_.silence();
    drums_.prepare(sampleRate_, mixSeed(score_.seed, 800));
    for (Track& t : tracks_) t.cursor = t.gestures.size();
    cellDirty_ = true;
}

bool Engine::process(float* L, float* R, int n)
{
    int done = 0;
    while (done < n) {
        if ((sample_ % kCell) == 0 || cellDirty_) { updateCell(); cellDirty_ = false; }
        // Every event due at this sample, offs before ons.
        while (evCursor_ < events_.size() && events_[evCursor_].sample <= sample_) {
            const Ev& e = events_[evCursor_++];
            switch (e.source) {
            case kSrcDrums:
                if (e.on) {
                    // The toms are tuned to the root of the moment, the high one a fifth above.
                    const float low = static_cast<float>(midiToHz(43 + pitchClass(score_.keyRoot + score_.rootAt(beat()) - 7)));
                    drums_.hit(e.pitch, e.velocity, e.pitch == 48 || e.pitch == 47 || e.pitch == 50 ? low * 1.5f : low);
                }
                break;
            case kSrcStrings:
                if (e.on) strings_.noteOn(e.pitch, e.velocity, e.id); else strings_.noteOff(e.id);
                break;
            case kSrcTape:
                if (e.on) tape_.noteOn(e.pitch, e.velocity, e.id); else tape_.noteOff(e.id);
                break;
            default:
                if (e.on) voices_.noteOn(e.source, e.pitch, e.velocity, e.accent, e.legato, e.id);
                else voices_.noteOff(e.source, e.id);
                break;
            }
            if (e.on) strips_[e.source].running = true;
        }
        int64_t end = std::min<int64_t>(sample_ + (n - done), (sample_ / kCell + 1) * kCell);
        if (evCursor_ < events_.size()) end = std::min(end, events_[evCursor_].sample);
        const int len = static_cast<int>(end - sample_);
        renderSpan(L + done, R + done, len);
        sample_ += len;
        done += len;
    }
    return seconds() < lengthSeconds();
}

} // namespace eph
