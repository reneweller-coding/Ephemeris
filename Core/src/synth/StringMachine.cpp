/**
 * @file StringMachine.cpp
 * @brief Divide-down registers, the registration, the ensemble and the phaser.
 * @author Rene Weller
 */
#include "eph/synth/StringMachine.h"
#include "eph/synth/Oscillator.h"   // polyBlep
#include <algorithm>
#include <cmath>
#include <cstring>

namespace eph {

namespace {
constexpr double TopOctaveMidi = 108.0;   ///< C8: the top-octave generator's C
constexpr double Modulus = 256.0;         ///< the counter wraps after eight octaves of division

/**
 * @brief A registration: the weights of saw 16', saw 8', saw 4', square 8', square 4', a tone factor, and a trim
 *        measured so that every registration plays a held chord as loud as the Violins (selftest "string
 *        machine"; a saw and a square of one footage are phase-locked and partly cancel, which the plain
 *        normalisation by the weights cannot know).
 */
struct Registration {
    const char* m_name;                   ///< its name
    float m_w[StringMachine::Registers];   ///< the registers' weights
    float m_tone;                         ///< the tone's factor
    float m_trim;                         ///< the loudness trim
};
/** @brief The registrations, in the order of the knob. */
const Registration RegTable[StringMachine::Registrations] = {
    { "Violins", { 0.00f, 1.00f, 0.55f, 0.00f, 0.00f }, 1.25f, 1.000f },
    { "Violas",  { 0.00f, 0.85f, 0.20f, 0.45f, 0.00f }, 1.00f, 2.100f },
    { "Cellos",  { 0.55f, 0.90f, 0.00f, 0.20f, 0.00f }, 0.70f, 1.236f },
    { "Basses",  { 1.00f, 0.45f, 0.00f, 0.00f, 0.00f }, 0.50f, 1.046f },
    { "Full",    { 0.60f, 1.00f, 0.60f, 0.00f, 0.00f }, 1.10f, 0.867f },
    { "Hollow",  { 0.00f, 0.30f, 0.00f, 1.00f, 0.45f }, 0.85f, 0.908f },
    { "Brass",   { 0.20f, 1.00f, 0.30f, 0.50f, 0.00f }, 1.60f, 1.673f },
    { "Organ",   { 0.45f, 0.00f, 0.00f, 1.00f, 0.70f }, 0.90f, 0.885f },
};

/**
 * @brief The fraction of @p x.
 * @param[in] x  the number
 * @return its fraction, 0..1
 */
float frac(double x) { return static_cast<float>(x - std::floor(x)); }
} // namespace

const char* StringMachine::registrationName(int i) { return RegTable[std::clamp(i, 0, Registrations - 1)].m_name; }

void StringMachine::prepare(double sampleRate, uint64_t seed)
{
    m_sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    size_t n = 4;
    while (static_cast<double>(n) < 0.04 * m_sr) n <<= 1;
    m_line.assign(n, 0.0f);
    m_mask = n - 1;
    m_write = 0;
    // The twelve top-octave generators start where the seed puts them, as a machine's do at power-on.
    Rng rng;
    rng.seed(seed);
    for (double& c : m_counter) c = Modulus * static_cast<double>(rng.uniform());
    for (Key& k : m_keys) k = Key{};
    m_slow = m_fast = m_animPhase = m_phPhase = 0.0;
    m_count = 0;
    m_tone.reset();
    for (Svf& b : m_bbd) { b.reset(); b.set(7000.0f, 0.0f, static_cast<float>(m_sr)); }
    for (int c = 0; c < 2; ++c) { for (int i = 0; i < 4; ++i) m_apX[c][i] = m_apY[c][i] = 0.0f; m_phFb[c] = 0.0f; }
    m_ring = 0.0f;
    m_mod.prepare(m_sr, mixSeed(seed, 0x5354ull));
    m_mod.set(m_s.m_mod);
    m_pitchMul = 1.0;
    m_toneMul = 1.0f;
    set(m_s);
}

void StringMachine::set(const StringSettings& s)
{
    if (std::memcmp(&s.m_mod, &m_s.m_mod, sizeof(ModSettings)) != 0) m_mod.set(s.m_mod);
    m_s = s;
}

void StringMachine::registrationAt(float position)
{
    const float x = std::clamp(position, 0.0f, static_cast<float>(Registrations - 1));
    const int i = std::min(static_cast<int>(x), Registrations - 2);
    const float f = x - static_cast<float>(i);
    const Registration& a = RegTable[i];
    const Registration& b = RegTable[i + 1];
    // The footage balance tilts the mix: low footages (16') against high ones (4').
    const float low = 1.5f - m_s.m_feet, high = 0.5f + m_s.m_feet;
    const float tilt[Registers] = { low, 1.0f, high, 1.0f, high };
    float sumSq = 0.0f;
    for (int r = 0; r < Registers; ++r) {
        m_weight[r] = (a.m_w[r] + f * (b.m_w[r] - a.m_w[r])) * tilt[r];
        sumSq += m_weight[r] * m_weight[r];
    }
    // The same loudness for every registration: the Violins' 8' and 4' saws at 0.4 are the reference.
    m_regGain = 1.077f / std::sqrt(std::max(sumSq, 1e-4f)) * (a.m_trim + f * (b.m_trim - a.m_trim));
    float tone = m_s.m_toneHz * (a.m_tone + f * (b.m_tone - a.m_tone));
    if (m_mod.active() && m_mod.targets(ModDestE::CUTOFF)) tone *= m_toneMul;
    tone = std::min(tone, static_cast<float>(0.45 * m_sr));
    m_tone.set(tone, 0.1f, static_cast<float>(m_sr));
}

void StringMachine::noteOn(int pitch, float velocity, int id)
{
    Key* slot = nullptr;
    for (Key& k : m_keys) if (!k.m_on) { slot = &k; break; }
    if (slot == nullptr) {
        slot = &m_keys[0];
        for (Key& k : m_keys) if (k.m_order < slot->m_order) slot = &k;
    }
    slot->m_on = slot->m_held = true;
    slot->m_pitch = pitch;
    slot->m_id = id;
    slot->m_velocity = velocity;
    slot->m_order = ++m_order;
    slot->m_pc = pitchClass(pitch);
    const int octDown = static_cast<int>(std::lround((TopOctaveMidi + slot->m_pc - pitch) / 12.0));   // divisions below the top
    slot->m_inv8 = std::ldexp(1.0, -std::clamp(octDown, 0, 8));
    slot->m_inv16 = std::ldexp(1.0, -std::clamp(octDown + 1, 0, 8));
    slot->m_inv4 = std::ldexp(1.0, -std::clamp(octDown - 1, 0, 8));
    // The envelope is not reset: a stolen key glides from where it was, as the machine's did.
    slot->m_decaying = false;
    m_mod.noteOn(m_count, beatAt(m_count));
}

void StringMachine::noteOff(int id)
{
    for (Key& k : m_keys) if (k.m_on && k.m_id == id) k.m_held = false;
    bool held = false;
    for (const Key& k : m_keys) held = held || (k.m_on && k.m_held);
    if (!held) m_mod.noteOff();
}

void StringMachine::silence()
{
    for (Key& k : m_keys) k = Key{};
    std::fill(m_line.begin(), m_line.end(), 0.0f);
    m_ring = 0.0f;
    m_mod.kill();
}

bool StringMachine::active() const
{
    for (const Key& k : m_keys) if (k.m_on) return true;
    return m_ring > 1e-5f;
}

void StringMachine::process(float* L, float* R, int n)
{
    const double inv = 1.0 / m_sr;
    const float att = static_cast<float>(1.0 - std::exp(-inv / std::max(0.005, static_cast<double>(m_s.m_attackS) / 3.0)));
    const float rel = static_cast<float>(1.0 - std::exp(-inv / std::max(0.01, static_cast<double>(m_s.m_releaseS) / 3.0)));
    const float dec = static_cast<float>(1.0 - std::exp(-inv / std::max(0.01, static_cast<double>(m_s.m_decayS) / 3.0)));
    const float sus = std::clamp(m_s.m_sustain, 0.0f, 1.0f);
    // Top-octave increments per pitch class (in counter units per sample).
    double inc[12];
    for (int pc = 0; pc < 12; ++pc) inc[pc] = midiToHz(TopOctaveMidi + pc) * inv;
    // The ensemble's shape by type: base delay, slow and fast depth (ms), their rates, the taps and the spread.
    const int type = std::clamp(m_s.m_ensembleType, 0, 2);
    static const double Base[3] = { 6.0, 8.0, 10.0 }, SlowMs[3] = { 2.0, 2.5, 3.5 }, FastMs[3] = { 0.35, 0.0, 0.5 };
    static const double SlowHz[3] = { 0.6, 0.8, 0.25 }, FastHz[3] = { 6.0, 6.0, 5.0 };
    static const float Side[3] = { 0.6f, 1.0f, 0.7f };   // the share of the outer tap on each side
    const int taps = type == 1 ? 2 : 3;
    const float e = m_s.m_ensemble;
    for (int i = 0; i < n; ++i) {
        if ((m_count & 31) == 0) {
            if (m_mod.active()) {
                // The machine's matrix (Modulation.h): pitch and tone as factors, the level below.
                const float ext[ModSources] = {};
                m_mod.evaluate(m_count, beatAt(m_count), ext, m_mo);
                m_pitchMul = std::exp2(static_cast<double>(m_mod.offset(m_mo, ModDestE::PITCH)) / 12.0);
                m_toneMul = std::exp2(m_mod.offset(m_mo, ModDestE::CUTOFF));
            }
            // The registration, moved by the animation's slow sine, and the phaser's sweep.
            const double dt = 32.0 * inv;
            m_animPhase += m_s.m_animateHz * dt;
            m_animPhase -= std::floor(m_animPhase);
            registrationAt(m_s.m_registration + m_s.m_animate * 3.5f * sin01(m_animPhase));
            if (m_s.m_phaser > 0.0f) {
                m_phPhase += 0.3 * dt;
                m_phPhase -= std::floor(m_phPhase);
                for (int c = 0; c < 2; ++c) {
                    const double sweep = 0.5 + 0.5 * sin01(m_phPhase + 0.25 * c);
                    const double hz = 200.0 * std::pow(12.5, sweep);                       // 200 Hz .. 2.5 kHz
                    const double t = std::tan(3.14159265358979 * std::min(hz, 0.45 * m_sr) * inv);
                    m_apCoef[c] = static_cast<float>((t - 1.0) / (t + 1.0));
                }
            }
        }
        ++m_count;
        const bool bend = m_mod.active() && m_mod.targets(ModDestE::PITCH);
        double step[12];
        for (int pc = 0; pc < 12; ++pc) {
            step[pc] = bend ? inc[pc] * m_pitchMul : inc[pc];
            m_counter[pc] += step[pc];
            if (m_counter[pc] >= Modulus) m_counter[pc] -= Modulus;
        }
        float sum = 0.0f;
        for (Key& k : m_keys) {
            if (!k.m_on) continue;
            // Crescendo, then (below a sustain level of 1) the decay towards it, and the release.
            if (k.m_held && !k.m_decaying && sus < 1.0f && k.m_env > 0.98f) k.m_decaying = true;
            k.m_env += ((k.m_held ? (k.m_decaying ? sus : 1.0f) : 0.0f) - k.m_env) * (k.m_held ? (k.m_decaying ? dec : att) : rel);
            if (!k.m_held && k.m_env < 1e-4f) { k.m_on = false; continue; }
            // The phases of the footages: the counter divided, which keeps every octave in lock.
            const double c = m_counter[k.m_pc];
            float v = 0.0f;
            const double invs[3] = { k.m_inv16, k.m_inv8, k.m_inv4 };
            for (int f = 0; f < 3; ++f) {
                const float ws = m_weight[f];
                const float wq = f == 0 ? 0.0f : m_weight[f + 2];   // squares at 8' and 4'
                if (ws <= 1e-4f && wq <= 1e-4f) continue;
                const float p = frac(c * invs[f]);
                const float dt = static_cast<float>(step[k.m_pc] * invs[f]);
                const float bl = polyBlep(p, dt);
                if (ws > 1e-4f) v += ws * (2.0f * p - 1.0f - bl);
                if (wq > 1e-4f) v += wq * ((p < 0.5f ? 1.0f : -1.0f) + bl - polyBlep(frac(p + 0.5), dt));
            }
            sum += k.m_env * k.m_velocity * v;
        }
        if (m_mod.active() && m_mod.targets(ModDestE::LEVEL)) sum *= std::clamp(1.0f + m_mo[static_cast<int>(ModDestE::LEVEL)], 0.0f, 2.0f);
        const float dry = m_tone.lp(0.12f * m_regGain * sum);
        m_ring = std::max(std::fabs(dry), m_ring * 0.9999f);
        // The ensemble: delay lines swept by a slow and a fast LFO, a third (or a half) of a cycle apart.
        m_line[m_write] = dry;
        m_slow += SlowHz[type] * inv; if (m_slow >= 1.0) m_slow -= 1.0;
        m_fast += FastHz[type] * inv; if (m_fast >= 1.0) m_fast -= 1.0;
        float tap[3] = { 0.0f, 0.0f, 0.0f };
        for (int b = 0; b < taps; ++b) {
            const double ph = static_cast<double>(b) / taps;
            const double ms = Base[type] + e * (SlowMs[type] * sin01(m_slow + ph) + FastMs[type] * sin01(m_fast + ph));
            const double pos = static_cast<double>(m_write) - ms * 0.001 * m_sr;
            const double fl = std::floor(pos);
            const float t = static_cast<float>(pos - fl);
            const size_t j = static_cast<size_t>(static_cast<int64_t>(fl)) & m_mask;
            const float a = m_line[j], cc = m_line[(j + 1) & m_mask];
            tap[b] = m_bbd[b].lp(a + t * (cc - a));
        }
        m_write = (m_write + 1) & m_mask;
        float l, r;
        if (taps == 2) {
            l = (1.0f - 0.5f * e) * dry + e * tap[0];
            r = (1.0f - 0.5f * e) * dry + e * tap[1];
        } else {
            const float side = Side[type];
            l = (1.0f - 0.5f * e) * dry + e * (side * tap[0] + (1.0f - side) * tap[1]);
            r = (1.0f - 0.5f * e) * dry + e * (side * tap[2] + (1.0f - side) * tap[1]);
        }
        // The phaser: four first-order all-passes with feedback, mixed with the direct sound for the notches.
        if (m_s.m_phaser > 0.0f) {
            float* out[2] = { &l, &r };
            for (int ch = 0; ch < 2; ++ch) {
                float x = *out[ch] + 0.35f * m_phFb[ch];
                const float a = m_apCoef[ch];
                for (int st = 0; st < 4; ++st) {
                    const float y = a * x + m_apX[ch][st] - a * m_apY[ch][st];
                    m_apX[ch][st] = x;
                    m_apY[ch][st] = y;
                    x = y;
                }
                m_phFb[ch] = x;
                *out[ch] += m_s.m_phaser * 0.5f * (x - *out[ch]);
            }
        }
        L[i] = l;
        R[i] = r;
    }
}

} // namespace eph
