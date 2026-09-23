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

    // Notes of the rows onto the sample grid. A note whose predecessor in the same row slides into it
    // is legato: the voice glides instead of retriggering.
    events_.clear();
    int id = 0;
    bool slideInto[kVoices + 3] = {};
    for (const NoteEvent& n : score_.notes) {
        int row = static_cast<int>(n.part) - static_cast<int>(Part::Row1);
        if (n.part == Part::Lead) row = kLeadVoice;
        else if (n.part == Part::Drone) row = kDroneVoice;
        else if (n.part == Part::TapeKeys) row = kTapeVoice;
        else if (n.part == Part::Strings) row = kStringsVoice;
        else if (n.part == Part::Drums) row = kDrumsVoice;
        else if (row < 0 || row >= kRows) continue;
        const int64_t on = std::llround(score_.tempo.secondsAt(n.beat) * sampleRate_);
        const int64_t off = std::max(on + 1, static_cast<int64_t>(std::llround(score_.tempo.secondsAt(n.beat + n.length) * sampleRate_)));
        ++id;
        events_.push_back({ on, 1, static_cast<uint8_t>(row), n.accent, slideInto[row], n.pitch, n.velocity, id });
        events_.push_back({ off, 0, static_cast<uint8_t>(row), false, false, n.pitch, 0.0f, id });
        slideInto[row] = n.slide;
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

    for (int r = 0; r < kVoices; ++r) voices_[r].prepare(sampleRate_, mixSeed(score_.seed, 100 + static_cast<uint64_t>(r)));
    echo_.prepare(sampleRate_, 2.5, mixSeed(score_.seed, 200));
    reverb_.prepare(sampleRate_);
    tape_.prepare(sampleRate_, mixSeed(score_.seed, 500));
    atmos_.prepare(sampleRate_, mixSeed(score_.seed, 600));
    comp_.prepare(sampleRate_);
    spring_.prepare(sampleRate_);
    drums_.prepare(sampleRate_, mixSeed(score_.seed, 800));
    drumsRunning_ = false;
    strings_.prepare(sampleRate_, mixSeed(score_.seed, 700));
    stringsRunning_ = false;
    limiter_.prepare(sampleRate_);
    tapeRunning_ = false;
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

    for (int r = 0; r < kRows; ++r) {
        auto v = [&](int index) { return played(params_.id(Module::Voice, r, index)); };
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
        voices_[r].set(s);
        running_[r] = voices_[r].active() || voices_[r].held();
        auto rp = [&](int index) { return played(params_.id(Module::Row, r, index)); };
        const float level = dbToGain(rp(row::Level));
        const float pan = std::clamp(rp(row::Pan), -1.0f, 1.0f);
        const float a = (pan + 1.0f) * 0.25f * kPi;
        gainL_[r] = level * std::cos(a);
        gainR_[r] = level * std::sin(a);
        send_[r] = rp(row::EchoSend);
        rsend_[r] = rp(row::ReverbSend);
    }
    setLeadLike(Module::Lead, kLeadVoice);
    setLeadLike(Module::Drone, kDroneVoice);
    auto e = [&](int index) { return played(params_.id(Module::Echo, 0, index)); };
    EchoSettings es;
    const double bpm = score_.tempo.bpmAt(beat);
    es.delaySeconds = echoTimeBeats(static_cast<EchoTime>(static_cast<int>(e(echo::Time)))) * 60.0 / bpm;
    es.feedback = e(echo::Feedback);
    es.toneHz = e(echo::Tone);
    es.wowMs = e(echo::Wow);
    es.flutterMs = e(echo::Flutter);
    es.driveDb = e(echo::Drive);
    es.pingPong = e(echo::PingPong) >= 0.5f;
    echo_.set(es);
    echoReturn_ = dbToGain(e(echo::Return));
    {
        auto t = [&](int index) { return played(params_.id(Module::Tape, 0, index)); };
        TapeSettings ts;
        ts.set = static_cast<TapeSet>(std::clamp(static_cast<int>(std::lround(t(tape::Set))), 0, 2));
        ts.vowel = t(tape::Vowel);
        ts.wowCents = t(tape::Wow);
        ts.flutterCents = t(tape::Flutter);
        ts.sagCents = t(tape::Sag);
        ts.toneHz = t(tape::Tone);
        ts.age = t(tape::Age);
        tape_.set(ts);
        tapeRunning_ = tape_.active();
        const float level = dbToGain(t(tape::Level));
        const float a = (std::clamp(t(tape::Pan), -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
        tapeL_ = level * std::cos(a);
        tapeR_ = level * std::sin(a);
        tapeEcho_ = t(tape::EchoSend);
        tapeReverb_ = t(tape::ReverbSend);
    }
    {
        auto d = [&](int index) { return played(params_.id(Module::Drums, 0, index)); };
        DrumSettings ds;
        ds.kickHz = d(drums::KickHz);
        ds.decay = d(drums::Decay);
        ds.tone = d(drums::Tone);
        drums_.set(ds);
        drumsRunning_ = drums_.active();
        drumLevel_ = dbToGain(d(drums::Level));
        drumEcho_ = d(drums::EchoSend);
        drumReverb_ = d(drums::ReverbSend);
    }
    {
        auto t = [&](int index) { return played(params_.id(Module::Strings, 0, index)); };
        StringSettings ss;
        ss.attackS = t(strings::Attack);
        ss.releaseS = t(strings::Release);
        ss.feet = t(strings::Feet);
        ss.toneHz = t(strings::Tone);
        ss.ensemble = t(strings::Ensemble);
        strings_.set(ss);
        stringsRunning_ = strings_.active();
        const float level = dbToGain(t(strings::Level));
        const float a = (std::clamp(t(strings::Pan), -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
        strL_ = level * std::cos(a) * 1.41421356f;
        strR_ = level * std::sin(a) * 1.41421356f;
        strEcho_ = t(strings::EchoSend);
        strReverb_ = t(strings::ReverbSend);
    }
    {
        auto a = [&](int index) { return played(params_.id(Module::Atmos, 0, index)); };
        auto gain = [](float db) { return db <= -59.9f ? 0.0f : dbToGain(db); };
        AtmosSettings as;
        as.windGain = gain(a(atmos::Wind));
        as.windHz = a(atmos::WindTone);
        as.sweepsPerMinute = a(atmos::Sweeps);
        as.sweepGain = gain(a(atmos::SweepLevel));
        as.bleepsPerMinute = a(atmos::Bleeps);
        as.bleepGain = gain(a(atmos::BleepLevel));
        // The bleeps take their notes from the root the rows are in.
        int shift = 0;
        for (const auto& rs : score_.rootShifts) { if (rs.first > beat) break; shift = rs.second; }
        as.rootPc = ((score_.keyRoot + shift) % 12 + 12) % 12;
        as.scale = params_.getInt(params_.id(Module::Compose, 0, compose::Scale));
        atmos_.set(as);
        atmosRunning_ = atmos_.active();
        atmosLevel_ = dbToGain(a(atmos::Level));
        atmosEcho_ = a(atmos::EchoSend);
        atmosReverb_ = a(atmos::ReverbSend);
    }
    auto rv = [&](int index) { return played(params_.id(Module::Reverb, 0, index)); };
    reverb_.set(rv(reverb::Size), rv(reverb::Decay), rv(reverb::Damping),
                rv(reverb::PreDelay) * 0.001f * static_cast<float>(sampleRate_), rv(reverb::LowCut), rv(reverb::HighCut));
    reverbReturn_ = dbToGain(rv(reverb::Return));
    auto sp = [&](int index) { return played(params_.id(Module::Spring, 0, index)); };
    spring_.set(sp(spring::Decay), sp(spring::Tone));
    springReturn_ = sp(spring::Return) <= -59.9f ? 0.0f : dbToGain(sp(spring::Return));
    master_ = dbToGain(played(params_.id(Module::Master, 0, master::Level)));
    const float amount = played(params_.id(Module::Master, 0, master::Compress));
    comp_.set(-16.0f, 1.0f + 0.5f * amount, 6.0f, 30.0f, 300.0f);
    limiter_.set(played(params_.id(Module::Master, 0, master::Ceiling)), 150.0f);
}

void Engine::setLeadLike(Module m, int voiceIndex)
{
    {
        auto l = [&](int index) { return played(params_.id(m, 0, index)); };
        VoiceSettings s;
        s.wave = l(lead::Wave);
        s.detuneCents = l(lead::Detune);
        s.pulseWidth = l(lead::PulseWidth);
        s.driftCents = l(lead::Drift);
        s.driveDb = l(lead::Drive);
        s.cutoffHz = l(lead::Cutoff);
        s.resonance = l(lead::Resonance);
        s.envOctaves = l(lead::EnvAmount);
        s.decayMs = l(lead::Decay);
        s.keyTrack = l(lead::KeyTrack);
        s.accent = l(lead::Accent);
        s.releaseMs = l(lead::AmpDecay);
        s.glideMs = l(lead::Glide);
        s.vibratoCents = l(lead::Vibrato);
        s.vibratoHz = l(lead::VibratoRate);
        ModVoice& v = voices_[voiceIndex];
        v.set(s);
        running_[voiceIndex] = v.active() || v.held();
        const float level = dbToGain(l(lead::Level));
        const float a = (std::clamp(l(lead::Pan), -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
        gainL_[voiceIndex] = level * std::cos(a);
        gainR_[voiceIndex] = level * std::sin(a);
        send_[voiceIndex] = l(lead::EchoSend);
        rsend_[voiceIndex] = l(lead::ReverbSend);
    }
}

void Engine::renderSpan(float* L, float* R, int n)
{
    float buf[kCell], sendL[kCell], sendR[kCell], wetL[kCell], wetR[kCell];
    float revL[kCell], revR[kCell], hallL[kCell], hallR[kCell];
    std::fill(revL, revL + n, 0.0f);
    std::fill(revR, revR + n, 0.0f);
    std::fill(L, L + n, 0.0f);
    std::fill(R, R + n, 0.0f);
    std::fill(sendL, sendL + n, 0.0f);
    std::fill(sendR, sendR + n, 0.0f);
    for (int r = 0; r < kVoices; ++r) {
        ModVoice& v = voices_[r];
        if (!running_[r]) continue;
        v.process(buf, n);
        const float gl = gainL_[r], gr = gainR_[r], s = send_[r], h = rsend_[r];
        for (int i = 0; i < n; ++i) {
            L[i] += buf[i] * gl;
            R[i] += buf[i] * gr;
            sendL[i] += buf[i] * gl * s;
            sendR[i] += buf[i] * gr * s;
            revL[i] += buf[i] * gl * h;
            revR[i] += buf[i] * gr * h;
        }
    }
    if (drumsRunning_) {
        float dL[kCell], dR[kCell];
        drums_.process(dL, dR, n);
        for (int i = 0; i < n; ++i) {
            const float l = dL[i] * drumLevel_, r = dR[i] * drumLevel_;
            L[i] += l; R[i] += r;
            sendL[i] += l * drumEcho_; sendR[i] += r * drumEcho_;
            revL[i] += l * drumReverb_; revR[i] += r * drumReverb_;
        }
    }
    if (stringsRunning_) {
        float sL[kCell], sR[kCell];
        strings_.process(sL, sR, n);
        for (int i = 0; i < n; ++i) {
            const float l = sL[i] * strL_, r = sR[i] * strR_;
            L[i] += l; R[i] += r;
            sendL[i] += l * strEcho_; sendR[i] += r * strEcho_;
            revL[i] += l * strReverb_; revR[i] += r * strReverb_;
        }
    }
    if (atmosRunning_) {
        float aL[kCell] = {}, aR[kCell] = {}, bL[kCell] = {}, bR[kCell] = {};
        atmos_.process(aL, aR, bL, bR, n);
        for (int i = 0; i < n; ++i) {
            L[i] += aL[i] * atmosLevel_;
            R[i] += aR[i] * atmosLevel_;
            // The bleeps go to the echo; the whole atmosphere to the hall.
            sendL[i] += bL[i] * atmosLevel_ * (0.5f + atmosEcho_);
            sendR[i] += bR[i] * atmosLevel_ * (0.5f + atmosEcho_);
            revL[i] += aL[i] * atmosLevel_ * atmosReverb_;
            revR[i] += aR[i] * atmosLevel_ * atmosReverb_;
        }
    }
    if (tapeRunning_) {
        tape_.process(buf, n);
        for (int i = 0; i < n; ++i) {
            L[i] += buf[i] * tapeL_;
            R[i] += buf[i] * tapeR_;
            sendL[i] += buf[i] * tapeL_ * tapeEcho_;
            sendR[i] += buf[i] * tapeR_ * tapeEcho_;
            revL[i] += buf[i] * tapeL_ * tapeReverb_;
            revR[i] += buf[i] * tapeR_ * tapeReverb_;
        }
    }
    std::fill(wetL, wetL + n, 0.0f);
    std::fill(wetR, wetR + n, 0.0f);
    echo_.process(sendL, sendR, wetL, wetR, n);
    // The springs take the echo's send, as in a tape echo with springs built in; they return beside it.
    float sprL[kCell] = {}, sprR[kCell] = {};
    spring_.process(sendL, sendR, sprL, sprR, n);
    for (int i = 0; i < n; ++i) {
        wetL[i] += sprL[i] * springReturn_ / std::max(echoReturn_, 1e-6f);
        wetR[i] += sprR[i] * springReturn_ / std::max(echoReturn_, 1e-6f);
    }
    // The echo's repeats go into the hall too, as they do on a desk where the echo returns to a channel.
    for (int i = 0; i < n; ++i) {
        revL[i] += wetL[i] * echoReturn_ * 0.5f;
        revR[i] += wetR[i] * echoReturn_ * 0.5f;
    }
    reverb_.process(revL, revR, hallL, hallR, n);
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
    for (int r = 0; r < kVoices; ++r) { voices_[r].reset(); running_[r] = false; }
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
            if (e.row == kDrumsVoice) {
                if (e.on) {
                    // The toms are tuned to the root of the moment, the high one a fifth above.
                    int shift = 0;
                    const double now = beat();
                    for (const auto& rs : score_.rootShifts) { if (rs.first > now) break; shift = rs.second; }
                    const float low = static_cast<float>(midiToHz(43 + ((score_.keyRoot + shift - 7) % 12 + 12) % 12));
                    drums_.hit(e.pitch, e.velocity, e.pitch == 48 || e.pitch == 47 || e.pitch == 50 ? low * 1.5f : low);
                    drumsRunning_ = true;
                }
            } else if (e.row == kStringsVoice) {
                if (e.on) { strings_.noteOn(e.pitch, e.velocity, e.id); stringsRunning_ = true; }
                else strings_.noteOff(e.id);
            } else if (e.row == kTapeVoice) {
                if (e.on) { tape_.noteOn(e.pitch, e.velocity, e.id); tapeRunning_ = true; }
                else tape_.noteOff(e.id);
            } else if (e.on) { voices_[e.row].noteOn(e.pitch, e.velocity, e.accent, e.legato, e.id); running_[e.row] = true; }
            else voices_[e.row].noteOff(e.id);
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
