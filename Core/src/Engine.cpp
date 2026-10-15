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
constexpr int Cell = 32;   ///< the parameter raster in samples
}

void Engine::prepare(double sampleRate, int maxBlock)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_maxBlock = std::max(1, maxBlock);
    m_sample = 0;
}

void Engine::load(const Score& score, bool sounds)
{
    m_score = score;
    m_score.sort();
    m_cueMarks = cueMarksOf(m_score);
    m_lateDb = 0.0f;
    m_sample = 0;
    m_evCursor = 0;
    // The composer's sounds at the start (Score::knobs): on the knobs now, or already there (the caller's word).
    m_knobCursor = 0;
    m_soundGroup = -1.0;
    if (sounds) {
        applyKnobs(0.0);
    } else {
        while (m_knobCursor < m_score.m_knobs.size() && m_score.m_knobs[m_knobCursor].m_beat <= 1e-9) ++m_knobCursor;
        if (m_knobCursor > 0) m_soundGroup = 0.0;
    }

    // Notes onto the sample grid. A note whose predecessor on the same source slides into it is legato:
    // the voice glides instead of retriggering.
    m_events.clear();
    int id = 0;
    bool slideInto[SOURCES] = {};
    for (const NoteEvent& n : m_score.m_notes) {
        int src = static_cast<int>(n.m_part) - static_cast<int>(PartE::ROW1);
        switch (n.m_part) {
        case PartE::LEAD:     src = SRC_LEAD; break;
        case PartE::DRONE:    src = SRC_DRONE; break;
        case PartE::TAPE_KEYS: src = SRC_TAPE; break;
        case PartE::STRINGS:  src = SRC_STRINGS; break;
        case PartE::PAD:      src = SRC_POLY; break;
        case PartE::DRUMS:    src = SRC_DRUMS; break;
        default: if (src < 0 || src >= Rows) continue;
        }
        const int64_t on = std::llround(m_score.m_tempo.secondsAt(n.m_beat) * m_sampleRate);
        const int64_t off = std::max(on + 1, static_cast<int64_t>(std::llround(m_score.m_tempo.secondsAt(n.m_beat + n.m_length) * m_sampleRate)));
        ++id;
        m_events.push_back({ on, 1, static_cast<uint8_t>(src), n.m_accent, slideInto[src], n.m_pitch, n.m_velocity, id, n.m_bright, n.m_decay });
        m_events.push_back({ off, 0, static_cast<uint8_t>(src), false, false, n.m_pitch, 0.0f, id });
        slideInto[src] = n.m_slide;
    }
    std::stable_sort(m_events.begin(), m_events.end(), [](const Ev& a, const Ev& b) {
        return a.m_sample != b.m_sample ? a.m_sample < b.m_sample : a.m_on < b.m_on;
    });
    m_offAt.assign(static_cast<size_t>(id) + 1, 0);
    m_tapPitch.assign(m_offAt.size(), 0xFF);
    for (const Ev& e : m_events) if (!e.m_on) m_offAt[static_cast<size_t>(e.m_id)] = e.m_sample;

    // Gestures grouped by the knob they move.
    m_tracks.clear();
    m_trackOf.assign(static_cast<size_t>(m_params.count()), -1);
    m_playedCache.assign(static_cast<size_t>(m_params.count()), { std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f });
    m_cache.m_valid = false;
    for (const Gesture& g : m_score.m_gestures) {
        if (g.m_param < 0 || g.m_param >= m_params.count()) continue;
        int& t = m_trackOf[static_cast<size_t>(g.m_param)];
        if (t < 0) { t = static_cast<int>(m_tracks.size()); m_tracks.push_back({ g.m_param, {}, 0, 0.0f }); }
        m_tracks[static_cast<size_t>(t)].m_gestures.push_back(g);
    }
    for (Track& t : m_tracks) t.m_cursor = t.m_gestures.size();

    // Every source and room from its own seed branch.
    uint64_t voiceSeeds[ModVoices];
    for (int r = 0; r < ModVoices; ++r) voiceSeeds[r] = mixSeed(m_score.m_seed, 100 + static_cast<uint64_t>(r));
    static_assert(ModVoices == BankVoices, "the voice bank holds the rows, the lead and the drone");
    m_voices.prepare(m_sampleRate, voiceSeeds);
    m_echo.prepare(m_sampleRate, 2.5, mixSeed(m_score.m_seed, 200));
    m_bbd.prepare(m_sampleRate, 2.5, mixSeed(m_score.m_seed, 210));
    m_echo2.prepare(m_sampleRate, 2.5, mixSeed(m_score.m_seed, 220));
    {
        const float sr = static_cast<float>(m_sampleRate);
        m_dcL.setQ(20.0f, 0.70710678f, sr);
        m_dcR.copyCoefficients(m_dcL);
        m_sideHp.setQ(100.0f, 0.70710678f, sr);
        m_sideHp300.setQ(300.0f, 0.70710678f, sr);
        m_dcL.reset(); m_dcR.reset(); m_sideHp.reset(); m_sideHp300.reset();
        m_energyMid = m_energyLow = m_energyHigh = 0.0f;
        m_energyCoef = 1.0f - std::exp(-1.0f / (0.3f * sr));
        m_envAttack = std::exp(-1.0f / (0.005f * sr));
        m_envRelease = std::exp(-1.0f / (0.25f * sr));
        m_duckEnv = 0.0f;
        for (Strip& st : m_strips) {
            st.m_hpL.reset(); st.m_hpR.reset(); st.m_lpL.reset(); st.m_lpR.reset();
            for (int c = 0; c < 2; ++c) {
                st.m_splitLo[c].setQ(300.0f, 0.70710678f, sr); st.m_splitLo[c].reset();
                st.m_splitHi[c].setQ(5000.0f, 0.70710678f, sr); st.m_splitHi[c].reset();
            }
        }
        m_subL.setQ(80.0f, 0.70710678f, sr); m_subR.copyCoefficients(m_subL); m_subL.reset(); m_subR.reset();
        for (int k = 0; k < 2; ++k) {
            m_subSplitL[k].setQ(80.0f, 0.70710678f, sr); m_subSplitL[k].reset();
            m_subSplitR[k].setQ(80.0f, 0.70710678f, sr); m_subSplitR[k].reset();
            m_subHpL[k].setQ(80.0f, 0.70710678f, sr); m_subHpL[k].reset();
            m_subHpR[k].setQ(80.0f, 0.70710678f, sr); m_subHpR[k].reset();
        }
        m_subEnv = 0.0f;
        m_subAttack = std::exp(-1.0f / (0.001f * sr));
        m_subRelease = std::exp(-1.0f / (0.1f * sr));
        m_clipPrev[0] = m_clipPrev[1] = 0.0f;
        m_punchFastA = std::exp(-1.0f / (0.001f * sr)); m_punchFastR = std::exp(-1.0f / (0.02f * sr));
        m_punchSlowA = std::exp(-1.0f / (0.02f * sr)); m_punchSlowR = std::exp(-1.0f / (0.2f * sr));
        for (Strip& st : m_strips) st.m_envFast = st.m_envSlow = 0.0f;
        for (int k = 0; k < 4; ++k) m_apL[k] = m_apR[k] = 0.0f;
        // The spread's all-passes turn their phase at corners spread over the band, different on each side:
        // a = (tan(pi f / fs) - 1) / (tan(pi f / fs) + 1).
        {
            static const float FL[4] = { 250.0f, 700.0f, 1800.0f, 4500.0f }, FR[4] = { 350.0f, 1000.0f, 2600.0f, 6500.0f };
            for (int k = 0; k < 4; ++k) {
                const float tl = std::tan(3.14159265f * FL[k] / sr), tr = std::tan(3.14159265f * FR[k] / sr);
                m_apCoefL[k] = (tl - 1.0f) / (tl + 1.0f);
                m_apCoefR[k] = (tr - 1.0f) / (tr + 1.0f);
            }
        }
        m_padEnv = 0.0f;
    }
    m_reverb.prepare(m_sampleRate);
    m_plate.prepare(m_sampleRate);
    m_blend.prepare(m_sampleRate);
    m_early.prepare(m_sampleRate);
    m_shimmer.prepare(m_sampleRate);
    {
        // The six bands (350 Hz .. 4.8 kHz, about three quarters of an octave apart).
        static const float Centre[Bands] = { 350.0f, 600.0f, 1000.0f, 1700.0f, 2900.0f, 4800.0f };
        const float sr = static_cast<float>(m_sampleRate);
        for (int k = 0; k < Bands; ++k) {
            for (Svf* f : { &m_rowSide[k], &m_padSide[k] }) { f->setQ(Centre[k], 1.2f, sr); f->reset(); }
            for (Strip& st : m_strips) for (int c = 0; c < 2; ++c) { st.m_band[c][k].setQ(Centre[k], 2.0f, sr); st.m_band[c][k].reset(); }
            m_rowBandEnv[k] = m_padBandEnv[k] = 0.0f;
        }
        m_tamer.prepare(m_sampleRate);
    }
    m_tape.prepare(m_sampleRate, mixSeed(m_score.m_seed, 500));
    m_tape.setSingers(m_tapeSingers);
    m_atmos.prepare(m_sampleRate, mixSeed(m_score.m_seed, 600));
    m_strings.prepare(m_sampleRate, mixSeed(m_score.m_seed, 700));
    m_poly.prepare(m_sampleRate, mixSeed(m_score.m_seed, 900));
    m_drums.prepare(m_sampleRate, mixSeed(m_score.m_seed, 800));
    m_spring.prepare(m_sampleRate);
    m_comp.prepare(m_sampleRate);
    m_limiter.prepare(m_sampleRate);
    for (Strip& s : m_strips) s.m_running = false;
    updateCell();
}

