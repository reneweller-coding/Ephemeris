/**
 * @file Engine.cpp
 * @brief The engine of Phase 1: rows, voices, gestures, mixer, tape echo.
 */
#include "eph/Engine.h"
#include "eph/Dsp.h"
#include <algorithm>
#include <limits>
#include <cstring>
#include <bit>
#include <cmath>

namespace eph {

namespace {
/** @brief Whether @p now differs from @p last bit for bit (or @p valid is false); if so, @p last takes it. */
template <class T>
bool changed(T& last, const T& now, bool valid)
{
    if (valid && std::memcmp(&last, &now, sizeof(T)) == 0) return false;
    std::memcpy(&last, &now, sizeof(T));
    return true;
}
} // namespace

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
    cueMarks_ = cueMarksOf(score_);
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
        case Part::Pad:      src = kSrcPoly; break;
        case Part::Drums:    src = kSrcDrums; break;
        default: if (src < 0 || src >= kRows) continue;
        }
        const int64_t on = std::llround(score_.tempo.secondsAt(n.beat) * sampleRate_);
        const int64_t off = std::max(on + 1, static_cast<int64_t>(std::llround(score_.tempo.secondsAt(n.beat + n.length) * sampleRate_)));
        ++id;
        events_.push_back({ on, 1, static_cast<uint8_t>(src), n.accent, slideInto[src], n.pitch, n.velocity, id, n.bright, n.decay });
        events_.push_back({ off, 0, static_cast<uint8_t>(src), false, false, n.pitch, 0.0f, id });
        slideInto[src] = n.slide;
    }
    std::stable_sort(events_.begin(), events_.end(), [](const Ev& a, const Ev& b) {
        return a.sample != b.sample ? a.sample < b.sample : a.on < b.on;
    });

    // Gestures grouped by the knob they move.
    tracks_.clear();
    trackOf_.assign(static_cast<size_t>(params_.count()), -1);
    playedCache_.assign(static_cast<size_t>(params_.count()), { std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f });
    cache_.valid = false;
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
    bbd_.prepare(sampleRate_, 2.5, mixSeed(score_.seed, 210));
    echo2_.prepare(sampleRate_, 2.5, mixSeed(score_.seed, 220));
    {
        const float sr = static_cast<float>(sampleRate_);
        dcL_.setQ(20.0f, 0.70710678f, sr);
        dcR_.copyCoefficients(dcL_);
        sideHp_.setQ(100.0f, 0.70710678f, sr);
        sideHp300_.setQ(300.0f, 0.70710678f, sr);
        dcL_.reset(); dcR_.reset(); sideHp_.reset(); sideHp300_.reset();
        energyMid_ = energyLow_ = energyHigh_ = 0.0f;
        energyCoef_ = 1.0f - std::exp(-1.0f / (0.3f * sr));
        envAttack_ = std::exp(-1.0f / (0.005f * sr));
        envRelease_ = std::exp(-1.0f / (0.25f * sr));
        duckEnv_ = 0.0f;
        for (Strip& st : strips_) {
            st.hpL.reset(); st.hpR.reset(); st.lpL.reset(); st.lpR.reset();
            for (int c = 0; c < 2; ++c) {
                st.splitLo[c].setQ(300.0f, 0.70710678f, sr); st.splitLo[c].reset();
                st.splitHi[c].setQ(5000.0f, 0.70710678f, sr); st.splitHi[c].reset();
            }
        }
        subL_.setQ(80.0f, 0.70710678f, sr); subR_.copyCoefficients(subL_); subL_.reset(); subR_.reset();
        for (int k = 0; k < 2; ++k) {
            subSplitL_[k].setQ(80.0f, 0.70710678f, sr); subSplitL_[k].reset();
            subSplitR_[k].setQ(80.0f, 0.70710678f, sr); subSplitR_[k].reset();
            subHpL_[k].setQ(80.0f, 0.70710678f, sr); subHpL_[k].reset();
            subHpR_[k].setQ(80.0f, 0.70710678f, sr); subHpR_[k].reset();
        }
        subEnv_ = 0.0f;
        subAttack_ = std::exp(-1.0f / (0.001f * sr));
        subRelease_ = std::exp(-1.0f / (0.1f * sr));
        clipPrev_[0] = clipPrev_[1] = 0.0f;
        punchFastA_ = std::exp(-1.0f / (0.001f * sr)); punchFastR_ = std::exp(-1.0f / (0.02f * sr));
        punchSlowA_ = std::exp(-1.0f / (0.02f * sr)); punchSlowR_ = std::exp(-1.0f / (0.2f * sr));
        for (Strip& st : strips_) st.envFast = st.envSlow = 0.0f;
        for (int k = 0; k < 4; ++k) apL_[k] = apR_[k] = 0.0f;
        // The spread's all-passes turn their phase at corners spread over the band, different on each side:
        // a = (tan(pi f / fs) - 1) / (tan(pi f / fs) + 1).
        {
            static const float kFL[4] = { 250.0f, 700.0f, 1800.0f, 4500.0f }, kFR[4] = { 350.0f, 1000.0f, 2600.0f, 6500.0f };
            for (int k = 0; k < 4; ++k) {
                const float tl = std::tan(3.14159265f * kFL[k] / sr), tr = std::tan(3.14159265f * kFR[k] / sr);
                apCoefL_[k] = (tl - 1.0f) / (tl + 1.0f);
                apCoefR_[k] = (tr - 1.0f) / (tr + 1.0f);
            }
        }
        padEnv_ = 0.0f;
    }
    reverb_.prepare(sampleRate_);
    plate_.prepare(sampleRate_);
    blend_.prepare(sampleRate_);
    early_.prepare(sampleRate_);
    shimmer_.prepare(sampleRate_);
    {
        // The six bands (350 Hz .. 4.8 kHz, about three quarters of an octave apart).
        static const float kCentre[kBands] = { 350.0f, 600.0f, 1000.0f, 1700.0f, 2900.0f, 4800.0f };
        const float sr = static_cast<float>(sampleRate_);
        for (int k = 0; k < kBands; ++k) {
            for (Svf* f : { &rowSide_[k], &padSide_[k] }) { f->setQ(kCentre[k], 1.2f, sr); f->reset(); }
            for (Strip& st : strips_) for (int c = 0; c < 2; ++c) { st.band[c][k].setQ(kCentre[k], 2.0f, sr); st.band[c][k].reset(); }
            rowBandEnv_[k] = padBandEnv_[k] = 0.0f;
        }
        tamer_.prepare(sampleRate_);
    }
    tape_.prepare(sampleRate_, mixSeed(score_.seed, 500));
    tape_.setSingers(tapeSingers_);
    atmos_.prepare(sampleRate_, mixSeed(score_.seed, 600));
    strings_.prepare(sampleRate_, mixSeed(score_.seed, 700));
    poly_.prepare(sampleRate_, mixSeed(score_.seed, 900));
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
    // The same knob and offset as last time give the same value: compared bit for bit.
    std::array<float, 3>& c = playedCache_[static_cast<size_t>(id)];
    if (std::bit_cast<uint32_t>(c[0]) == std::bit_cast<uint32_t>(knob) && std::bit_cast<uint32_t>(c[1]) == std::bit_cast<uint32_t>(off))
        return c[2];
    const float norm = std::clamp(params_.toNormalised(id, knob) + off, 0.0f, 1.0f);
    c = { knob, off, params_.fromNormalised(id, norm) };
    return c[2];
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
    // The performer's hand on the rows' filters (perform.filter), in octaves; nothing at 0.
    if (m == Module::Voice) {
        const float grab = played(params_.id(Module::Perform, 0, perform::Filter));
        if (grab != 0.0f) s.cutoffHz *= std::exp2(grab);
    }
    s.resonance = v(voice::Resonance);
    s.envOctaves = v(voice::EnvAmount);
    s.decayMs = v(voice::Decay);
    s.keyTrack = v(voice::KeyTrack);
    s.accent = v(voice::Accent);
    s.releaseMs = v(voice::AmpDecay);
    s.glideMs = v(voice::Glide);
    if (m == Module::Voice) {
        // The rows' wavetables (the lead's and the drone's tables have other knobs from here on).
        s.table = static_cast<int>(std::lround(v(voice::Table)));
        s.tablePos = v(voice::TablePos);
        s.tableMod = v(voice::TableMod);
    }
    if (vibrato) {
        s.vibratoCents = v(lead::Vibrato);
        s.vibratoHz = v(lead::VibratoRate);
    }
    return s;
}

