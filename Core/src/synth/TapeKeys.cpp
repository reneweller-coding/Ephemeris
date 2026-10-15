/**
 * @file TapeKeys.cpp
 * @brief The tape keyboard.
 * @author Rene Weller
 */
#include "eph/synth/TapeKeys.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace eph {

namespace {

/// Formants of a mixed choir: "aah" and "ooh" (after Klatt 1980; male and female values averaged and
/// rounded). Frequency in Hz, bandwidth in Hz, level in dB.
constexpr float AahF[5] = { 730.0f, 1090.0f, 2440.0f, 3400.0f, 4200.0f };
constexpr float AahB[5] = { 80.0f, 90.0f, 120.0f, 180.0f, 250.0f };   ///< "aah": bandwidths, Hz
constexpr float AahG[5] = { 0.0f, -5.0f, -15.0f, -22.0f, -28.0f };   ///< "aah": levels, dB
constexpr float OohF[5] = { 330.0f, 870.0f, 2240.0f, 3200.0f, 4100.0f };   ///< "ooh": frequencies, Hz
constexpr float OohB[5] = { 60.0f, 80.0f, 110.0f, 170.0f, 240.0f };   ///< "ooh": bandwidths, Hz
constexpr float OohG[5] = { 0.0f, -10.0f, -26.0f, -32.0f, -36.0f };   ///< "ooh": levels, dB

constexpr int StringSaws = 5;   ///< saws of a strings tape

/**
 * @brief The pressure pad's rise time constant for tape set @p set, s.
 * @param[in] set  the tape set
 * @return the rise time constant in seconds
 */
double riseTau(TapeSetE set) { return set == TapeSetE::STRINGS ? 0.08 : (set == TapeSetE::FLUTE ? 0.05 : 0.06); }

} // namespace

void TapeKeys::prepare(double sampleRate, uint64_t seed)
{
    m_sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_seed = seed;
    m_rng.seed(mixSeed(seed, 17));
    m_mod.prepare(m_sr, mixSeed(seed, 0x5441ull));
    m_mod.set(m_s.m_mod);
    reset();
    set(m_s);
}

void TapeKeys::reset()
{
    for (Key& k : m_keys) { k.m_on = k.m_held = false; k.m_id = -1; }
    m_wowPhase = m_flutterPhase = m_drift = m_driftTarget = 0.0;
    m_count = 0;
    m_thump = 0.0f;
    m_mod.kill();
}

void TapeKeys::set(const TapeSettings& s)
{
    const bool vowelMoved = s.m_vowel != m_s.m_vowel || s.m_set != m_s.m_set;
    if (std::memcmp(&s.m_mod, &m_s.m_mod, sizeof(ModSettings)) != 0) m_mod.set(s.m_mod);
    m_s = s;
    // The fall after the key (a time constant) and the player's decay (to 5 % of the way in its time).
    m_fallCoef = std::exp(-1.0 / (static_cast<double>(s.m_releaseMs) / 1000.0 * m_sr));
    m_decayCoef = std::exp(-3.0 / (std::max(0.005, static_cast<double>(s.m_decayMs) / 1000.0) * m_sr));
    for (int f = 0; f < 5; ++f) m_formantGain[f] = dbToGain(AahG[f] + m_s.m_vowel * (OohG[f] - AahG[f]));
    const float sr = static_cast<float>(m_sr);
    for (Key& k : m_keys) {
        if (!k.m_on) continue;
        k.m_tone.set(std::min(0.45f * sr, m_s.m_toneHz * static_cast<float>(std::exp2(0.4 * k.m_cents / 6.0 + m_mod.offset(m_mo, ModDestE::CUTOFF)))), 0.0f, sr);
        if (vowelMoved && k.m_set == TapeSetE::CHOIR) {
            for (int f = 0; f < 5; ++f) {
                const float F = AahF[f] + m_s.m_vowel * (OohF[f] - AahF[f]);
                const float B = AahB[f] + m_s.m_vowel * (OohB[f] - AahB[f]);
                k.m_formant[f].setQ(F, F / B, sr);
            }
        }
    }
}