float Engine::played(int id) const
{
    const float knob = m_params.get(id);
    const int t = id >= 0 && id < static_cast<int>(m_trackOf.size()) ? m_trackOf[static_cast<size_t>(id)] : -1;
    if (t < 0) return knob;
    const float off = m_tracks[static_cast<size_t>(t)].m_offset;
    if (off == 0.0f) return knob;
    // The same knob and offset as last time give the same value: compared bit for bit.
    std::array<float, 3>& c = m_playedCache[static_cast<size_t>(id)];
    if (std::bit_cast<uint32_t>(c[0]) == std::bit_cast<uint32_t>(knob) && std::bit_cast<uint32_t>(c[1]) == std::bit_cast<uint32_t>(off))
        return c[2];
    const float norm = std::clamp(m_params.toNormalised(id, knob) + off, 0.0f, 1.0f);
    c = { knob, off, m_params.fromNormalised(id, norm) };
    return c[2];
}

float Engine::playedNow(int id) const
{
    const float knob = m_params.get(id);
    const int t = id >= 0 && id < static_cast<int>(m_trackOf.size()) ? m_trackOf[static_cast<size_t>(id)] : -1;
    if (t < 0) return knob;
    const float off = m_tracks[static_cast<size_t>(t)].m_offset;
    if (off == 0.0f) return knob;
    return m_params.fromNormalised(id, std::clamp(m_params.toNormalised(id, knob) + off, 0.0f, 1.0f));
}

VoiceSettings Engine::voiceSettings(ModuleE m, int instance, bool vibrato) const
{
    // The voice table and the lead's share their first thirteen entries (Params.h), so one reader does.
    auto v = [&](int index) { return played(m_params.id(m, instance, index)); };
    VoiceSettings s;
    s.m_wave = v(voice::WAVE);
    s.m_detuneCents = v(voice::DETUNE);
    s.m_pulseWidth = v(voice::PULSE_WIDTH);
    s.m_driftCents = v(voice::DRIFT);
    s.m_driveDb = v(voice::DRIVE);
    s.m_cutoffHz = v(voice::CUTOFF);
    // The performer's hand on the rows' filters (perform.filter), in octaves; nothing at 0.
    if (m == ModuleE::VOICE) {
        const float grab = played(m_params.id(ModuleE::PERFORM, 0, perform::FILTER));
        if (grab != 0.0f) s.m_cutoffHz *= std::exp2(grab);
        else if (std::fabs(m_jamOctaves) > 0.01f) s.m_cutoffHz *= std::exp2(m_jamOctaves);   // the family jam's energy
    }
    s.m_resonance = v(voice::RESONANCE);
    s.m_envOctaves = v(voice::ENV_AMOUNT);
    s.m_decayMs = v(voice::DECAY);
    s.m_keyTrack = v(voice::KEY_TRACK);
    s.m_accent = v(voice::ACCENT);
    s.m_releaseMs = v(voice::AMP_DECAY);
    s.m_glideMs = v(voice::GLIDE);
    if (m == ModuleE::VOICE) {
        // The rows' wavetables (the lead's and the drone's tables have other knobs from here on).
        s.m_table = static_cast<int>(std::lround(v(voice::TABLE)));
        s.m_tablePos = v(voice::TABLE_POS);
        s.m_tableMod = v(voice::TABLE_MOD);
    }
    // The filter model and its knobs (26.09.2026, Filters.h): the rows' and the lead's and drone's tables each have them.
    const bool isVoice = m == ModuleE::VOICE;
    s.m_filter = static_cast<int>(std::lround(v(isVoice ? voice::FILTER : lead::FILTER)));
    s.m_filterMode = v(isVoice ? voice::FILTER_MODE : lead::FILTER_MODE);
    s.m_filterFm = v(isVoice ? voice::FILTER_FM : lead::FILTER_FM);
    // The envelopes in full and the modulation (26.09.2026, Modulation.h): no hand moves these knobs, but the composer's
    // sounds (compose.pick_sounds) set them as offsets like every other knob.
    auto raw = [&](int vi, int li) { return played(m_params.id(m, instance, isVoice ? vi : li)); };
    auto choice = [&](int vi, int li) { return static_cast<int>(std::lround(raw(vi, li))); };
    s.m_ampAttackMs = raw(voice::AMP_ATTACK, lead::AMP_ATTACK);
    s.m_ampDecayMs = raw(voice::AMP_DECAY2, lead::AMP_DECAY2);
    s.m_ampSustain = raw(voice::AMP_SUSTAIN, lead::AMP_SUSTAIN);
    s.m_filtAttackMs = raw(voice::FILT_ATTACK, lead::FILT_ATTACK);
    s.m_filtSustain = raw(voice::FILT_SUSTAIN, lead::FILT_SUSTAIN);
    s.m_filtReleaseMs = raw(voice::FILT_RELEASE, lead::FILT_RELEASE);
    s.m_filtLink = choice(voice::FILT_LINK, lead::FILT_LINK);
    s.m_envVelocity = raw(voice::ENV_VELOCITY, lead::ENV_VELOCITY);
    s.m_mod = modSettings(m, instance, isVoice ? voice::MOD_ATTACK : lead::MOD_ATTACK, 1.0f, isVoice ? voice::LFO1_RATE : lead::LFO1_RATE,
                        Lfos, isVoice ? voice::MOD1_SRC : lead::MOD1_SRC, ModSlots, false);
    // The classic VCOs (26.09.2026, Vco.h).
    s.m_vco = choice(voice::VCO, lead::VCO);
    s.m_sync = choice(voice::SYNC, lead::SYNC);
    s.m_osc2Semis = raw(voice::OSC2_PITCH, lead::OSC2_PITCH);
    s.m_crossMod = raw(voice::CROSS_MOD, lead::CROSS_MOD);
    if (vibrato) {
        s.m_vibratoCents = v(lead::VIBRATO);
        s.m_vibratoHz = v(lead::VIBRATO_RATE);
    }
    return s;
}

ModSettings Engine::modSettings(ModuleE m, int instance, int env, float envToMs, int lfo, int lfos, int slot, int slots, bool shortMatrix) const
{
    // Every synth lays these out alike: the envelope's four times, then five knobs per LFO, then three per slot.
    auto raw = [&](int index) { return played(m_params.id(m, instance, index)); };
    auto choice = [&](int index) { return static_cast<int>(std::lround(raw(index))); };
    ModSettings s;
    if (env >= 0) {
        s.m_attackMs = raw(env) * envToMs;
        s.m_decayMs = raw(env + 1) * envToMs;
        s.m_sustain = raw(env + 2);
        s.m_releaseMs = raw(env + 3) * envToMs;
    }
    for (int l = 0; l < std::min(lfos, Lfos); ++l) {
        const int o = lfo + 5 * l;
        s.m_lfo[l] = { raw(o), choice(o + 1), choice(o + 2), choice(o + 3), raw(o + 4) };
    }
    for (int k = 0; k < std::min(slots, ModSlots); ++k) {
        const int o = slot + 3 * k;
        const int dst = choice(o + 1);
        s.m_slot[k] = { choice(o), shortMatrix ? shortModDest(dst) : dst, raw(o + 2) };
    }
    return s;
}

void Engine::setStrip(int s, float levelDb, float pan, float echo, float reverb, float width, float echo2, float distance)
{
    const float in[7] = { levelDb, pan, echo, reverb, width, echo2, distance };
    if (!changed(m_cache.m_strip[s], in, m_cache.m_valid)) return;
    Strip& st = m_strips[s];
    // The distance macro (the addon's 2): level, highs and the hall's share move together, so nothing is near in
    // its level and far in the hall. From a table at 0, 0.3, 0.6 and 1: 0, -4, -10, -24 dB; open, 10, 5, 2.5 kHz;
    // the hall send up by 0, 0.2, 0.6, 1.
    {
        static const float D[4] = { 0.0f, 0.3f, 0.6f, 1.0f }, Level[4] = { 0.0f, -4.0f, -10.0f, -24.0f };
        static const float Lp[4] = { 20000.0f, 10000.0f, 5000.0f, 2500.0f }, Send[4] = { 0.0f, 0.2f, 0.6f, 1.0f };
        const float d = std::clamp(distance, 0.0f, 1.0f);
        int k = 0;
        while (k < 2 && d > D[k + 1]) ++k;
        const float t = (d - D[k]) / (D[k + 1] - D[k]);
        levelDb += Level[k] + t * (Level[k + 1] - Level[k]);
        reverb = std::min(1.0f, reverb + Send[k] + t * (Send[k + 1] - Send[k]));
        st.m_lpHz = d > 0.001f ? Lp[k] * std::pow(Lp[k + 1] / Lp[k], t) : 0.0f;
        if (st.m_lpHz > 0.0f) {
            st.m_lpL.setQ(st.m_lpHz, 0.70710678f, static_cast<float>(m_sampleRate));
            st.m_lpR.copyCoefficients(st.m_lpL);
        }
    }
    const float level = dbToGain(levelDb);
    const float a = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * Pi;
    st.m_gainL = level * std::cos(a) * width;
    st.m_gainR = level * std::sin(a) * width;
    st.m_echo = echo;
    st.m_reverb = reverb;
    st.m_echo2 = echo2;
}

void Engine::setLowCut(int s, float hz)
{
    if (m_cache.m_valid && m_cache.m_lowCut[s] == hz) return;
    m_cache.m_lowCut[s] = hz;
    Strip& st = m_strips[s];
    st.m_lowCut = hz;
    if (hz > 0.0f) {
        st.m_hpL.setQ(hz, 0.70710678f, static_cast<float>(m_sampleRate));
        st.m_hpR.copyCoefficients(st.m_hpL);
    }
}

float Engine::trimAt(double beat) const
{
    // From a piece's start, gliding over eight bars from the one before it: a concert's pieces meet at their own levels.
    float trim = 0.0f;
    for (size_t i = 0; i < m_score.m_levels.size(); ++i) {
        const LevelMark& m = m_score.m_levels[i];
        if (m.m_beat > beat + 1e-9) break;
        const float before = i > 0 ? m_score.m_levels[i - 1].m_trimDb : m.m_trimDb;
        trim = before + (m.m_trimDb - before) * static_cast<float>(std::clamp((beat - m.m_beat) / (8.0 * BeatsPerBar), 0.0, 1.0));
    }
    return trim;
}