void Engine::setStrip(int s, float levelDb, float pan, float echo, float reverb, float width, float echo2, float distance)
{
    const float in[7] = { levelDb, pan, echo, reverb, width, echo2, distance };
    if (!changed(cache_.strip[s], in, cache_.valid)) return;
    Strip& st = strips_[s];
    // The distance macro (the addon's 2): level, highs and the hall's share move together, so nothing is near in
    // its level and far in the hall. From a table at 0, 0.3, 0.6 and 1: 0, -4, -10, -24 dB; open, 10, 5, 2.5 kHz;
    // the hall send up by 0, 0.2, 0.6, 1.
    {
        static const float kD[4] = { 0.0f, 0.3f, 0.6f, 1.0f }, kLevel[4] = { 0.0f, -4.0f, -10.0f, -24.0f };
        static const float kLp[4] = { 20000.0f, 10000.0f, 5000.0f, 2500.0f }, kSend[4] = { 0.0f, 0.2f, 0.6f, 1.0f };
        const float d = std::clamp(distance, 0.0f, 1.0f);
        int k = 0;
        while (k < 2 && d > kD[k + 1]) ++k;
        const float t = (d - kD[k]) / (kD[k + 1] - kD[k]);
        levelDb += kLevel[k] + t * (kLevel[k + 1] - kLevel[k]);
        reverb = std::min(1.0f, reverb + kSend[k] + t * (kSend[k + 1] - kSend[k]));
        st.lpHz = d > 0.001f ? kLp[k] * std::pow(kLp[k + 1] / kLp[k], t) : 0.0f;
        if (st.lpHz > 0.0f) {
            st.lpL.setQ(st.lpHz, 0.70710678f, static_cast<float>(sampleRate_));
            st.lpR.copyCoefficients(st.lpL);
        }
    }
    const float level = dbToGain(levelDb);
    const float a = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
    st.gainL = level * std::cos(a) * width;
    st.gainR = level * std::sin(a) * width;
    st.echo = echo;
    st.reverb = reverb;
    st.echo2 = echo2;
}

void Engine::setLowCut(int s, float hz)
{
    if (cache_.valid && cache_.lowCut[s] == hz) return;
    cache_.lowCut[s] = hz;
    Strip& st = strips_[s];
    st.lowCut = hz;
    if (hz > 0.0f) {
        st.hpL.setQ(hz, 0.70710678f, static_cast<float>(sampleRate_));
        st.hpR.copyCoefficients(st.hpL);
    }
}