double TapeKeys::speedFor(int keysDown) const
{
    return std::exp2(-static_cast<double>(m_s.m_sagCents) * std::max(0, keysDown - 1) / 1200.0);
}

void TapeKeys::startKey(Key& k, int pitch, float velocity, int id)
{
    k.m_on = k.m_held = true;
    k.m_pitch = pitch;
    k.m_id = id;
    k.m_velocity = velocity;
    k.m_age = 0.0;
    k.m_released = -1.0;
    k.m_order = ++m_counter;
    k.m_set = m_s.m_set;
    k.m_rise = 0.0;
    k.m_riseCoef = 1.0 - std::exp(-1.0 / (riseTau(k.m_set) * m_sr));
    k.m_fall = 1.0;
    k.m_decay = 1.0;
    // This key's tape: the same every time the key is pressed, as an instrument has one set of tapes.
    Rng tape;
    tape.seed(mixSeed(m_seed, 1000 + static_cast<uint64_t>(pitch)));
    const double a = static_cast<double>(m_s.m_age);
    k.m_cents = 6.0 * a * static_cast<double>(tape.bipolar());
    k.m_gain = dbToGain(1.5f * m_s.m_age * tape.bipolar());
    k.m_lag = 0.005 + 0.02 * a * static_cast<double>(tape.uniform());
    const float sr = static_cast<float>(m_sr);
    k.m_tone.reset();
    k.m_tone.set(std::min(0.45f * sr, m_s.m_toneHz * static_cast<float>(std::exp2(0.4 * k.m_cents / 6.0))), 0.0f, sr);
    for (int i = 0; i < Singers; ++i) {
        Singer& g = k.m_singers[i];
        g.m_detune = (k.m_set == TapeSetE::STRINGS ? 12.0 : 9.0) * static_cast<double>(tape.bipolar());
        g.m_vibHz = 4.8 + 1.4 * static_cast<double>(tape.uniform());
        g.m_vibDepth = 15.0 + 20.0 * static_cast<double>(tape.uniform());
        g.m_vibPhase = static_cast<double>(tape.uniform());
        g.m_wander = g.m_wanderTarget = 0.0;
        g.m_level = 0.8 + 0.4 * static_cast<double>(tape.uniform());
        g.m_osc.restart(static_cast<double>(tape.uniform()), 0.0);
        g.m_tilt.reset();
        g.m_tilt.setQ(1100.0f, 0.707f, sr);
    }
    for (int f = 0; f < 5; ++f) {
        const float F = AahF[f] + m_s.m_vowel * (OohF[f] - AahF[f]);
        const float B = AahB[f] + m_s.m_vowel * (OohB[f] - AahB[f]);
        k.m_formant[f].reset();
        k.m_formant[f].setQ(F, F / B, sr);
    }
    k.m_phase = 0.0;
    k.m_breath.reset();
    k.m_breath.setQ(std::min(0.45f * sr, static_cast<float>(2.0 * midiToHz(pitch))), 4.0f, sr);
    m_thump += 0.12f * m_s.m_age * velocity;
}

void TapeKeys::noteOn(int pitch, float velocity, int id)
{
    Key* slot = nullptr;
    for (Key& k : m_keys) if (!k.m_on) { slot = &k; break; }
    if (slot == nullptr) {
        slot = &m_keys[0];
        for (Key& k : m_keys) if (k.m_order < slot->m_order) slot = &k;
    }
    startKey(*slot, pitch, velocity, id);
    m_mod.noteOn(m_count, beatAt(m_count));
}

void TapeKeys::noteOff(int id)
{
    for (Key& k : m_keys)
        if (k.m_on && k.m_held && k.m_id == id) { k.m_held = false; k.m_released = 0.0; }
    bool held = false;
    for (const Key& k : m_keys) held = held || k.m_held;
    if (!held) m_mod.noteOff();
}