void Engine::setLevelTrims(const std::vector<float>& trims)
{
    const double now = beat();
    const float was = trimAt(now) + m_lateDb;
    for (size_t i = 0; i < m_score.m_levels.size() && i < trims.size(); ++i) m_score.m_levels[i].m_trimDb = trims[i];
    m_lateDb = was - trimAt(now);
}

void Engine::updateCell()
{
    // Gesture offsets at this cell's beat.
    const double beat = m_score.m_tempo.beatAt(seconds());
    // A concert's next piece brings its sounds: the composer's knob settings on the knobs, where it begins.
    if (m_knobCursor < m_score.m_knobs.size() && m_score.m_knobs[m_knobCursor].m_beat <= beat + 1e-9) applyKnobs(beat);
    auto knob = [&](ModuleE m, int index) { return played(m_params.id(m, 0, index)); };
    // Hold (perform.hold): the hands let go, every gesture stands where it is until they take the knobs again.
    const bool hold = knob(ModuleE::PERFORM, perform::HOLD) >= 0.5f;
    for (Track& t : m_tracks) {
        if (hold) break;
        // Positions only move forward, so the cursor only moves forward: the latest gesture that has
        // started is the one that counts (Score::gestureOffset), gestures.size() while none has.
        const size_t none = t.m_gestures.size();
        size_t c = t.m_cursor;
        size_t next = c == none ? 0 : c + 1;
        while (next < none && t.m_gestures[next].m_beat <= beat) c = next++;
        t.m_cursor = c;
        t.m_offset = c == none ? 0.0f : gestureValue(t.m_gestures[c], beat);
    }
    m_transpose = static_cast<int>(std::lround(knob(ModuleE::PERFORM, perform::TRANSPOSE))) + m_jamTranspose;
    // The family jam (02.10.2026): the leader's energy on the rows' filters -- open at full energy, an octave and a half
    // down at 0.3 --, gliding over half a second.
    {
        const float target = m_jamEnergy >= 0.0f ? (std::clamp(m_jamEnergy, 0.0f, 1.0f) - 1.0f) * 2.0f : 0.0f;
        m_jamOctaves += (target - m_jamOctaves) * (1.0f - std::exp(-static_cast<float>(Cell) / (0.5f * static_cast<float>(m_sampleRate))));
    }
    // The keyboard (live play only, 01.10.2026): what it plays, whether that replaces the composer's notes, whether the
    // composer plays at all.
    m_keyTarget = m_live ? m_params.getInt(m_params.id(ModuleE::PERFORM, 0, perform::KEYBOARD_PART)) : perform::keys::OFF;
    m_keyReplace = !m_live || m_params.getInt(m_params.id(ModuleE::PERFORM, 0, perform::KEYBOARD_MODE)) == 0;
    m_composerOff = m_live && !m_params.getBool(m_params.id(ModuleE::PERFORM, 0, perform::COMPOSER));

    // The modular voices: the rows (their strips on the row module), the lead and the drone; the clock for the synths'
    // synced LFOs.
    const double beatsPerSample = m_score.m_tempo.bpmAt(beat) / (60.0 * m_sampleRate);
    m_voices.setClock(beat, beatsPerSample);
    m_poly.setClock(beat, beatsPerSample);
    m_tape.setClock(beat, beatsPerSample);
    m_strings.setClock(beat, beatsPerSample);
    for (int r = 0; r < ModVoices; ++r) {
        const bool row = r < Rows;
        const ModuleE m = row ? ModuleE::VOICE : (r == SRC_LEAD ? ModuleE::LEAD : ModuleE::DRONE);
        const int inst = row ? r : 0;
        VoiceSettings vs = voiceSettings(m, inst, !row);
        if (row) {
            // The filter sweep (25.09.2026): the Berlin School's slow opening and closing of a sequence, a sine on the
            // cutoff with a period of its own per row (39 .. 79 s, irrational against the bars and each other), on
            // the piece's clock, over whatever the hands do; master.motion scales it with the other slow movements.
            static const double Period[Rows] = { 53.1, 41.7, 67.3, 38.9, 79.1, 47.9, 61.3, 71.7 };
            const float depth = played(m_params.id(ModuleE::ROW, r, row::SWEEP)) * knob(ModuleE::MASTER, master::MOTION);
            if (depth > 0.0f) vs.m_cutoffHz *= std::exp2(depth * static_cast<float>(std::sin(6.283185307179586 * seconds() / Period[r] + r)));
        }
        if (changed(m_cache.m_voice[r], vs, m_cache.m_valid)) m_voices.set(r, vs);
        m_strips[r].m_running = m_voices.active(r) || m_voices.held(r);
        auto s = [&](int rowIndex, int leadIndex) {
            return row ? played(m_params.id(ModuleE::ROW, r, rowIndex)) : played(m_params.id(m, 0, leadIndex));
        };
        float pan = s(row::PAN, lead::PAN);
        if (const float pm = m_voices.panMod(r); pm != 0.0f) pan += pm;   // the voice's matrix (Modulation.h)
        if (!row) {
            // The drone (or the lead) wanders slowly across the stereo field: lead.auto_pan, a sine of 0.05 Hz on the
            // piece's clock (the style guide's 7.3).
            const float depth = played(m_params.id(m, 0, lead::AUTO_PAN));
            if (depth > 0.0f) pan += depth * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 0.05 * seconds()));
        }
        setStrip(r, s(row::LEVEL, lead::LEVEL), pan, s(row::ECHO_SEND, lead::ECHO_SEND), s(row::REVERB_SEND, lead::REVERB_SEND), 1.0f,
                 row ? played(m_params.id(ModuleE::ROW, r, row::ECHO2_SEND)) : 0.0f, s(row::DISTANCE, lead::DISTANCE));
        setLowCut(r, s(row::LOW_CUT, lead::LOW_CUT));
        m_strips[r].m_blend = s(row::BLEND_SEND, lead::BLEND_SEND);
        m_strips[r].m_early = s(row::EARLY_SEND, lead::EARLY_SEND);
        m_strips[r].m_shimmer = row ? 0.0f : played(m_params.id(m, 0, lead::SHIMMER_SEND));
        m_strips[r].m_punch = row ? played(m_params.id(ModuleE::ROW, r, row::PUNCH)) : 0.0f;
    }

    // The slow movements (the addon's 7): micro (under 2 s, a few tenths of a dB on the pads' levels) and meso
    // (13 to 41 s: the pads' hall sends, the strings' tone, the echo's feedback, the atmosphere's level), with
    // irrational periods that never lock into the sequence's 8- and 16-bar cycles; foreground and background never
    // breathe together. On the piece's clock, so the same everywhere; master.motion scales them (0 still).
    const float motion = knob(ModuleE::MASTER, master::MOTION);
    const double clock = seconds();
    auto wave = [&](double period) { return motion * static_cast<float>(std::sin(6.283185307179586 * clock / period)); };
    // The tape keys, the string machine, the drums, the atmosphere.
    TapeSettings ts;
    ts.m_set = static_cast<TapeSetE>(std::clamp(static_cast<int>(std::lround(knob(ModuleE::TAPE, tape::SET))), 0, 2));
    ts.m_vowel = knob(ModuleE::TAPE, tape::VOWEL);
    ts.m_wowCents = knob(ModuleE::TAPE, tape::WOW);
    ts.m_flutterCents = knob(ModuleE::TAPE, tape::FLUTTER);
    ts.m_sagCents = knob(ModuleE::TAPE, tape::SAG);
    ts.m_toneHz = knob(ModuleE::TAPE, tape::TONE);
    ts.m_age = knob(ModuleE::TAPE, tape::AGE);
    {
        // The player's envelope and the keyboard's modulation (26.09.2026).
        auto raw = [&](int index) { return played(m_params.id(ModuleE::TAPE, 0, index)); };
        ts.m_swellMs = raw(tape::AMP_ATTACK);
        ts.m_decayMs = raw(tape::AMP_DECAY);
        ts.m_sustain = raw(tape::AMP_SUSTAIN);
        ts.m_releaseMs = raw(tape::AMP_RELEASE);
        ts.m_mod = modSettings(ModuleE::TAPE, 0, -1, 1.0f, tape::LFO1_RATE, 2, tape::MOD1_SRC, 4, true);
    }
    if (changed(m_cache.m_tape, ts, m_cache.m_valid)) m_tape.set(ts);
    m_strips[SRC_TAPE].m_running = m_tape.active();
    float tapePan = knob(ModuleE::TAPE, tape::PAN);
    if (const float pm = m_tape.panMod(); pm != 0.0f) tapePan += pm;
    setStrip(SRC_TAPE, knob(ModuleE::TAPE, tape::LEVEL) + 0.3f * wave(1.37) + 0.2f * wave(0.83), tapePan,
             knob(ModuleE::TAPE, tape::ECHO_SEND), std::clamp(knob(ModuleE::TAPE, tape::REVERB_SEND) + 0.08f * wave(13.7), 0.0f, 1.0f),
             1.0f, 0.0f, knob(ModuleE::TAPE, tape::DISTANCE));
    m_tapeSpread = knob(ModuleE::TAPE, tape::SPREAD);
    m_strips[SRC_TAPE].m_blend = knob(ModuleE::TAPE, tape::BLEND_SEND);
    m_strips[SRC_TAPE].m_early = knob(ModuleE::TAPE, tape::EARLY_SEND);
    m_strips[SRC_TAPE].m_shimmer = knob(ModuleE::TAPE, tape::SHIMMER_SEND);
    setLowCut(SRC_TAPE, knob(ModuleE::TAPE, tape::LOW_CUT));

    StringSettings ss;
    ss.m_attackS = knob(ModuleE::STRINGS, strings::ATTACK);
    ss.m_releaseS = knob(ModuleE::STRINGS, strings::RELEASE);
    ss.m_feet = knob(ModuleE::STRINGS, strings::FEET);
    ss.m_toneHz = knob(ModuleE::STRINGS, strings::TONE) * std::pow(2.0f, 0.5f * wave(17.3));
    ss.m_ensemble = knob(ModuleE::STRINGS, strings::ENSEMBLE);
    ss.m_registration = knob(ModuleE::STRINGS, strings::REGISTRATION);
    ss.m_animate = knob(ModuleE::STRINGS, strings::ANIMATE);
    ss.m_animateHz = knob(ModuleE::STRINGS, strings::ANIMATE_RATE);
    ss.m_ensembleType = static_cast<int>(std::lround(knob(ModuleE::STRINGS, strings::ENSEMBLE_TYPE)));
    ss.m_phaser = knob(ModuleE::STRINGS, strings::PHASER);
    ss.m_decayS = knob(ModuleE::STRINGS, strings::AMP_DECAY);
    ss.m_sustain = knob(ModuleE::STRINGS, strings::AMP_SUSTAIN);
    ss.m_mod = modSettings(ModuleE::STRINGS, 0, -1, 1.0f, strings::LFO1_RATE, 2, strings::MOD1_SRC, 4, true);
    if (changed(m_cache.m_strings, ss, m_cache.m_valid)) m_strings.set(ss);
    m_strips[SRC_STRINGS].m_running = m_strings.active();
    float stringsPan = knob(ModuleE::STRINGS, strings::PAN);
    if (const float pm = m_strings.panMod(); pm != 0.0f) stringsPan += pm;
    // The ensemble spreads the machine over both sides; the pan law's 3 dB come back with sqrt 2.
    setStrip(SRC_STRINGS, knob(ModuleE::STRINGS, strings::LEVEL) + 0.3f * wave(1.61) + 0.2f * wave(0.97), stringsPan,
             knob(ModuleE::STRINGS, strings::ECHO_SEND), std::clamp(knob(ModuleE::STRINGS, strings::REVERB_SEND) + 0.08f * wave(21.1), 0.0f, 1.0f),
             1.41421356f, 0.0f, knob(ModuleE::STRINGS, strings::DISTANCE));
    setLowCut(SRC_STRINGS, knob(ModuleE::STRINGS, strings::LOW_CUT));
    m_strips[SRC_STRINGS].m_blend = knob(ModuleE::STRINGS, strings::BLEND_SEND);
    m_strips[SRC_STRINGS].m_early = knob(ModuleE::STRINGS, strings::EARLY_SEND);
    m_strips[SRC_STRINGS].m_shimmer = knob(ModuleE::STRINGS, strings::SHIMMER_SEND);

    PolySettings ps;
    ps.m_table = static_cast<int>(std::lround(knob(ModuleE::POLY, poly::TABLE)));
    ps.m_position = knob(ModuleE::POLY, poly::POSITION);
    ps.m_scan = knob(ModuleE::POLY, poly::SCAN);
    ps.m_scanHz = knob(ModuleE::POLY, poly::SCAN_RATE);
    ps.m_detuneCents = knob(ModuleE::POLY, poly::DETUNE);
    ps.m_spread = knob(ModuleE::POLY, poly::SPREAD);
    ps.m_driftCents = knob(ModuleE::POLY, poly::DRIFT);
    ps.m_cutoffHz = knob(ModuleE::POLY, poly::CUTOFF) * std::pow(2.0f, 0.4f * wave(19.7));
    ps.m_resonance = knob(ModuleE::POLY, poly::RESONANCE);
    ps.m_envOctaves = knob(ModuleE::POLY, poly::ENV_AMOUNT);
    ps.m_attackS = knob(ModuleE::POLY, poly::ATTACK);
    ps.m_releaseS = knob(ModuleE::POLY, poly::RELEASE);
    ps.m_chorus = knob(ModuleE::POLY, poly::CHORUS);
    ps.m_filter = static_cast<int>(std::lround(knob(ModuleE::POLY, poly::FILTER)));
    ps.m_filterMode = knob(ModuleE::POLY, poly::FILTER_MODE);
    {
        // The envelopes in full and every key's modulation (26.09.2026); the pad's times in seconds.
        auto raw = [&](int index) { return played(m_params.id(ModuleE::POLY, 0, index)); };
        ps.m_ampDecayS = raw(poly::AMP_DECAY);
        ps.m_ampSustain = raw(poly::AMP_SUSTAIN);
        ps.m_filtLink = static_cast<int>(std::lround(raw(poly::FILT_LINK)));
        ps.m_filtAttackS = raw(poly::FILT_ATTACK);
        ps.m_filtDecayS = raw(poly::FILT_DECAY);
        ps.m_filtSustain = raw(poly::FILT_SUSTAIN);
        ps.m_filtReleaseS = raw(poly::FILT_RELEASE);
        ps.m_envVelocity = raw(poly::ENV_VELOCITY);
        ps.m_mod = modSettings(ModuleE::POLY, 0, poly::MOD_ATTACK, 1000.0f, poly::LFO1_RATE, Lfos, poly::MOD1_SRC, ModSlots, false);
    }
    if (changed(m_cache.m_poly, ps, m_cache.m_valid)) m_poly.set(ps);
    m_strips[SRC_POLY].m_running = m_poly.active();
    setStrip(SRC_POLY, knob(ModuleE::POLY, poly::LEVEL) + 0.3f * wave(1.37) + 0.2f * wave(1.13), knob(ModuleE::POLY, poly::PAN),
             knob(ModuleE::POLY, poly::ECHO_SEND), std::clamp(knob(ModuleE::POLY, poly::REVERB_SEND) + 0.08f * wave(23.3), 0.0f, 1.0f),
             1.41421356f, 0.0f, knob(ModuleE::POLY, poly::DISTANCE));
    setLowCut(SRC_POLY, knob(ModuleE::POLY, poly::LOW_CUT));
    m_strips[SRC_POLY].m_blend = knob(ModuleE::POLY, poly::BLEND_SEND);
    m_strips[SRC_POLY].m_early = knob(ModuleE::POLY, poly::EARLY_SEND);
    m_strips[SRC_POLY].m_shimmer = knob(ModuleE::POLY, poly::SHIMMER_SEND);

    DrumSettings ds;
    ds.m_kickHz = knob(ModuleE::DRUMS, drums::KICK_HZ);
    ds.m_decay = knob(ModuleE::DRUMS, drums::DECAY);
    ds.m_tone = knob(ModuleE::DRUMS, drums::TONE);
    if (changed(m_cache.m_drums, ds, m_cache.m_valid)) m_drums.set(ds);
    {
        // The kit pans its instruments itself (Drums.h): the strip only sets the level.
        Strip& d = m_strips[SRC_DRUMS];
        d.m_running = m_drums.active();
        d.m_gainL = d.m_gainR = dbToGain(knob(ModuleE::DRUMS, drums::LEVEL));
        d.m_echo = knob(ModuleE::DRUMS, drums::ECHO_SEND);
        d.m_reverb = knob(ModuleE::DRUMS, drums::REVERB_SEND);
    }
    setLowCut(SRC_DRUMS, knob(ModuleE::DRUMS, drums::LOW_CUT));
    m_strips[SRC_DRUMS].m_blend = knob(ModuleE::DRUMS, drums::BLEND_SEND);
    m_strips[SRC_DRUMS].m_early = knob(ModuleE::DRUMS, drums::EARLY_SEND);
    m_strips[SRC_ATMOS].m_shimmer = knob(ModuleE::ATMOS, atmos::SHIMMER_SEND);
    setLowCut(SRC_ATMOS, knob(ModuleE::ATMOS, atmos::LOW_CUT));

    auto gain = [](float db) { return db <= -59.9f ? 0.0f : dbToGain(db); };
    AtmosSettings as;
    as.m_windGain = gain(knob(ModuleE::ATMOS, atmos::WIND));
    as.m_windHz = knob(ModuleE::ATMOS, atmos::WIND_TONE);
    as.m_sweepsPerMinute = knob(ModuleE::ATMOS, atmos::SWEEPS);
    as.m_sweepGain = gain(knob(ModuleE::ATMOS, atmos::SWEEP_LEVEL));
    as.m_bleepsPerMinute = knob(ModuleE::ATMOS, atmos::BLEEPS);
    as.m_bleepGain = gain(knob(ModuleE::ATMOS, atmos::BLEEP_LEVEL));
    as.m_grainGain = gain(knob(ModuleE::ATMOS, atmos::GRAINS));
    as.m_grainsPerSecond = knob(ModuleE::ATMOS, atmos::GRAIN_DENSITY);
    // The bleeps take their notes from the root the rows are in.
    as.m_rootPc = pitchClass(m_score.m_keyRoot + m_score.rootAt(beat));
    as.m_scale = m_params.getInt(m_params.id(ModuleE::COMPOSE, 0, compose::SCALE));
    if (changed(m_cache.m_atmos, as, m_cache.m_valid)) m_atmos.set(as);
    {
        // Stereo already: the strip only sets the level; the echo takes the bleeps (renderSpan).
        Strip& at = m_strips[SRC_ATMOS];
        at.m_running = m_atmos.active();
        at.m_gainL = at.m_gainR = dbToGain(knob(ModuleE::ATMOS, atmos::LEVEL) + 3.0f * wave(41.3));   // a far layer swells by 6 dB
        at.m_echo = knob(ModuleE::ATMOS, atmos::ECHO_SEND);
        at.m_reverb = knob(ModuleE::ATMOS, atmos::REVERB_SEND);
    }

    // The echo throw (perform.throw): every send up, and the echo's feedback with them (below).
    const float throwAmount = knob(ModuleE::PERFORM, perform::THROW);
    m_echoThrow = 0.8f * throwAmount;

    // The rooms and the master.
    EchoSettings es;
    const double bpm = m_score.m_tempo.bpmAt(beat);
    es.m_delaySeconds = echoTimeBeats(static_cast<EchoTimeE>(static_cast<int>(knob(ModuleE::ECHO, echo::TIME)))) * 60.0 / bpm;
    es.m_feedback = knob(ModuleE::ECHO, echo::FEEDBACK) + 0.04f * wave(29.9);
    if (throwAmount > 0.0f) es.m_feedback += (0.9f - es.m_feedback) * throwAmount;
    es.m_toneHz = knob(ModuleE::ECHO, echo::TONE);
    es.m_wowMs = knob(ModuleE::ECHO, echo::WOW);
    es.m_flutterMs = knob(ModuleE::ECHO, echo::FLUTTER);
    es.m_driveDb = knob(ModuleE::ECHO, echo::DRIVE);
    es.m_pingPong = knob(ModuleE::ECHO, echo::PING_PONG) >= 0.5f;
    es.m_lowCutHz = knob(ModuleE::ECHO, echo::LOW_CUT);
    m_bbdOn = knob(ModuleE::ECHO, echo::TYPE) >= 0.5f;
    const EchoSettings& le = m_cache.m_echo;
    if (!m_cache.m_valid || m_cache.m_echoType != static_cast<int>(m_bbdOn) || le.m_delaySeconds != es.m_delaySeconds || le.m_feedback != es.m_feedback
        || le.m_toneHz != es.m_toneHz || le.m_wowMs != es.m_wowMs || le.m_flutterMs != es.m_flutterMs || le.m_driveDb != es.m_driveDb || le.m_pingPong != es.m_pingPong
        || le.m_lowCutHz != es.m_lowCutHz) {
        m_cache.m_echo = es;
        m_cache.m_echoType = static_cast<int>(m_bbdOn);
        if (m_bbdOn) m_bbd.set(es); else m_echo.set(es);
    }
    m_echoReturn = dbToGain(knob(ModuleE::ECHO, echo::RETURN));
    // The second echo: clean, a little wow, its own time and return.
    EchoSettings e2;
    e2.m_delaySeconds = echoTimeBeats(static_cast<EchoTimeE>(static_cast<int>(knob(ModuleE::ECHO2, echo2::TIME)))) * 60.0 / bpm;
    e2.m_feedback = knob(ModuleE::ECHO2, echo2::FEEDBACK);
    e2.m_toneHz = knob(ModuleE::ECHO2, echo2::TONE);
    e2.m_wowMs = 0.1f;      // clean: no timing smear on the counter rows (the production guide's 8.1)
    e2.m_flutterMs = 0.01f;
    e2.m_driveDb = 2.0f;
    e2.m_pingPong = knob(ModuleE::ECHO2, echo2::PING_PONG) >= 0.5f;
    e2.m_lowCutHz = knob(ModuleE::ECHO2, echo2::LOW_CUT);
    const EchoSettings& l2 = m_cache.m_echo2;
    if (!m_cache.m_valid || l2.m_delaySeconds != e2.m_delaySeconds || l2.m_feedback != e2.m_feedback || l2.m_toneHz != e2.m_toneHz
        || l2.m_pingPong != e2.m_pingPong || l2.m_lowCutHz != e2.m_lowCutHz) {
        m_cache.m_echo2 = e2;
        m_echo2.set(e2);
    }
    m_echo2Return = dbToGain(knob(ModuleE::ECHO2, echo2::RETURN));
    m_echoDuckDb = knob(ModuleE::ECHO, echo::DUCK);
    m_hallDuckDb = knob(ModuleE::REVERB, reverb::DUCK);
    m_width = dbToGain(knob(ModuleE::MASTER, master::WIDTH));
    m_mono = knob(ModuleE::MASTER, master::MONO) >= 0.5f;
    m_subSolo = knob(ModuleE::MASTER, master::SUB_SOLO) >= 0.5f;
    const float cascade = knob(ModuleE::MASTER, master::CASCADE);
    if (cascade != m_cascadeDb || !m_cache.m_valid) {
        // The duck's gains from its amount, once per change: 256 steps are finer than anyone hears (at most 0.02 dB).
        m_cascadeDb = cascade;
        for (int k = 0; k < 256; ++k) m_duckTable[k] = dbToGain(-m_cascadeDb * static_cast<float>(k) / 255.0f);
    }
    m_plateOn = knob(ModuleE::REVERB, reverb::TYPE) >= 0.5f;
    const float hall[7] = { knob(ModuleE::REVERB, reverb::SIZE), knob(ModuleE::REVERB, reverb::DECAY), knob(ModuleE::REVERB, reverb::DAMPING),
                            knob(ModuleE::REVERB, reverb::PRE_DELAY) * 0.001f * static_cast<float>(m_sampleRate),
                            knob(ModuleE::REVERB, reverb::LOW_CUT), knob(ModuleE::REVERB, reverb::HIGH_CUT), m_plateOn ? 1.0f : 0.0f };
    if (changed(m_cache.m_reverb, hall, m_cache.m_valid)) {
        if (m_plateOn) m_plate.set(hall[1], hall[2], hall[3], hall[4], hall[5]);
        else m_reverb.set(hall[0], hall[1], hall[2], hall[3], hall[4], hall[5]);
    }
    m_reverbReturn = dbToGain(knob(ModuleE::REVERB, reverb::RETURN));
    const float blendSet[5] = { knob(ModuleE::BLEND, blend::DECAY), knob(ModuleE::BLEND, blend::DAMPING),
                                knob(ModuleE::BLEND, blend::PRE_DELAY) * 0.001f * static_cast<float>(m_sampleRate),
                                knob(ModuleE::BLEND, blend::LOW_CUT), knob(ModuleE::BLEND, blend::HIGH_CUT) };
    if (changed(m_cache.m_blend, blendSet, m_cache.m_valid)) m_blend.set(blendSet[0], blendSet[1], blendSet[2], blendSet[3], blendSet[4]);
    m_blendReturn = dbToGain(knob(ModuleE::BLEND, blend::RETURN));
    m_blendIntoHall = knob(ModuleE::BLEND, blend::INTO_HALL);
    const float earlySet[3] = { knob(ModuleE::EARLY, early::SIZE), knob(ModuleE::EARLY, early::LOW_CUT), knob(ModuleE::EARLY, early::HIGH_CUT) };
    if (changed(m_cache.m_early, earlySet, m_cache.m_valid)) m_early.set(earlySet[0], earlySet[1], earlySet[2]);
    m_earlyReturn = dbToGain(knob(ModuleE::EARLY, early::RETURN));
    const float shimSet[4] = { knob(ModuleE::SHIMMER, shimmer::DECAY), knob(ModuleE::SHIMMER, shimmer::AMOUNT),
                               knob(ModuleE::SHIMMER, shimmer::LOW_CUT), knob(ModuleE::SHIMMER, shimmer::HIGH_CUT) };
    if (changed(m_cache.m_shimmer, shimSet, m_cache.m_valid)) m_shimmer.set(shimSet[0], shimSet[1], shimSet[2], shimSet[3]);
    m_shimmerReturn = dbToGain(knob(ModuleE::SHIMMER, shimmer::RETURN));
    m_tame = knob(ModuleE::MASTER, master::TAME);
    m_clipDb = knob(ModuleE::MASTER, master::CLIP);
    m_clipCeiling = dbToGain(knob(ModuleE::MASTER, master::CEILING));
    m_subCeiling = dbToGain(knob(ModuleE::MASTER, master::SUB_CEILING));
    const float springs[2] = { knob(ModuleE::SPRING, spring::DECAY), knob(ModuleE::SPRING, spring::TONE) };
    if (changed(m_cache.m_spring, springs, m_cache.m_valid)) m_spring.set(springs[0], springs[1]);
    m_springReturn = gain(knob(ModuleE::SPRING, spring::RETURN));
    // The piece's loudness correction (Score::levels, Leveler.h), and what is left of one that came late.
    const float trim = trimAt(beat) + m_lateDb;
    if (m_lateDb != 0.0f) {
        m_lateDb *= static_cast<float>(std::exp(-Cell / (1.5 * m_sampleRate)));
        if (std::fabs(m_lateDb) < 1.0e-3f) m_lateDb = 0.0f;
    }
    m_master = dbToGain(knob(ModuleE::MASTER, master::LEVEL) + trim);
    const float compress = knob(ModuleE::MASTER, master::COMPRESS), ceiling = knob(ModuleE::MASTER, master::CEILING);
    if (changed(m_cache.m_compress, compress, m_cache.m_valid)) m_comp.set(-16.0f, 1.0f + 0.5f * compress, 6.0f, 30.0f, 300.0f);
    if (changed(m_cache.m_ceiling, ceiling, m_cache.m_valid)) m_limiter.set(ceiling, 150.0f);
    m_cache.m_valid = true;
}