void Engine::updateCell()
{
    // Gesture offsets at this cell's beat.
    const double beat = score_.tempo.beatAt(seconds());
    auto knob = [&](Module m, int index) { return played(params_.id(m, 0, index)); };
    // Hold (perform.hold): the hands let go, every gesture stands where it is until they take the knobs again.
    const bool hold = knob(Module::Perform, perform::Hold) >= 0.5f;
    for (Track& t : tracks_) {
        if (hold) break;
        // Positions only move forward, so the cursor only moves forward: the latest gesture that has
        // started is the one that counts (Score::gestureOffset), gestures.size() while none has.
        const size_t none = t.gestures.size();
        size_t c = t.cursor;
        size_t next = c == none ? 0 : c + 1;
        while (next < none && t.gestures[next].beat <= beat) c = next++;
        t.cursor = c;
        t.offset = c == none ? 0.0f : gestureValue(t.gestures[c], beat);
    }
    transpose_ = static_cast<int>(std::lround(knob(Module::Perform, perform::Transpose)));

    // The modular voices: the rows (their strips on the row module), the lead and the drone.
    for (int r = 0; r < kModVoices; ++r) {
        const bool row = r < kRows;
        const Module m = row ? Module::Voice : (r == kSrcLead ? Module::Lead : Module::Drone);
        const int inst = row ? r : 0;
        VoiceSettings vs = voiceSettings(m, inst, !row);
        if (row) {
            // The filter sweep (25.09.2026): the Berlin School's slow opening and closing of a sequence, a sine on the
            // cutoff with a period of its own per row (39 .. 79 s, irrational against the bars and each other), on
            // the piece's clock, over whatever the hands do; master.motion scales it with the other slow movements.
            static const double kPeriod[kRows] = { 53.1, 41.7, 67.3, 38.9, 79.1, 47.9, 61.3, 71.7 };
            const float depth = played(params_.id(Module::Row, r, row::Sweep)) * knob(Module::Master, master::Motion);
            if (depth > 0.0f) vs.cutoffHz *= std::exp2(depth * static_cast<float>(std::sin(6.283185307179586 * seconds() / kPeriod[r] + r)));
        }
        if (changed(cache_.voice[r], vs, cache_.valid)) voices_.set(r, vs);
        strips_[r].running = voices_.active(r) || voices_.held(r);
        auto s = [&](int rowIndex, int leadIndex) {
            return row ? played(params_.id(Module::Row, r, rowIndex)) : played(params_.id(m, 0, leadIndex));
        };
        float pan = s(row::Pan, lead::Pan);
        if (!row) {
            // The drone (or the lead) wanders slowly across the stereo field: lead.auto_pan, a sine of 0.05 Hz on the
            // piece's clock (the style guide's 7.3).
            const float depth = played(params_.id(m, 0, lead::AutoPan));
            if (depth > 0.0f) pan += depth * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 0.05 * seconds()));
        }
        setStrip(r, s(row::Level, lead::Level), pan, s(row::EchoSend, lead::EchoSend), s(row::ReverbSend, lead::ReverbSend), 1.0f,
                 row ? played(params_.id(Module::Row, r, row::Echo2Send)) : 0.0f, s(row::Distance, lead::Distance));
        setLowCut(r, s(row::LowCut, lead::LowCut));
        strips_[r].blend = s(row::BlendSend, lead::BlendSend);
        strips_[r].early = s(row::EarlySend, lead::EarlySend);
        strips_[r].shimmer = row ? 0.0f : played(params_.id(m, 0, lead::ShimmerSend));
        strips_[r].punch = row ? played(params_.id(Module::Row, r, row::Punch)) : 0.0f;
    }

    // The slow movements (the addon's 7): micro (under 2 s, a few tenths of a dB on the pads' levels) and meso
    // (13 to 41 s: the pads' hall sends, the strings' tone, the echo's feedback, the atmosphere's level), with
    // irrational periods that never lock into the sequence's 8- and 16-bar cycles; foreground and background never
    // breathe together. On the piece's clock, so the same everywhere; master.motion scales them (0 still).
    const float motion = knob(Module::Master, master::Motion);
    const double clock = seconds();
    auto wave = [&](double period) { return motion * static_cast<float>(std::sin(6.283185307179586 * clock / period)); };
    // The tape keys, the string machine, the drums, the atmosphere.
    TapeSettings ts;
    ts.set = static_cast<TapeSet>(std::clamp(static_cast<int>(std::lround(knob(Module::Tape, tape::Set))), 0, 2));
    ts.vowel = knob(Module::Tape, tape::Vowel);
    ts.wowCents = knob(Module::Tape, tape::Wow);
    ts.flutterCents = knob(Module::Tape, tape::Flutter);
    ts.sagCents = knob(Module::Tape, tape::Sag);
    ts.toneHz = knob(Module::Tape, tape::Tone);
    ts.age = knob(Module::Tape, tape::Age);
    if (changed(cache_.tape, ts, cache_.valid)) tape_.set(ts);
    strips_[kSrcTape].running = tape_.active();
    setStrip(kSrcTape, knob(Module::Tape, tape::Level) + 0.3f * wave(1.37) + 0.2f * wave(0.83), knob(Module::Tape, tape::Pan),
             knob(Module::Tape, tape::EchoSend), std::clamp(knob(Module::Tape, tape::ReverbSend) + 0.08f * wave(13.7), 0.0f, 1.0f),
             1.0f, 0.0f, knob(Module::Tape, tape::Distance));
    tapeSpread_ = knob(Module::Tape, tape::Spread);
    strips_[kSrcTape].blend = knob(Module::Tape, tape::BlendSend);
    strips_[kSrcTape].early = knob(Module::Tape, tape::EarlySend);
    strips_[kSrcTape].shimmer = knob(Module::Tape, tape::ShimmerSend);
    setLowCut(kSrcTape, knob(Module::Tape, tape::LowCut));

    StringSettings ss;
    ss.attackS = knob(Module::Strings, strings::Attack);
    ss.releaseS = knob(Module::Strings, strings::Release);
    ss.feet = knob(Module::Strings, strings::Feet);
    ss.toneHz = knob(Module::Strings, strings::Tone) * std::pow(2.0f, 0.5f * wave(17.3));
    ss.ensemble = knob(Module::Strings, strings::Ensemble);
    ss.registration = knob(Module::Strings, strings::Registration);
    ss.animate = knob(Module::Strings, strings::Animate);
    ss.animateHz = knob(Module::Strings, strings::AnimateRate);
    ss.ensembleType = static_cast<int>(std::lround(knob(Module::Strings, strings::EnsembleType)));
    ss.phaser = knob(Module::Strings, strings::Phaser);
    if (changed(cache_.strings, ss, cache_.valid)) strings_.set(ss);
    strips_[kSrcStrings].running = strings_.active();
    // The ensemble spreads the machine over both sides; the pan law's 3 dB come back with sqrt 2.
    setStrip(kSrcStrings, knob(Module::Strings, strings::Level) + 0.3f * wave(1.61) + 0.2f * wave(0.97), knob(Module::Strings, strings::Pan),
             knob(Module::Strings, strings::EchoSend), std::clamp(knob(Module::Strings, strings::ReverbSend) + 0.08f * wave(21.1), 0.0f, 1.0f),
             1.41421356f, 0.0f, knob(Module::Strings, strings::Distance));
    setLowCut(kSrcStrings, knob(Module::Strings, strings::LowCut));
    strips_[kSrcStrings].blend = knob(Module::Strings, strings::BlendSend);
    strips_[kSrcStrings].early = knob(Module::Strings, strings::EarlySend);
    strips_[kSrcStrings].shimmer = knob(Module::Strings, strings::ShimmerSend);

    PolySettings ps;
    ps.table = static_cast<int>(std::lround(knob(Module::Poly, poly::Table)));
    ps.position = knob(Module::Poly, poly::Position);
    ps.scan = knob(Module::Poly, poly::Scan);
    ps.scanHz = knob(Module::Poly, poly::ScanRate);
    ps.detuneCents = knob(Module::Poly, poly::Detune);
    ps.spread = knob(Module::Poly, poly::Spread);
    ps.driftCents = knob(Module::Poly, poly::Drift);
    ps.cutoffHz = knob(Module::Poly, poly::Cutoff) * std::pow(2.0f, 0.4f * wave(19.7));
    ps.resonance = knob(Module::Poly, poly::Resonance);
    ps.envOctaves = knob(Module::Poly, poly::EnvAmount);
    ps.attackS = knob(Module::Poly, poly::Attack);
    ps.releaseS = knob(Module::Poly, poly::Release);
    ps.chorus = knob(Module::Poly, poly::Chorus);
    if (changed(cache_.poly, ps, cache_.valid)) poly_.set(ps);
    strips_[kSrcPoly].running = poly_.active();
    setStrip(kSrcPoly, knob(Module::Poly, poly::Level) + 0.3f * wave(1.37) + 0.2f * wave(1.13), knob(Module::Poly, poly::Pan),
             knob(Module::Poly, poly::EchoSend), std::clamp(knob(Module::Poly, poly::ReverbSend) + 0.08f * wave(23.3), 0.0f, 1.0f),
             1.41421356f, 0.0f, knob(Module::Poly, poly::Distance));
    setLowCut(kSrcPoly, knob(Module::Poly, poly::LowCut));
    strips_[kSrcPoly].blend = knob(Module::Poly, poly::BlendSend);
    strips_[kSrcPoly].early = knob(Module::Poly, poly::EarlySend);
    strips_[kSrcPoly].shimmer = knob(Module::Poly, poly::ShimmerSend);

    DrumSettings ds;
    ds.kickHz = knob(Module::Drums, drums::KickHz);
    ds.decay = knob(Module::Drums, drums::Decay);
    ds.tone = knob(Module::Drums, drums::Tone);
    if (changed(cache_.drums, ds, cache_.valid)) drums_.set(ds);
    {
        // The kit pans its instruments itself (Drums.h): the strip only sets the level.
        Strip& d = strips_[kSrcDrums];
        d.running = drums_.active();
        d.gainL = d.gainR = dbToGain(knob(Module::Drums, drums::Level));
        d.echo = knob(Module::Drums, drums::EchoSend);
        d.reverb = knob(Module::Drums, drums::ReverbSend);
    }
    setLowCut(kSrcDrums, knob(Module::Drums, drums::LowCut));
    strips_[kSrcDrums].blend = knob(Module::Drums, drums::BlendSend);
    strips_[kSrcDrums].early = knob(Module::Drums, drums::EarlySend);
    strips_[kSrcAtmos].shimmer = knob(Module::Atmos, atmos::ShimmerSend);
    setLowCut(kSrcAtmos, knob(Module::Atmos, atmos::LowCut));

    auto gain = [](float db) { return db <= -59.9f ? 0.0f : dbToGain(db); };
    AtmosSettings as;
    as.windGain = gain(knob(Module::Atmos, atmos::Wind));
    as.windHz = knob(Module::Atmos, atmos::WindTone);
    as.sweepsPerMinute = knob(Module::Atmos, atmos::Sweeps);
    as.sweepGain = gain(knob(Module::Atmos, atmos::SweepLevel));
    as.bleepsPerMinute = knob(Module::Atmos, atmos::Bleeps);
    as.bleepGain = gain(knob(Module::Atmos, atmos::BleepLevel));
    as.grainGain = gain(knob(Module::Atmos, atmos::Grains));
    as.grainsPerSecond = knob(Module::Atmos, atmos::GrainDensity);
    // The bleeps take their notes from the root the rows are in.
    as.rootPc = pitchClass(score_.keyRoot + score_.rootAt(beat));
    as.scale = params_.getInt(params_.id(Module::Compose, 0, compose::Scale));
    if (changed(cache_.atmos, as, cache_.valid)) atmos_.set(as);
    {
        // Stereo already: the strip only sets the level; the echo takes the bleeps (renderSpan).
        Strip& at = strips_[kSrcAtmos];
        at.running = atmos_.active();
        at.gainL = at.gainR = dbToGain(knob(Module::Atmos, atmos::Level) + 3.0f * wave(41.3));   // a far layer swells by 6 dB
        at.echo = knob(Module::Atmos, atmos::EchoSend);
        at.reverb = knob(Module::Atmos, atmos::ReverbSend);
    }

    // The echo throw (perform.throw): every send up, and the echo's feedback with them (below).
    const float throwAmount = knob(Module::Perform, perform::Throw);
    echoThrow_ = 0.8f * throwAmount;

    // The rooms and the master.
    EchoSettings es;
    const double bpm = score_.tempo.bpmAt(beat);
    es.delaySeconds = echoTimeBeats(static_cast<EchoTime>(static_cast<int>(knob(Module::Echo, echo::Time)))) * 60.0 / bpm;
    es.feedback = knob(Module::Echo, echo::Feedback) + 0.04f * wave(29.9);
    if (throwAmount > 0.0f) es.feedback += (0.9f - es.feedback) * throwAmount;
    es.toneHz = knob(Module::Echo, echo::Tone);
    es.wowMs = knob(Module::Echo, echo::Wow);
    es.flutterMs = knob(Module::Echo, echo::Flutter);
    es.driveDb = knob(Module::Echo, echo::Drive);
    es.pingPong = knob(Module::Echo, echo::PingPong) >= 0.5f;
    es.lowCutHz = knob(Module::Echo, echo::LowCut);
    bbdOn_ = knob(Module::Echo, echo::Type) >= 0.5f;
    const EchoSettings& le = cache_.echo;
    if (!cache_.valid || cache_.echoType != static_cast<int>(bbdOn_) || le.delaySeconds != es.delaySeconds || le.feedback != es.feedback
        || le.toneHz != es.toneHz || le.wowMs != es.wowMs || le.flutterMs != es.flutterMs || le.driveDb != es.driveDb || le.pingPong != es.pingPong
        || le.lowCutHz != es.lowCutHz) {
        cache_.echo = es;
        cache_.echoType = static_cast<int>(bbdOn_);
        if (bbdOn_) bbd_.set(es); else echo_.set(es);
    }
    echoReturn_ = dbToGain(knob(Module::Echo, echo::Return));
    // The second echo: clean, a little wow, its own time and return.
    EchoSettings e2;
    e2.delaySeconds = echoTimeBeats(static_cast<EchoTime>(static_cast<int>(knob(Module::Echo2, echo2::Time)))) * 60.0 / bpm;
    e2.feedback = knob(Module::Echo2, echo2::Feedback);
    e2.toneHz = knob(Module::Echo2, echo2::Tone);
    e2.wowMs = 0.1f;      // clean: no timing smear on the counter rows (the production guide's 8.1)
    e2.flutterMs = 0.01f;
    e2.driveDb = 2.0f;
    e2.pingPong = knob(Module::Echo2, echo2::PingPong) >= 0.5f;
    e2.lowCutHz = knob(Module::Echo2, echo2::LowCut);
    const EchoSettings& l2 = cache_.echo2;
    if (!cache_.valid || l2.delaySeconds != e2.delaySeconds || l2.feedback != e2.feedback || l2.toneHz != e2.toneHz
        || l2.pingPong != e2.pingPong || l2.lowCutHz != e2.lowCutHz) {
        cache_.echo2 = e2;
        echo2_.set(e2);
    }
    echo2Return_ = dbToGain(knob(Module::Echo2, echo2::Return));
    echoDuckDb_ = knob(Module::Echo, echo::Duck);
    hallDuckDb_ = knob(Module::Reverb, reverb::Duck);
    width_ = dbToGain(knob(Module::Master, master::Width));
    mono_ = knob(Module::Master, master::Mono) >= 0.5f;
    subSolo_ = knob(Module::Master, master::SubSolo) >= 0.5f;
    const float cascade = knob(Module::Master, master::Cascade);
    if (cascade != cascadeDb_ || !cache_.valid) {
        // The duck's gains from its amount, once per change: 256 steps are finer than anyone hears (at most 0.02 dB).
        cascadeDb_ = cascade;
        for (int k = 0; k < 256; ++k) duckTable_[k] = dbToGain(-cascadeDb_ * static_cast<float>(k) / 255.0f);
    }
    plateOn_ = knob(Module::Reverb, reverb::Type) >= 0.5f;
    const float hall[7] = { knob(Module::Reverb, reverb::Size), knob(Module::Reverb, reverb::Decay), knob(Module::Reverb, reverb::Damping),
                            knob(Module::Reverb, reverb::PreDelay) * 0.001f * static_cast<float>(sampleRate_),
                            knob(Module::Reverb, reverb::LowCut), knob(Module::Reverb, reverb::HighCut), plateOn_ ? 1.0f : 0.0f };
    if (changed(cache_.reverb, hall, cache_.valid)) {
        if (plateOn_) plate_.set(hall[1], hall[2], hall[3], hall[4], hall[5]);
        else reverb_.set(hall[0], hall[1], hall[2], hall[3], hall[4], hall[5]);
    }
    reverbReturn_ = dbToGain(knob(Module::Reverb, reverb::Return));
    const float blendSet[5] = { knob(Module::Blend, blend::Decay), knob(Module::Blend, blend::Damping),
                                knob(Module::Blend, blend::PreDelay) * 0.001f * static_cast<float>(sampleRate_),
                                knob(Module::Blend, blend::LowCut), knob(Module::Blend, blend::HighCut) };
    if (changed(cache_.blend, blendSet, cache_.valid)) blend_.set(blendSet[0], blendSet[1], blendSet[2], blendSet[3], blendSet[4]);
    blendReturn_ = dbToGain(knob(Module::Blend, blend::Return));
    blendIntoHall_ = knob(Module::Blend, blend::IntoHall);
    const float earlySet[3] = { knob(Module::Early, early::Size), knob(Module::Early, early::LowCut), knob(Module::Early, early::HighCut) };
    if (changed(cache_.early, earlySet, cache_.valid)) early_.set(earlySet[0], earlySet[1], earlySet[2]);
    earlyReturn_ = dbToGain(knob(Module::Early, early::Return));
    const float shimSet[4] = { knob(Module::Shimmer, shimmer::Decay), knob(Module::Shimmer, shimmer::Amount),
                               knob(Module::Shimmer, shimmer::LowCut), knob(Module::Shimmer, shimmer::HighCut) };
    if (changed(cache_.shimmer, shimSet, cache_.valid)) shimmer_.set(shimSet[0], shimSet[1], shimSet[2], shimSet[3]);
    shimmerReturn_ = dbToGain(knob(Module::Shimmer, shimmer::Return));
    tame_ = knob(Module::Master, master::Tame);
    clipDb_ = knob(Module::Master, master::Clip);
    clipCeiling_ = dbToGain(knob(Module::Master, master::Ceiling));
    subCeiling_ = dbToGain(knob(Module::Master, master::SubCeiling));
    const float springs[2] = { knob(Module::Spring, spring::Decay), knob(Module::Spring, spring::Tone) };
    if (changed(cache_.spring, springs, cache_.valid)) spring_.set(springs[0], springs[1]);
    springReturn_ = gain(knob(Module::Spring, spring::Return));
    master_ = dbToGain(knob(Module::Master, master::Level));
    const float compress = knob(Module::Master, master::Compress), ceiling = knob(Module::Master, master::Ceiling);
    if (changed(cache_.compress, compress, cache_.valid)) comp_.set(-16.0f, 1.0f + 0.5f * compress, 6.0f, 30.0f, 300.0f);
    if (changed(cache_.ceiling, ceiling, cache_.valid)) limiter_.set(ceiling, 150.0f);
    cache_.valid = true;
}