bool TapeKeys::active() const
{
    for (const Key& k : m_keys) if (k.m_on) return true;
    return m_thump > 1e-5f;
}

float TapeKeys::render(Key& k, double speedCents)
{
    const double dt = 1.0 / m_sr;
    k.m_age += dt;
    if (k.m_released >= 0.0) { k.m_released += dt; k.m_fall *= m_fallCoef; }
    // The envelope of the machine: the lag, the pressure pad's rise, the tape end, the release.
    if (k.m_age > k.m_lag) k.m_rise += (1.0 - k.m_rise) * k.m_riseCoef;
    double env = k.m_rise * k.m_fall;
    // The player's envelope over the machine's: the swell, then the decay towards the sustain level.
    const double swellS = static_cast<double>(m_s.m_swellMs) / 1000.0;
    if (swellS > 0.001 && k.m_age < swellS) env *= k.m_age / swellS;
    if (m_s.m_sustain < 1.0f && k.m_age >= std::max(swellS, 0.0)) {
        k.m_decay *= m_decayCoef;
        env *= static_cast<double>(m_s.m_sustain) + (1.0 - static_cast<double>(m_s.m_sustain)) * k.m_decay;
    }
    const double fadeStart = TapeSeconds - 0.33;
    if (k.m_age > fadeStart) env *= std::max(0.0, 1.0 - (k.m_age - fadeStart) / 0.33);
    const double gone = m_s.m_releaseMs > 70.0f ? 10.0 * static_cast<double>(m_s.m_releaseMs) / 1000.0 : 0.7;   // a longer fall rings longer
    if (k.m_age >= TapeSeconds || (k.m_released >= 0.0 && k.m_released > gone)) { k.m_on = false; return 0.0f; }

    const double cents = k.m_cents + speedCents;
    const float sr = static_cast<float>(m_sr);
    float y = 0.0f;
    switch (k.m_set) {
    case TapeSetE::CHOIR: {
        if ((m_count & 7) == 0) {
            for (int i = 0; i < m_singers; ++i) {
                Singer& g = k.m_singers[i];
                g.m_vibPhase += 8.0 * g.m_vibHz / m_sr;
                if (g.m_vibPhase >= 1.0) g.m_vibPhase -= 1.0;
                if ((m_count & 2047) == 0) g.m_wanderTarget = 6.0 * static_cast<double>(m_rng.bipolar());
                g.m_wander += (g.m_wanderTarget - g.m_wander) * 0.004;
                const double c = cents + g.m_detune + g.m_wander + g.m_vibDepth * static_cast<double>(sin01(g.m_vibPhase));
                g.m_osc.set(midiToHz(k.m_pitch + c * 0.01), m_sr, 0.0f, 0.5f);
            }
        }
        float sum = 0.0f;
        for (int i = 0; i < m_singers; ++i) sum += static_cast<float>(k.m_singers[i].m_level) * k.m_singers[i].m_osc.next();
        float lo, bp, hi;
        k.m_singers[0].m_tilt.tick(sum * (1.0f / std::sqrt(static_cast<float>(Singers * m_singers))), lo, bp, hi);
        const float src = lo;
        for (int f = 0; f < 5; ++f) {
            float fl, fb, fh;
            k.m_formant[f].tick(src, fl, fb, fh);
            y += m_formantGain[f] * fb * k.m_formant[f].m_k;
        }
        y *= 2.5f;
        break;
    }
    case TapeSetE::STRINGS: {
        if ((m_count & 7) == 0)
            for (int i = 0; i < StringSaws; ++i)
                k.m_singers[i].m_osc.set(midiToHz(k.m_pitch + (cents + k.m_singers[i].m_detune) * 0.01), m_sr, 0.0f, 0.5f);
        float sum = 0.0f;
        for (int i = 0; i < StringSaws; ++i) sum += k.m_singers[i].m_osc.next();
        float lo, bp, hi;
        k.m_singers[0].m_tilt.tick(sum * (1.0f / StringSaws), lo, bp, hi);   // 1.1 kHz: the bowed body's roll-off
        y = 0.6f * lo + 0.25f * bp;
        break;
    }
    case TapeSetE::FLUTE: {
        const double f = midiToHz(k.m_pitch + cents * 0.01);
        k.m_phase += f / m_sr;
        if (k.m_phase >= 1.0) k.m_phase -= 1.0;
        const float s1 = sin01(k.m_phase), s2 = sin01(2.0 * k.m_phase), s3 = sin01(3.0 * k.m_phase);
        float lo, bp, hi;
        k.m_breath.tick(m_rng.bipolar(), lo, bp, hi);
        const float chiff = k.m_age < 0.04 ? static_cast<float>(1.0 - k.m_age / 0.04) : 0.0f;
        y = 0.8f * s1 + 0.15f * s2 + 0.05f * s3 + (0.08f + 0.5f * chiff) * bp * k.m_breath.m_k;
        (void)sr;
        break;
    }
    default: break;
    }
    return k.m_tone.lp(y) * static_cast<float>(env) * k.m_gain * k.m_velocity;
}