const char* Engine::channelName(int c)
{
    static_assert(Channels == SOURCES, "a meter per source");
    static const char* const names[Channels] = { "Row 1", "Row 2", "Row 3", "Row 4", "Row 5", "Row 6", "Row 7", "Row 8",
                                                  "Lead", "Drone", "Tape Keys", "Strings", "Poly", "Drums", "Atmosphere" };
    return c >= 0 && c < Channels ? names[c] : "";
}

int Engine::takeMeters(float* peak, double* sumSq)
{
    for (int c = 0; c < Channels; ++c) {
        peak[c] = m_meterPeak[c];
        sumSq[c] = m_meterSum[c];
        m_meterPeak[c] = 0.0f;
        m_meterSum[c] = 0.0;
    }
    const int n = m_meterCount;
    m_meterCount = 0;
    return n;
}

void Engine::mix(int source, const float* xl, const float* xr, int n, const Buses& b, bool echoSend, const float* midGain)
{
    Strip& strip = m_strips[source];
    const bool mono = xl == xr;
    // The strip's low cut first (the production guide's 4.2): every role keeps out of the bands below its own.
    float fl[Cell], fr[Cell];
    if (strip.m_lowCut > 0.0f) {
        float lo, bo, hp;
        for (int i = 0; i < n; ++i) { strip.m_hpL.tick(xl[i], lo, bo, hp); fl[i] = hp; }
        if (xr == xl) std::copy(fl, fl + n, fr);
        else for (int i = 0; i < n; ++i) { strip.m_hpR.tick(xr[i], lo, bo, hp); fr[i] = hp; }
        xl = fl;
        xr = fr;
    }
    // The punch (the production guide's 7.5): a fast and a slow follower; where the fast runs ahead -- an attack --
    // the gain rises by up to 6 dB, the sustain stays. Rows only, and only where it is set.
    if (strip.m_punch > 0.0f) {
        const float k = strip.m_punch;
        for (int i = 0; i < n; ++i) {
            const float x = std::max(std::fabs(xl[i]), std::fabs(xr[i]));
            strip.m_envFast = x + (x > strip.m_envFast ? m_punchFastA : m_punchFastR) * (strip.m_envFast - x);
            strip.m_envSlow = x + (x > strip.m_envSlow ? m_punchSlowA : m_punchSlowR) * (strip.m_envSlow - x);
            const float ratio = strip.m_envSlow > 1e-6f ? strip.m_envFast / strip.m_envSlow : 1.0f;
            const float g = std::clamp(1.0f + k * (ratio - 1.0f), 1.0f, 2.0f);
            fl[i] = xl[i] * g;
            fr[i] = xr[i] * g;
        }
        xl = fl;
        xr = fr;
    }
    // The distance's high loss (the addon's 2).
    if (strip.m_lpHz > 0.0f) {
        for (int i = 0; i < n; ++i) fl[i] = strip.m_lpL.lp(xl[i]);
        if (mono) std::copy(fl, fl + n, fr);
        else for (int i = 0; i < n; ++i) fr[i] = strip.m_lpR.lp(xr[i]);
        xl = fl;
        xr = fr;
    }
    // The cascaded, spectral duck (the addon's 4): six bands between 350 Hz and 4.8 kHz, each stepping back by its own
    // gain (midGain holds Bands rows of Cell): only where the side chain has energy; the body and the air stay.
    if (midGain != nullptr) {
        float* out[2] = { fl, fr };
        const float* in[2] = { xl, xr };
        for (int c = 0; c < (mono ? 1 : 2); ++c) {
            for (int i = 0; i < n; ++i) {
                const float x = in[c][i];
                float y = x;
                for (int k = 0; k < Bands; ++k) {
                    float lo, bp, hp;
                    strip.m_band[c][k].tick(x, lo, bp, hp);
                    y += (midGain[k * Cell + i] - 1.0f) * bp * strip.m_band[c][k].m_k;
                }
                out[c][i] = y;
            }
        }
        if (mono) std::copy(fl, fl + n, fr);
        xl = fl;
        xr = fr;
    }
    const float echo = strip.m_echo + m_echoThrow;   // the throw on top of the send (exactly the send without it)
    for (int i = 0; i < n; ++i) {
        const float l = xl[i] * strip.m_gainL, r = xr[i] * strip.m_gainR;
        // The rows go onto their own bus (the resonance suppressor works on it, renderSpan); the rest into the mix.
        if (source >= Rows) { b.m_L[i] += l; b.m_R[i] += r; }
        if (echoSend) { b.m_echoL[i] += l * echo; b.m_echoR[i] += r * echo; }
        b.m_hallL[i] += l * strip.m_reverb;
        b.m_hallR[i] += r * strip.m_reverb;
        b.m_echo2L[i] += l * strip.m_echo2;
        b.m_echo2R[i] += r * strip.m_echo2;
        if (source < Rows) { b.m_rowsL[i] += l; b.m_rowsR[i] += r; }
        if (source == SRC_STRINGS || source == SRC_TAPE || source == SRC_POLY) { b.m_padsL[i] += l; b.m_padsR[i] += r; }
        b.m_blendL[i] += l * strip.m_blend;
        b.m_blendR[i] += r * strip.m_blend;
        b.m_earlyL[i] += l * strip.m_early;
        b.m_earlyR[i] += r * strip.m_early;
        b.m_shimL[i] += l * strip.m_shimmer;
        b.m_shimR[i] += r * strip.m_shimmer;
    }
    if (m_metering) {
        // What the strip put into the mix, read again: the sums above are not touched.
        float peak = m_meterPeak[source];
        double sum = m_meterSum[source];
        for (int i = 0; i < n; ++i) {
            const float l = xl[i] * strip.m_gainL, r = xr[i] * strip.m_gainR;
            peak = std::max(peak, std::max(std::fabs(l), std::fabs(r)));
            sum += 0.5 * (static_cast<double>(l) * l + static_cast<double>(r) * r);
        }
        m_meterPeak[source] = peak;
        m_meterSum[source] = sum;
    }
    if (m_stemL != nullptr) {
        float* sl = m_stemL[source] + m_spanAt;
        float* sr = m_stemR[source] + m_spanAt;
        for (int i = 0; i < n; ++i) { sl[i] = xl[i] * strip.m_gainL; sr[i] = xr[i] * strip.m_gainR; }
    }
}