const char* Engine::channelName(int c)
{
    static_assert(kChannels == kSources, "a meter per source");
    static const char* const names[kChannels] = { "Row 1", "Row 2", "Row 3", "Row 4", "Row 5", "Row 6", "Row 7", "Row 8",
                                                  "Lead", "Drone", "Tape Keys", "Strings", "Poly", "Drums", "Atmosphere" };
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

void Engine::mix(int source, const float* xl, const float* xr, int n, const Buses& b, bool echoSend, const float* midGain)
{
    Strip& strip = strips_[source];
    const bool mono = xl == xr;
    // The strip's low cut first (the production guide's 4.2): every role keeps out of the bands below its own.
    float fl[kCell], fr[kCell];
    if (strip.lowCut > 0.0f) {
        float lo, bo, hp;
        for (int i = 0; i < n; ++i) { strip.hpL.tick(xl[i], lo, bo, hp); fl[i] = hp; }
        if (xr == xl) std::copy(fl, fl + n, fr);
        else for (int i = 0; i < n; ++i) { strip.hpR.tick(xr[i], lo, bo, hp); fr[i] = hp; }
        xl = fl;
        xr = fr;
    }
    // The punch (the production guide's 7.5): a fast and a slow follower; where the fast runs ahead -- an attack --
    // the gain rises by up to 6 dB, the sustain stays. Rows only, and only where it is set.
    if (strip.punch > 0.0f) {
        const float k = strip.punch;
        for (int i = 0; i < n; ++i) {
            const float x = std::max(std::fabs(xl[i]), std::fabs(xr[i]));
            strip.envFast = x + (x > strip.envFast ? punchFastA_ : punchFastR_) * (strip.envFast - x);
            strip.envSlow = x + (x > strip.envSlow ? punchSlowA_ : punchSlowR_) * (strip.envSlow - x);
            const float ratio = strip.envSlow > 1e-6f ? strip.envFast / strip.envSlow : 1.0f;
            const float g = std::clamp(1.0f + k * (ratio - 1.0f), 1.0f, 2.0f);
            fl[i] = xl[i] * g;
            fr[i] = xr[i] * g;
        }
        xl = fl;
        xr = fr;
    }
    // The distance's high loss (the addon's 2).
    if (strip.lpHz > 0.0f) {
        for (int i = 0; i < n; ++i) fl[i] = strip.lpL.lp(xl[i]);
        if (mono) std::copy(fl, fl + n, fr);
        else for (int i = 0; i < n; ++i) fr[i] = strip.lpR.lp(xr[i]);
        xl = fl;
        xr = fr;
    }
    // The cascaded, spectral duck (the addon's 4): six bands between 350 Hz and 4.8 kHz, each stepping back by its own
    // gain (midGain holds kBands rows of kCell): only where the side chain has energy; the body and the air stay.
    if (midGain != nullptr) {
        float* out[2] = { fl, fr };
        const float* in[2] = { xl, xr };
        for (int c = 0; c < (mono ? 1 : 2); ++c) {
            for (int i = 0; i < n; ++i) {
                const float x = in[c][i];
                float y = x;
                for (int k = 0; k < kBands; ++k) {
                    float lo, bp, hp;
                    strip.band[c][k].tick(x, lo, bp, hp);
                    y += (midGain[k * kCell + i] - 1.0f) * bp * strip.band[c][k].k;
                }
                out[c][i] = y;
            }
        }
        if (mono) std::copy(fl, fl + n, fr);
        xl = fl;
        xr = fr;
    }
    const float echo = strip.echo + echoThrow_;   // the throw on top of the send (exactly the send without it)
    for (int i = 0; i < n; ++i) {
        const float l = xl[i] * strip.gainL, r = xr[i] * strip.gainR;
        // The rows go onto their own bus (the resonance suppressor works on it, renderSpan); the rest into the mix.
        if (source >= kRows) { b.L[i] += l; b.R[i] += r; }
        if (echoSend) { b.echoL[i] += l * echo; b.echoR[i] += r * echo; }
        b.hallL[i] += l * strip.reverb;
        b.hallR[i] += r * strip.reverb;
        b.echo2L[i] += l * strip.echo2;
        b.echo2R[i] += r * strip.echo2;
        if (source < kRows) { b.rowsL[i] += l; b.rowsR[i] += r; }
        if (source == kSrcStrings || source == kSrcTape || source == kSrcPoly) { b.padsL[i] += l; b.padsR[i] += r; }
        b.blendL[i] += l * strip.blend;
        b.blendR[i] += r * strip.blend;
        b.earlyL[i] += l * strip.early;
        b.earlyR[i] += r * strip.early;
        b.shimL[i] += l * strip.shimmer;
        b.shimR[i] += r * strip.shimmer;
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
    if (stemL_ != nullptr) {
        float* sl = stemL_[source] + spanAt_;
        float* sr = stemR_[source] + spanAt_;
        for (int i = 0; i < n; ++i) { sl[i] = xl[i] * strip.gainL; sr[i] = xr[i] * strip.gainR; }
    }
}

void Engine::renderSpan(float* L, float* R, int n)
{
    float bufL[kCell], bufR[kCell], echoL[kCell], echoR[kCell], hallInL[kCell], hallInR[kCell], echo2L[kCell], echo2R[kCell];
    float rowsL[kCell] = {}, rowsR[kCell] = {}, padsL[kCell] = {}, padsR[kCell] = {}, blendInL[kCell] = {}, blendInR[kCell] = {};
    float earlyInL[kCell] = {}, earlyInR[kCell] = {}, shimInL[kCell] = {}, shimInR[kCell] = {};
    std::fill(L, L + n, 0.0f);
    std::fill(R, R + n, 0.0f);
    std::fill(echoL, echoL + n, 0.0f);
    std::fill(echoR, echoR + n, 0.0f);
    std::fill(hallInL, hallInL + n, 0.0f);
    std::fill(hallInR, hallInR + n, 0.0f);
    std::fill(echo2L, echo2L + n, 0.0f);
    std::fill(echo2R, echo2R + n, 0.0f);
    const Buses b{ L, R, echoL, echoR, hallInL, hallInR, echo2L, echo2R, rowsL, rowsR, padsL, padsR, blendInL, blendInR,
                   earlyInL, earlyInR, shimInL, shimInR };
    if (metering_) meterCount_ += n;

    // The sources, each through its strip, in a fixed order (the order of the sums is part of the result).
    bool run[kModVoices];
    for (int r = 0; r < kModVoices; ++r) run[r] = strips_[r].running;
    voices_.process(run, n);
    for (int r = 0; r < kModVoices; ++r)
        if (run[r]) mix(r, voices_.output(r), voices_.output(r), n, b);
    // The rows' envelope, sample by sample (so the result does not depend on how a block is cut into spans): it ducks
    // the rooms' returns (the production guide's 5.2, 5.4) and, in their middle band, the pads (the addon's 4). From
    // -30 dBFS on, fully at -12.
    // The rows' bus through the resonance suppressor (the production guide's 4.4, 8.2): six bands, each one's follower
    // (3 ms up, 60 ms down) against its neighbours'; a band more than 4.5 dB over them is pulled back 3:1 (master.tame scales
    // the ratio), so a filter's resonance that sweeps through does not stick out; then into the mix.
    {
        float tl[kCell], tr[kCell];
        std::copy(rowsL, rowsL + n, tl);
        std::copy(rowsR, rowsR + n, tr);
        tamer_.process(tl, tr, n, tame_);
        for (int i = 0; i < n; ++i) { L[i] += tl[i]; R[i] += tr[i]; }
    }
    float duckAmount[kCell], rowGain[kBands * kCell], padGain[kBands * kCell];
    for (int i = 0; i < n; ++i) {
        const float x = std::max(std::fabs(rowsL[i]), std::fabs(rowsR[i]));
        duckEnv_ = x + (x > duckEnv_ ? envAttack_ : envRelease_) * (duckEnv_ - x);
        duckAmount[i] = std::clamp((duckEnv_ - 0.0316f) / (0.2512f - 0.0316f), 0.0f, 1.0f);
        // The side chain in the six bands: each band of the pads steps back where the rows have energy in it.
        const float mono = 0.5f * (rowsL[i] + rowsR[i]);
        for (int k = 0; k < kBands; ++k) {
            float lo, bp, hp;
            rowSide_[k].tick(mono, lo, bp, hp);
            const float bx = std::fabs(bp * rowSide_[k].k);
            rowBandEnv_[k] = bx + (bx > rowBandEnv_[k] ? envAttack_ : envRelease_) * (rowBandEnv_[k] - bx);
            rowGain[k * kCell + i] = duckTable_[static_cast<int>(255.0f * std::clamp((rowBandEnv_[k] - 0.01f) / (0.08f - 0.01f), 0.0f, 1.0f))];
        }
    }
    if (strips_[kSrcDrums].running) {
        drums_.process(bufL, bufR, n);
        mix(kSrcDrums, bufL, bufR, n, b);
    }
    if (strips_[kSrcStrings].running) {
        strings_.process(bufL, bufR, n);
        mix(kSrcStrings, bufL, bufR, n, b, true, rowGain);
    }
    if (strips_[kSrcPoly].running) {
        poly_.process(bufL, bufR, n);
        mix(kSrcPoly, bufL, bufR, n, b, true, rowGain);
    }
    if (strips_[kSrcTape].running) {
        tape_.process(bufL, n);
        if (tapeSpread_ > 0.0f) {
            // The mono keyboard widened by allpass decorrelation (the addon's 6): the side is the direct sound through
            // a chain of eight first-order all-passes whose phase turns at corners spread over the band, so left and
            // right differ in phase, not in colour. Their sum is the direct sound alone: nothing changes in mono.
            // The correlation is about (1 - g^2) / (1 + g^2): 0.34 at the default 0.7, never under 0.
            const float g = std::clamp(tapeSpread_, 0.0f, 1.0f), norm = 1.0f / std::sqrt(1.0f + g * g);
            for (int i = 0; i < n; ++i) {
                float a = bufL[i];
                for (int s = 0; s < 4; ++s) {
                    const float ya = apCoefL_[s] * a + apL_[s]; apL_[s] = a - apCoefL_[s] * ya; a = ya;
                    const float yb = apCoefR_[s] * a + apR_[s]; apR_[s] = a - apCoefR_[s] * yb; a = yb;
                }
                bufR[i] = norm * (bufL[i] - g * a);
                bufL[i] = norm * (bufL[i] + g * a);
            }
            mix(kSrcTape, bufL, bufR, n, b, true, rowGain);
        } else {
            mix(kSrcTape, bufL, bufL, n, b, true, rowGain);
        }
    }
    // The pads' envelope ducks the atmosphere's middle band in turn.
    for (int i = 0; i < n; ++i) {
        const float x = std::max(std::fabs(padsL[i]), std::fabs(padsR[i]));
        padEnv_ = x + (x > padEnv_ ? envAttack_ : envRelease_) * (padEnv_ - x);
        const float mono = 0.5f * (padsL[i] + padsR[i]);
        for (int k = 0; k < kBands; ++k) {
            float lo, bp, hp;
            padSide_[k].tick(mono, lo, bp, hp);
            const float y = std::fabs(bp * padSide_[k].k);
            padBandEnv_[k] = y + (y > padBandEnv_[k] ? envAttack_ : envRelease_) * (padBandEnv_[k] - y);
            padGain[k * kCell + i] = duckTable_[static_cast<int>(255.0f * std::clamp((padBandEnv_[k] - 0.01f) / (0.08f - 0.01f), 0.0f, 1.0f))];
        }
    }
    if (strips_[kSrcAtmos].running) {
        // The whole atmosphere goes to the mix and the hall, only its bleeps to the echo.
        float bleepL[kCell] = {}, bleepR[kCell] = {};
        std::fill(bufL, bufL + n, 0.0f);
        std::fill(bufR, bufR + n, 0.0f);
        atmos_.process(bufL, bufR, bleepL, bleepR, n);
        const Strip& at = strips_[kSrcAtmos];
        mix(kSrcAtmos, bufL, bufR, n, b, false, padGain);
        for (int i = 0; i < n; ++i) {
            echoL[i] += bleepL[i] * at.gainL * (0.5f + at.echo + echoThrow_);
            echoR[i] += bleepR[i] * at.gainR * (0.5f + at.echo + echoThrow_);
        }
    }

    // The tape echo, its springs, the hall, the master.
    float wetL[kCell] = {}, wetR[kCell] = {}, sprL[kCell] = {}, sprR[kCell] = {}, hallL[kCell], hallR[kCell];
    if (bbdOn_) bbd_.process(echoL, echoR, wetL, wetR, n);
    else echo_.process(echoL, echoR, wetL, wetR, n);
    // The springs take the echo's send, as in a tape echo with springs built in; they return beside it.
    spring_.process(echoL, echoR, sprL, sprR, n);
    for (int i = 0; i < n; ++i) {
        wetL[i] += sprL[i] * springReturn_ / std::max(echoReturn_, 1e-6f);
        wetR[i] += sprR[i] * springReturn_ / std::max(echoReturn_, 1e-6f);
    }
    // The rooms' returns ducked by the rows (duckAmount above): the tails grow in the gaps, step back under the notes.
    const float echoK = -echoDuckDb_ * 0.11512925f, hallK = -hallDuckDb_ * 0.11512925f;   // ln 10 / 20
    // The second echo, beside the first.
    float wet2L[kCell] = {}, wet2R[kCell] = {};
    echo2_.process(echo2L, echo2R, wet2L, wet2R, n);
    // Both echoes' repeats at about 70 % of the base, not hard left and right (the production guide's 5.4): the
    // middle keeps a share of each repeat, so the ping-pong stays audible in mono.
    for (int i = 0; i < n; ++i) {
        const float l = wetL[i], r = wetR[i], l2 = wet2L[i], r2 = wet2R[i];
        wetL[i] = 0.85f * l + 0.15f * r;
        wetR[i] = 0.85f * r + 0.15f * l;
        wet2L[i] = 0.85f * l2 + 0.15f * r2;
        wet2R[i] = 0.85f * r2 + 0.15f * l2;
    }
    // The echoes' repeats go into the hall too, as they do on a desk where the echo returns to a channel.
    for (int i = 0; i < n; ++i) {
        hallInL[i] += wetL[i] * echoReturn_ * 0.5f + wet2L[i] * echo2Return_ * 0.5f;
        hallInR[i] += wetR[i] * echoReturn_ * 0.5f + wet2R[i] * echo2Return_ * 0.5f;
    }
    // The blend room (the addon's serial far space): a short plate; a share of its return goes on into the hall, so the
    // far room sounds like the near one going on, not like a second place.
    float blendL[kCell] = {}, blendR[kCell] = {}, earlyL[kCell] = {}, earlyR[kCell] = {}, shimL[kCell] = {}, shimR[kCell] = {};
    blend_.process(blendInL, blendInR, blendL, blendR, n);
    early_.process(earlyInL, earlyInR, earlyL, earlyR, n);
    shimmer_.process(shimInL, shimInR, shimL, shimR, n);
    for (int i = 0; i < n; ++i) {
        hallInL[i] += blendL[i] * blendReturn_ * blendIntoHall_;
        hallInR[i] += blendR[i] * blendReturn_ * blendIntoHall_;
    }
    if (plateOn_) {
        std::fill(hallL, hallL + n, 0.0f);
        std::fill(hallR, hallR + n, 0.0f);
        plate_.process(hallInL, hallInR, hallL, hallR, n);
    } else {
        reverb_.process(hallInL, hallInR, hallL, hallR, n);
    }
    for (int i = 0; i < n; ++i) {
        const float eg = std::exp(echoK * duckAmount[i]), hg = std::exp(hallK * duckAmount[i]);
        const float rl = (wetL[i] * echoReturn_ + wet2L[i] * echo2Return_) * eg
                       + (hallL[i] * reverbReturn_ + blendL[i] * blendReturn_ + earlyL[i] * earlyReturn_) * hg + shimL[i] * shimmerReturn_;
        const float rr = (wetR[i] * echoReturn_ + wet2R[i] * echo2Return_) * eg
                       + (hallR[i] * reverbReturn_ + blendR[i] * blendReturn_ + earlyR[i] * earlyReturn_) * hg + shimR[i] * shimmerReturn_;
        if (stemL_ != nullptr) {
            stemL_[kChannels][spanAt_ + i] = rl;
            stemR_[kChannels][spanAt_ + i] = rr;
        }
        L[i] = (L[i] + rl) * master_;
        R[i] = (R[i] + rr) * master_;
    }
    // The mix bus (the production guide's 3.3, 6.3, 6.4): a 20 Hz high pass for DC and subsonics; the side through a
    // 12 dB/octave high pass at 100 Hz (mono beneath), plus the widening by master.width on what lies above 300 Hz --
    // both Butterworth, so no bass leaks into the widening.
    for (int i = 0; i < n; ++i) {
        float lo, bo, hl, hr;
        dcL_.tick(L[i], lo, bo, hl);
        dcR_.tick(R[i], lo, bo, hr);
        const float m = 0.5f * (hl + hr), s = 0.5f * (hl - hr);
        float sl, sb, sh, l3, b3, high;
        sideHp_.tick(s, sl, sb, sh);
        sideHp300_.tick(s, l3, b3, high);
        // The side never over the mid (6.5): the widening stops where the side's energy would come within 3 dB of
        // the mid's (energies over about 300 ms), so decorrelated spaces are not pushed into negative correlation.
        energyMid_ += energyCoef_ * (m * m - energyMid_);
        energyLow_ += energyCoef_ * (sh * sh - energyLow_);
        energyHigh_ += energyCoef_ * (high * high - energyHigh_);
        float g = width_;
        if (g > 1.0f) {
            const float room = 0.5f * energyMid_ - energyLow_;
            const float most = room > 0.0f && energyHigh_ > 1e-12f ? std::sqrt(1.0f + room / energyHigh_) : 1.0f;
            g = std::clamp(most, 1.0f, g);
        }
        const float side = sh + (g - 1.0f) * high;
        L[i] = m + side;
        R[i] = m - side;
    }
    // The band under 80 Hz limited on its own (the addon's 5): the sub never pushes the whole mix into the limiter.
    // A Linkwitz-Riley crossover (two Butterworth sections each way): both bands turn their phase alike, so their sum
    // is flat, and only the low one is held under its ceiling.
    for (int i = 0; i < n; ++i) {
        float lo, bp, hp, hl[2], hr[2];
        subHpL_[0].tick(L[i], lo, bp, hl[0]); subHpL_[1].tick(hl[0], lo, bp, hl[1]);
        subHpR_[0].tick(R[i], lo, bp, hr[0]); subHpR_[1].tick(hr[0], lo, bp, hr[1]);
        const float lowL = subSplitL_[1].lp(subSplitL_[0].lp(L[i])), lowR = subSplitR_[1].lp(subSplitR_[0].lp(R[i]));
        const float x = std::max(std::fabs(lowL), std::fabs(lowR));
        subEnv_ = x + (x > subEnv_ ? subAttack_ : subRelease_) * (subEnv_ - x);
        const float g = subEnv_ > subCeiling_ ? subCeiling_ / subEnv_ : 1.0f;
        L[i] = hl[1] + g * lowL;
        R[i] = hr[1] + g * lowR;
        (void)hp;
    }
    comp_.process(L, R, n);
    // The soft clipper (the production guide's 7.4): peaks above the limiter's ceiling are rounded into at most clipDb_
    // above it, so short ratchet and percussion peaks do not make the limiter work; first-order antiderivative
    // anti-aliasing (the difference quotient of the curve's integral) keeps the harmonics it makes from folding back.
    if (clipDb_ > 0.01f) {
        const float t = clipCeiling_;
        const float kk = t * (dbToGain(clipDb_) - 1.0f);
        auto curve = [&](float x) {
            const float a = std::fabs(x);
            return a <= t ? x : std::copysign(t + kk * std::tanh((a - t) / kk), x);
        };
        auto integral = [&](float x) {
            const float a = std::fabs(x);
            if (a <= t) return 0.5f * x * x;
            const float u = (a - t) / kk;
            // ln cosh u, stable for large u
            const float lc = u > 15.0f ? u - 0.69314718f : std::log(std::cosh(u));
            return 0.5f * t * t + t * (a - t) + kk * kk * lc;
        };
        float* ch[2] = { L, R };
        for (int c = 0; c < 2; ++c) {
            float prev = clipPrev_[c];
            for (int i = 0; i < n; ++i) {
                const float x = ch[c][i];
                const float d = x - prev;
                // Under the ceiling on both sides the sample passes untouched (no averaging of the whole mix).
                const float y = std::fabs(x) <= t && std::fabs(prev) <= t ? x
                              : (std::fabs(d) > 1e-5f ? (integral(x) - integral(prev)) / d : curve(0.5f * (x + prev)));
                prev = x;
                ch[c][i] = y;
            }
            clipPrev_[c] = prev;
        }
    }
    if (limiterOn_) limiter_.process(L, R, n);
    // Listening: the sub alone (an 80 Hz low pass, the addon's 5), and mono.
    if (subSolo_)
        for (int i = 0; i < n; ++i) { L[i] = subL_.lp(L[i]); R[i] = subR_.lp(R[i]); }
    if (mono_)
        for (int i = 0; i < n; ++i) L[i] = R[i] = 0.5f * (L[i] + R[i]);
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
    poly_.silence();
    drums_.prepare(sampleRate_, mixSeed(score_.seed, 800));
    cache_.valid = false;   // the components were reset: every setter runs again
    for (Track& t : tracks_) t.cursor = t.gestures.size();
    cellDirty_ = true;
}

bool Engine::process(float* L, float* R, int n)
{
    if (stemL_ != nullptr)
        for (int c = 0; c <= kChannels; ++c) { std::fill(stemL_[c], stemL_[c] + n, 0.0f); std::fill(stemR_[c], stemR_[c] + n, 0.0f); }
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
                if (e.on) strings_.noteOn(std::clamp(e.pitch + transpose_, 0, 127), e.velocity, e.id); else strings_.noteOff(e.id);
                break;
            case kSrcPoly:
                if (e.on) poly_.noteOn(std::clamp(e.pitch + transpose_, 0, 127), e.velocity, e.id); else poly_.noteOff(e.id);
                break;
            case kSrcTape:
                if (e.on) tape_.noteOn(std::clamp(e.pitch + transpose_, 0, 127), e.velocity, e.id); else tape_.noteOff(e.id);
                break;
            default:
                if (e.on) voices_.noteOn(e.source, std::clamp(e.pitch + transpose_, 0, 127), e.velocity, e.accent, e.legato, e.id, e.bright, e.decay);
                else voices_.noteOff(e.source, e.id);
                break;
            }
            if (e.on) strips_[e.source].running = true;
        }
        int64_t end = std::min<int64_t>(sample_ + (n - done), (sample_ / kCell + 1) * kCell);
        if (evCursor_ < events_.size()) end = std::min(end, events_[evCursor_].sample);
        const int len = static_cast<int>(end - sample_);
        spanAt_ = done;
        renderSpan(L + done, R + done, len);
        sample_ += len;
        done += len;
    }
    return seconds() < lengthSeconds();
}

} // namespace eph