void TapeKeys::process(float* out, int n)
{
    for (int i = 0; i < n; ++i) {
        if ((m_count & 31) == 0) {
            if ((m_count % 32768) == 0) m_driftTarget = static_cast<double>(m_rng.bipolar());
            m_drift += (m_driftTarget - m_drift) * 0.002;
            if (m_mod.active()) {
                // The keyboard's matrix (Modulation.h); a moving tone retunes every key's head filter.
                const float ext[ModSources] = {};
                m_mod.evaluate(m_count, beatAt(m_count), ext, m_mo);
                if (m_mod.targets(ModDestE::CUTOFF)) {
                    const float sr = static_cast<float>(m_sr);
                    for (Key& k : m_keys)
                        if (k.m_on) k.m_tone.set(std::min(0.45f * sr, m_s.m_toneHz * static_cast<float>(std::exp2(0.4 * k.m_cents / 6.0 + m_mo[static_cast<int>(ModDestE::CUTOFF)]))), 0.0f, sr);
                }
            }
        }
        m_wowPhase += 0.6 / m_sr;
        if (m_wowPhase >= 1.0) m_wowPhase -= 1.0;
        m_flutterPhase += 7.5 / m_sr;
        if (m_flutterPhase >= 1.0) m_flutterPhase -= 1.0;
        int down = 0;
        bool any = false;
        for (const Key& k : m_keys) { down += k.m_held ? 1 : 0; any = any || k.m_on; }
        double speed = static_cast<double>(m_s.m_wowCents) * (0.7 * static_cast<double>(sin01(m_wowPhase)) + 0.3 * m_drift)
                     + static_cast<double>(m_s.m_flutterCents) * static_cast<double>(sin01(m_flutterPhase))
                     - static_cast<double>(m_s.m_sagCents) * std::max(0, down - 1);
        const bool mod = m_mod.active();
        if (mod && m_mod.targets(ModDestE::PITCH)) speed += 100.0 * static_cast<double>(m_mo[static_cast<int>(ModDestE::PITCH)]);
        float y = 0.0f;
        for (Key& k : m_keys) if (k.m_on) y += render(k, speed);
        y *= 0.3f;
        if (mod && m_mod.targets(ModDestE::LEVEL)) y *= std::clamp(1.0f + m_mo[static_cast<int>(ModDestE::LEVEL)], 0.0f, 2.0f);
        // The pressure pad's thump and the tape's hiss.
        if (m_thump > 1e-5f) { y += m_thump * m_rng.bipolar(); m_thump *= 0.9974f; }
        if (any) y += (0.0003f + 0.001f * m_s.m_age) * m_rng.bipolar();
        out[i] = std::tanh(1.5f * y) / 1.5f;
        ++m_count;
    }
}

} // namespace eph