void Engine::renderSpan(float* L, float* R, int n)
{
    float bufL[Cell], bufR[Cell], echoL[Cell], echoR[Cell], hallInL[Cell], hallInR[Cell], echo2L[Cell], echo2R[Cell];
    float rowsL[Cell] = {}, rowsR[Cell] = {}, padsL[Cell] = {}, padsR[Cell] = {}, blendInL[Cell] = {}, blendInR[Cell] = {};
    float earlyInL[Cell] = {}, earlyInR[Cell] = {}, shimInL[Cell] = {}, shimInR[Cell] = {};
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
    if (m_metering) m_meterCount += n;

    // The sources, each through its strip, in a fixed order (the order of the sums is part of the result).
    bool run[ModVoices];
    for (int r = 0; r < ModVoices; ++r) run[r] = m_strips[r].m_running;
    m_voices.process(run, n);
    for (int r = 0; r < ModVoices; ++r)
        if (run[r]) mix(r, m_voices.output(r), m_voices.output(r), n, b);
    // The rows' envelope, sample by sample (so the result does not depend on how a block is cut into spans): it ducks
    // the rooms' returns (the production guide's 5.2, 5.4) and, in their middle band, the pads (the addon's 4). From
    // -30 dBFS on, fully at -12.
    // The rows' bus through the resonance suppressor (the production guide's 4.4, 8.2): six bands, each one's follower
    // (3 ms up, 60 ms down) against its neighbours'; a band more than 4.5 dB over them is pulled back 3:1 (master.tame scales
    // the ratio), so a filter's resonance that sweeps through does not stick out; then into the mix.
    {
        float tl[Cell], tr[Cell];
        std::copy(rowsL, rowsL + n, tl);
        std::copy(rowsR, rowsR + n, tr);
        m_tamer.process(tl, tr, n, m_tame);
        for (int i = 0; i < n; ++i) { L[i] += tl[i]; R[i] += tr[i]; }
    }
    float duckAmount[Cell], rowGain[Bands * Cell], padGain[Bands * Cell];
    for (int i = 0; i < n; ++i) {
        const float x = std::max(std::fabs(rowsL[i]), std::fabs(rowsR[i]));
        m_duckEnv = x + (x > m_duckEnv ? m_envAttack : m_envRelease) * (m_duckEnv - x);
        duckAmount[i] = std::clamp((m_duckEnv - 0.0316f) / (0.2512f - 0.0316f), 0.0f, 1.0f);
        // The side chain in the six bands: each band of the pads steps back where the rows have energy in it.
        const float mono = 0.5f * (rowsL[i] + rowsR[i]);
        for (int k = 0; k < Bands; ++k) {
            float lo, bp, hp;
            m_rowSide[k].tick(mono, lo, bp, hp);
            const float bx = std::fabs(bp * m_rowSide[k].m_k);
            m_rowBandEnv[k] = bx + (bx > m_rowBandEnv[k] ? m_envAttack : m_envRelease) * (m_rowBandEnv[k] - bx);
            rowGain[k * Cell + i] = m_duckTable[static_cast<int>(255.0f * std::clamp((m_rowBandEnv[k] - 0.01f) / (0.08f - 0.01f), 0.0f, 1.0f))];
        }
    }
    if (m_strips[SRC_DRUMS].m_running) {
        m_drums.process(bufL, bufR, n);
        mix(SRC_DRUMS, bufL, bufR, n, b);
    }
    if (m_strips[SRC_STRINGS].m_running) {
        m_strings.process(bufL, bufR, n);
        mix(SRC_STRINGS, bufL, bufR, n, b, true, rowGain);
    }
    if (m_strips[SRC_POLY].m_running) {
        m_poly.process(bufL, bufR, n);
        mix(SRC_POLY, bufL, bufR, n, b, true, rowGain);
    }
    if (m_strips[SRC_TAPE].m_running) {
        m_tape.process(bufL, n);
        if (m_tapeSpread > 0.0f) {
            // The mono keyboard widened by allpass decorrelation (the addon's 6): the side is the direct sound through
            // a chain of eight first-order all-passes whose phase turns at corners spread over the band, so left and
            // right differ in phase, not in colour. Their sum is the direct sound alone: nothing changes in mono.
            // The correlation is about (1 - g^2) / (1 + g^2): 0.34 at the default 0.7, never under 0.
            const float g = std::clamp(m_tapeSpread, 0.0f, 1.0f), norm = 1.0f / std::sqrt(1.0f + g * g);
            for (int i = 0; i < n; ++i) {
                float a = bufL[i];
                for (int s = 0; s < 4; ++s) {
                    const float ya = m_apCoefL[s] * a + m_apL[s]; m_apL[s] = a - m_apCoefL[s] * ya; a = ya;
                    const float yb = m_apCoefR[s] * a + m_apR[s]; m_apR[s] = a - m_apCoefR[s] * yb; a = yb;
                }
                bufR[i] = norm * (bufL[i] - g * a);
                bufL[i] = norm * (bufL[i] + g * a);
            }
            mix(SRC_TAPE, bufL, bufR, n, b, true, rowGain);
        } else {
            mix(SRC_TAPE, bufL, bufL, n, b, true, rowGain);
        }
    }
    // The pads' envelope ducks the atmosphere's middle band in turn.
    for (int i = 0; i < n; ++i) {
        const float x = std::max(std::fabs(padsL[i]), std::fabs(padsR[i]));
        m_padEnv = x + (x > m_padEnv ? m_envAttack : m_envRelease) * (m_padEnv - x);
        const float mono = 0.5f * (padsL[i] + padsR[i]);
        for (int k = 0; k < Bands; ++k) {
            float lo, bp, hp;
            m_padSide[k].tick(mono, lo, bp, hp);
            const float y = std::fabs(bp * m_padSide[k].m_k);
            m_padBandEnv[k] = y + (y > m_padBandEnv[k] ? m_envAttack : m_envRelease) * (m_padBandEnv[k] - y);
            padGain[k * Cell + i] = m_duckTable[static_cast<int>(255.0f * std::clamp((m_padBandEnv[k] - 0.01f) / (0.08f - 0.01f), 0.0f, 1.0f))];
        }
    }
    if (m_strips[SRC_ATMOS].m_running) {
        // The whole atmosphere goes to the mix and the hall, only its bleeps to the echo.
        float bleepL[Cell] = {}, bleepR[Cell] = {};
        std::fill(bufL, bufL + n, 0.0f);
        std::fill(bufR, bufR + n, 0.0f);
        m_atmos.process(bufL, bufR, bleepL, bleepR, n);
        const Strip& at = m_strips[SRC_ATMOS];
        mix(SRC_ATMOS, bufL, bufR, n, b, false, padGain);
        for (int i = 0; i < n; ++i) {
            echoL[i] += bleepL[i] * at.m_gainL * (0.5f + at.m_echo + m_echoThrow);
            echoR[i] += bleepR[i] * at.m_gainR * (0.5f + at.m_echo + m_echoThrow);
        }
    }

    // The tape echo, its springs, the hall, the master.
    float wetL[Cell] = {}, wetR[Cell] = {}, sprL[Cell] = {}, sprR[Cell] = {}, hallL[Cell], hallR[Cell];
    if (m_bbdOn) m_bbd.process(echoL, echoR, wetL, wetR, n);
    else m_echo.process(echoL, echoR, wetL, wetR, n);
    // The springs take the echo's send, as in a tape echo with springs built in; they return beside it.
    m_spring.process(echoL, echoR, sprL, sprR, n);
    for (int i = 0; i < n; ++i) {
        wetL[i] += sprL[i] * m_springReturn / std::max(m_echoReturn, 1e-6f);
        wetR[i] += sprR[i] * m_springReturn / std::max(m_echoReturn, 1e-6f);
    }
    // The rooms' returns ducked by the rows (duckAmount above): the tails grow in the gaps, step back under the notes.
    const float echoK = -m_echoDuckDb * 0.11512925f, hallK = -m_hallDuckDb * 0.11512925f;   // ln 10 / 20
    // The second echo, beside the first.
    float wet2L[Cell] = {}, wet2R[Cell] = {};
    m_echo2.process(echo2L, echo2R, wet2L, wet2R, n);
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
        hallInL[i] += wetL[i] * m_echoReturn * 0.5f + wet2L[i] * m_echo2Return * 0.5f;
        hallInR[i] += wetR[i] * m_echoReturn * 0.5f + wet2R[i] * m_echo2Return * 0.5f;
    }
    // The blend room (the addon's serial far space): a short plate; a share of its return goes on into the hall, so the
    // far room sounds like the near one going on, not like a second place.
    float blendL[Cell] = {}, blendR[Cell] = {}, earlyL[Cell] = {}, earlyR[Cell] = {}, shimL[Cell] = {}, shimR[Cell] = {};
    m_blend.process(blendInL, blendInR, blendL, blendR, n);
    m_early.process(earlyInL, earlyInR, earlyL, earlyR, n);
    m_shimmer.process(shimInL, shimInR, shimL, shimR, n);
    for (int i = 0; i < n; ++i) {
        hallInL[i] += blendL[i] * m_blendReturn * m_blendIntoHall;
        hallInR[i] += blendR[i] * m_blendReturn * m_blendIntoHall;
    }
    if (m_plateOn) {
        std::fill(hallL, hallL + n, 0.0f);
        std::fill(hallR, hallR + n, 0.0f);
        m_plate.process(hallInL, hallInR, hallL, hallR, n);
    } else {
        m_reverb.process(hallInL, hallInR, hallL, hallR, n);
    }
    for (int i = 0; i < n; ++i) {
        const float eg = std::exp(echoK * duckAmount[i]), hg = std::exp(hallK * duckAmount[i]);
        const float rl = (wetL[i] * m_echoReturn + wet2L[i] * m_echo2Return) * eg
                       + (hallL[i] * m_reverbReturn + blendL[i] * m_blendReturn + earlyL[i] * m_earlyReturn) * hg + shimL[i] * m_shimmerReturn;
        const float rr = (wetR[i] * m_echoReturn + wet2R[i] * m_echo2Return) * eg
                       + (hallR[i] * m_reverbReturn + blendR[i] * m_blendReturn + earlyR[i] * m_earlyReturn) * hg + shimR[i] * m_shimmerReturn;
        if (m_stemL != nullptr) {
            m_stemL[Channels][m_spanAt + i] = rl;
            m_stemR[Channels][m_spanAt + i] = rr;
        }
        L[i] = (L[i] + rl) * m_master;
        R[i] = (R[i] + rr) * m_master;
    }
    // The mix bus (the production guide's 3.3, 6.3, 6.4): a 20 Hz high pass for DC and subsonics; the side through a
    // 12 dB/octave high pass at 100 Hz (mono beneath), plus the widening by master.width on what lies above 300 Hz --
    // both Butterworth, so no bass leaks into the widening.
    for (int i = 0; i < n; ++i) {
        float lo, bo, hl, hr;
        m_dcL.tick(L[i], lo, bo, hl);
        m_dcR.tick(R[i], lo, bo, hr);
        const float m = 0.5f * (hl + hr), s = 0.5f * (hl - hr);
        float sl, sb, sh, l3, b3, high;
        m_sideHp.tick(s, sl, sb, sh);
        m_sideHp300.tick(s, l3, b3, high);
        // The side never over the mid (6.5): the widening stops where the side's energy would come within 3 dB of
        // the mid's (energies over about 300 ms), so decorrelated spaces are not pushed into negative correlation.
        m_energyMid += m_energyCoef * (m * m - m_energyMid);
        m_energyLow += m_energyCoef * (sh * sh - m_energyLow);
        m_energyHigh += m_energyCoef * (high * high - m_energyHigh);
        float g = m_width;
        if (g > 1.0f) {
            const float room = 0.5f * m_energyMid - m_energyLow;
            const float most = room > 0.0f && m_energyHigh > 1e-12f ? std::sqrt(1.0f + room / m_energyHigh) : 1.0f;
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
        m_subHpL[0].tick(L[i], lo, bp, hl[0]); m_subHpL[1].tick(hl[0], lo, bp, hl[1]);
        m_subHpR[0].tick(R[i], lo, bp, hr[0]); m_subHpR[1].tick(hr[0], lo, bp, hr[1]);
        const float lowL = m_subSplitL[1].lp(m_subSplitL[0].lp(L[i])), lowR = m_subSplitR[1].lp(m_subSplitR[0].lp(R[i]));
        const float x = std::max(std::fabs(lowL), std::fabs(lowR));
        m_subEnv = x + (x > m_subEnv ? m_subAttack : m_subRelease) * (m_subEnv - x);
        const float g = m_subEnv > m_subCeiling ? m_subCeiling / m_subEnv : 1.0f;
        L[i] = hl[1] + g * lowL;
        R[i] = hr[1] + g * lowR;
        (void)hp;
    }
    m_comp.process(L, R, n);
    // The soft clipper (the production guide's 7.4): peaks above the limiter's ceiling are rounded into at most m_clipDb
    // above it, so short ratchet and percussion peaks do not make the limiter work; first-order antiderivative
    // anti-aliasing (the difference quotient of the curve's integral) keeps the harmonics it makes from folding back.
    if (m_clipDb > 0.01f) {
        const float t = m_clipCeiling;
        const float kk = t * (dbToGain(m_clipDb) - 1.0f);
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
            float prev = m_clipPrev[c];
            for (int i = 0; i < n; ++i) {
                const float x = ch[c][i];
                const float d = x - prev;
                // Under the ceiling on both sides the sample passes untouched (no averaging of the whole mix).
                const float y = std::fabs(x) <= t && std::fabs(prev) <= t ? x
                              : (std::fabs(d) > 1e-5f ? (integral(x) - integral(prev)) / d : curve(0.5f * (x + prev)));
                prev = x;
                ch[c][i] = y;
            }
            m_clipPrev[c] = prev;
        }
    }
    if (m_limiterOn) m_limiter.process(L, R, n);
    // Listening: the sub alone (an 80 Hz low pass, the addon's 5), and mono.
    if (m_subSolo)
        for (int i = 0; i < n; ++i) { L[i] = m_subL.lp(L[i]); R[i] = m_subR.lp(R[i]); }
    if (m_mono)
        for (int i = 0; i < n; ++i) L[i] = R[i] = 0.5f * (L[i] + R[i]);
}

void Engine::applyKnobs(double beat)
{
    bool any = false;
    while (m_knobCursor < m_score.m_knobs.size() && m_score.m_knobs[m_knobCursor].m_beat <= beat + 1e-9) {
        const KnobSet& k = m_score.m_knobs[m_knobCursor++];
        if (k.m_param >= 0 && k.m_param < m_params.count()) m_params.set(k.m_param, k.m_value);
        m_soundGroup = k.m_beat;
        any = true;
    }
    if (any) m_soundsVersion.fetch_add(1, std::memory_order_relaxed);
}

void Engine::seek(double beat)
{
    // A jump into another piece (a concert) takes that piece's sounds, as playing into it would have; a jump inside the
    // piece leaves the knobs as the player has them.
    {
        double group = -1.0;
        for (const KnobSet& k : m_score.m_knobs) if (k.m_beat <= beat + 1e-9) group = k.m_beat;
        if (group != m_soundGroup) {
            m_knobCursor = 0;
            applyKnobs(beat);
        }
        m_knobCursor = static_cast<size_t>(std::upper_bound(m_score.m_knobs.begin(), m_score.m_knobs.end(), beat + 1e-9,
            [](double b, const KnobSet& k) { return b < k.m_beat; }) - m_score.m_knobs.begin());
    }
    m_sample = std::llround(m_score.m_tempo.secondsAt(std::max(0.0, beat)) * m_sampleRate);
    m_evCursor = static_cast<size_t>(std::lower_bound(m_events.begin(), m_events.end(), m_sample,
        [](const Ev& e, int64_t s) { return e.m_sample < s; }) - m_events.begin());
    m_voices.reset();
    for (Strip& s : m_strips) s.m_running = false;
    m_tape.reset();
    m_strings.silence();
    m_poly.silence();
    m_drums.prepare(m_sampleRate, mixSeed(m_score.m_seed, 800));
    m_cache.m_valid = false;   // the components were reset: every setter runs again
    for (Track& t : m_tracks) t.m_cursor = t.m_gestures.size();
    m_cellDirty = true;
    // The notes that sound on across the jump start again where it lands (26.09.2026): the drone held since the phase
    // began, the pads, a long note of the lead -- as a player would find the keys still down. The drums' hits are gone.
    updateCell();
    m_cellDirty = false;
    for (size_t i = 0; i < m_evCursor; ++i) {
        const Ev& e = m_events[i];
        if (e.m_on && e.m_source != SRC_DRUMS && m_offAt[static_cast<size_t>(e.m_id)] > m_sample) dispatch(e);
    }
}

void Engine::dispatch(const Ev& e)
{
    if (e.m_on && !m_liveEvent && silenced(e.m_source)) return;   // the keyboard's or nobody's (01.10.2026)
    if (e.m_on && !m_liveEvent && m_jamDrumsOut && e.m_source == SRC_DRUMS) return;   // the family jam: the leader's break
    // MIDI out (02.10.2026): the composer's notes as they are played, transposed as they are heard; an off goes out
    // with the pitch its on had, and only when that on went out.
    if (m_noteTap != nullptr && !m_liveEvent && static_cast<size_t>(e.m_id) < m_tapPitch.size()) {
        uint8_t& sent = m_tapPitch[static_cast<size_t>(e.m_id)];
        if (e.m_on) {
            sent = static_cast<uint8_t>(e.m_source == SRC_DRUMS ? e.m_pitch : std::clamp(e.m_pitch + m_transpose, 0, 127));
            m_noteTap->add(e.m_sample, static_cast<int>(partOf(e.m_source)), sent, e.m_velocity, true, -1);
        } else if (sent != 0xFF) {
            m_noteTap->add(e.m_sample, static_cast<int>(partOf(e.m_source)), sent, 0.0f, false, -1);
            sent = 0xFF;
        }
    }
    switch (e.m_source) {
    case SRC_DRUMS:
        if (e.m_on) {
            // The toms are tuned to the root of the moment, the high one a fifth above.
            const float low = static_cast<float>(midiToHz(43 + pitchClass(m_score.m_keyRoot + m_score.rootAt(beat()) - 7)));
            m_drums.hit(e.m_pitch, e.m_velocity, e.m_pitch == 48 || e.m_pitch == 47 || e.m_pitch == 50 ? low * 1.5f : low);
        }
        break;
    case SRC_STRINGS:
        if (e.m_on) m_strings.noteOn(std::clamp(e.m_pitch + m_transpose, 0, 127), e.m_velocity, e.m_id); else m_strings.noteOff(e.m_id);
        break;
    case SRC_POLY:
        if (e.m_on) m_poly.noteOn(std::clamp(e.m_pitch + m_transpose, 0, 127), e.m_velocity, e.m_id); else m_poly.noteOff(e.m_id);
        break;
    case SRC_TAPE:
        if (e.m_on) m_tape.noteOn(std::clamp(e.m_pitch + m_transpose, 0, 127), e.m_velocity, e.m_id); else m_tape.noteOff(e.m_id);
        break;
    default:
        if (e.m_on) m_voices.noteOn(e.m_source, std::clamp(e.m_pitch + m_transpose, 0, 127), e.m_velocity, e.m_accent, e.m_legato, e.m_id, e.m_bright, e.m_decay);
        else m_voices.noteOff(e.m_source, e.m_id);
        break;
    }
    if (e.m_on) m_strips[e.m_source].m_running = true;
}

bool Engine::process(float* L, float* R, int n)
{
    if (m_stemL != nullptr)
        for (int c = 0; c <= Channels; ++c) { std::fill(m_stemL[c], m_stemL[c] + n, 0.0f); std::fill(m_stemR[c], m_stemR[c] + n, 0.0f); }
    int done = 0;
    while (done < n) {
        if ((m_sample % Cell) == 0 || m_cellDirty) { updateCell(); m_cellDirty = false; }
        // Every event due at this sample, offs before ons; then the keys played on a MIDI keyboard (queueLive).
        while (m_evCursor < m_events.size() && m_events[m_evCursor].m_sample <= m_sample) dispatch(m_events[m_evCursor++]);
        while (m_liveCursor < m_liveCount && m_liveQueue[m_liveCursor].m_at <= m_sample) playLive(m_liveQueue[m_liveCursor++]);
        int64_t end = std::min<int64_t>(m_sample + (n - done), (m_sample / Cell + 1) * Cell);
        if (m_evCursor < m_events.size()) end = std::min(end, m_events[m_evCursor].m_sample);
        if (m_liveCursor < m_liveCount) end = std::min(end, m_liveQueue[m_liveCursor].m_at);
        const int len = static_cast<int>(end - m_sample);
        m_spanAt = done;
        renderSpan(L + done, R + done, len);
        m_sample += len;
        done += len;
    }
    while (m_liveCursor < m_liveCount) playLive(m_liveQueue[m_liveCursor++]);   // a key past the block's end: now
    m_liveCount = m_liveCursor = 0;
    return seconds() < lengthSeconds();
}

int Engine::sourceOf(int target)
{
    switch (target) {
    case perform::keys::LEAD: return SRC_LEAD;
    case perform::keys::DRONE: return SRC_DRONE;
    case perform::keys::POLY: return SRC_POLY;
    case perform::keys::TAPE_KEYS: return SRC_TAPE;
    case perform::keys::STRINGS: return SRC_STRINGS;
    case perform::keys::DRUMS: return SRC_DRUMS;
    default: return target >= perform::keys::ROW1 && target <= perform::keys::ROW8 ? target - perform::keys::ROW1 : -1;
    }
}

int Engine::targetOf(int source)
{
    for (int t = perform::keys::LEAD; t < perform::keys::BY_CHANNEL; ++t)
        if (sourceOf(t) == source) return t;
    return perform::keys::OFF;
}

PartE Engine::partOf(int source)
{
    switch (source) {
    case SRC_LEAD: return PartE::LEAD;
    case SRC_DRONE: return PartE::DRONE;
    case SRC_TAPE: return PartE::TAPE_KEYS;
    case SRC_STRINGS: return PartE::STRINGS;
    case SRC_POLY: return PartE::PAD;
    case SRC_DRUMS: return PartE::DRUMS;
    case SRC_ATMOS: return PartE::ATMOS;
    default: return rowPart(source);
    }
}

bool Engine::silenced(int source) const
{
    if (m_composerOff) return true;
    if (m_keyTarget == perform::keys::OFF || !m_keyReplace) return false;
    const int t = targetOf(source);
    if (t == perform::keys::OFF) return false;
    // By channel, a voice is the player's from its first played key on (until liveAllOff).
    if (m_keyTarget == perform::keys::BY_CHANNEL) return ((m_keyPlayed >> t) & 1u) != 0;
    return m_keyTarget == t;
}

int Engine::keyboardTarget(int channel) const
{
    const int part = m_params.getInt(m_params.id(ModuleE::PERFORM, 0, perform::KEYBOARD_PART));
    if (part != perform::keys::BY_CHANNEL) return std::clamp(part, 0, static_cast<int>(perform::keys::BY_CHANNEL) - 1);
    // By channel: 1 .. 8 the rows, 9 the lead, 10 the drums (General MIDI's), 11 the drone, 12 the poly synth, 13 the tape
    // keys, 14 the strings.
    if (channel >= 0 && channel < Rows) return perform::keys::ROW1 + channel;
    static const int Channel[6] = { perform::keys::LEAD, perform::keys::DRUMS, perform::keys::DRONE, perform::keys::POLY,
                                     perform::keys::TAPE_KEYS, perform::keys::STRINGS };
    return channel >= Rows && channel < Rows + 6 ? Channel[channel - Rows] : perform::keys::OFF;
}

void Engine::queueLive(int offset, int pitch, int velocity, int channel, bool on, int target)
{
    if (!m_live || pitch < 0 || pitch > 127 || m_liveCount >= LiveQueue) return;
    LiveKey k;
    k.m_at = m_sample + std::max(0, offset);
    k.m_pitch = pitch;
    k.m_velocity = static_cast<float>(std::clamp(velocity, 1, 127)) / 127.0f;
    k.m_target = on ? (target > perform::keys::OFF ? target : keyboardTarget(channel)) : m_liveTarget[pitch] - 1;   // the split names one
    k.m_on = on;
    if (k.m_target <= perform::keys::OFF) return;
    m_liveQueue[m_liveCount++] = k;
}

void Engine::playLive(const LiveKey& k)
{
    const int source = sourceOf(k.m_target);
    if (source < 0) return;
    if (k.m_on && m_liveTarget[k.m_pitch] != 0) playLive(LiveKey{ k.m_at, k.m_pitch, 0.0f, m_liveTarget[k.m_pitch] - 1, false });
    Ev e{};
    e.m_sample = m_sample;
    e.m_on = k.m_on ? 1 : 0;
    e.m_source = static_cast<uint8_t>(source);
    e.m_accent = k.m_velocity > 0.86f;
    // Untransposed: dispatch() adds the transposition the composer's notes get.
    e.m_pitch = source == SRC_DRUMS ? k.m_pitch : k.m_pitch - m_transpose;
    e.m_velocity = k.m_velocity;
    e.m_id = 0x40000000 + k.m_pitch;   // a key's own id: its release finds its note
    m_liveEvent = true;
    dispatch(e);
    m_liveEvent = false;
    m_liveTarget[k.m_pitch] = k.m_on ? static_cast<uint8_t>(k.m_target + 1) : 0;
    if (k.m_on) m_keyPlayed |= 1u << k.m_target;
}

void Engine::liveAllOff()
{
    m_liveCount = m_liveCursor = 0;
    for (int k = 0; k < 128; ++k)
        if (m_liveTarget[k] != 0) playLive(LiveKey{ m_sample, k, 0.0f, m_liveTarget[k] - 1, false });
    m_keyPlayed = 0;
}

} // namespace eph
