/**
 * @file Drums.cpp
 * @brief The analogue kit.
 */
#include "eph/synth/Drums.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
constexpr double MetalHz[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };   ///< the TR-808's hat oscillators
constexpr float Pan[8] = { 0.0f, 0.05f, 0.3f, 0.3f, -0.4f, 0.4f, -0.2f, 0.5f }; ///< kick .. shaker
}

void DrumKit::prepare(double sampleRate, uint64_t seed)
{
    m_sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_rng.seed(seed);
    for (Voice& v : m_v) v = Voice{};
    for (double& m : m_metal) m = 0.0;
    m_hatBp.reset(); m_hatHp.reset();
    set(m_s);
}

void DrumKit::set(const DrumSettings& s)
{
    m_s = s;
    const float sr = static_cast<float>(m_sr);
    m_hatBp.setQ(std::min(0.45f * sr, 7000.0f + 4000.0f * s.m_tone), 1.2f, sr);
    m_hatHp.setQ(std::min(0.45f * sr, 6000.0f + 2000.0f * s.m_tone), 0.7f, sr);
    m_v[SNARE].m_bp.setQ(std::min(0.45f * sr, 2500.0f + 3000.0f * s.m_tone), 0.8f, sr);
    m_v[SHAKER].m_bp.setQ(std::min(0.45f * sr, 6000.0f), 1.5f, sr);
}

void DrumKit::hit(int note, float velocity, float tomHz)
{
    InstE i;
    switch (note) {
    case 36: i = KICK; break;
    case 38: case 40: i = SNARE; break;
    case 42: case 44: i = CLOSED; break;
    case 46: i = OPEN; break;
    case 45: case 41: case 43: i = TOM_LOW; break;
    case 48: case 47: case 50: i = TOM_HIGH; break;
    case 37: i = RIM; break;
    case 70: case 69: i = SHAKER; break;
    default: return;
    }
    Voice& v = m_v[i];
    v.m_age = 0.0;
    v.m_velocity = velocity;
    v.m_phase = v.m_phase2 = 0.0;
    if (i == TOM_LOW || i == TOM_HIGH) v.m_hz = tomHz;
    if (i == CLOSED) m_v[OPEN].m_age = 1e9;   // the closed hat chokes the open one
}

bool DrumKit::active() const
{
    for (const Voice& v : m_v) if (v.m_age < 2.0) return true;
    return false;
}

void DrumKit::process(float* L, float* R, int n)
{
    const double dt = 1.0 / m_sr;
    const float dk = std::max(0.2f, m_s.m_decay);
    for (int i = 0; i < n; ++i) {
        float out[INSTS] = {};
        // Kick: falling sine, click, drive.
        if (Voice& k = m_v[KICK]; k.m_age < 1.5) {
            const double f = m_s.m_kickHz + (170.0 - m_s.m_kickHz) * std::exp(-k.m_age / 0.035);
            k.m_phase += f * dt;
            const float env = static_cast<float>(std::exp(-k.m_age / (0.28 * dk)));
            const float click = k.m_age < 0.002 ? 0.6f * noise() : 0.0f;
            out[KICK] = std::tanh(1.8f * (env * sin01(k.m_phase) + click)) * k.m_velocity;
            k.m_age += dt;
        }
        // Snare: two modes and noise.
        if (Voice& s = m_v[SNARE]; s.m_age < 1.0) {
            s.m_phase += 180.0 * dt;
            s.m_phase2 += 330.0 * dt;
            const float body = static_cast<float>(std::exp(-s.m_age / (0.08 * dk))) * (0.6f * sin01(s.m_phase) + 0.4f * sin01(s.m_phase2));
            float lo, bp, hi;
            s.m_bp.tick(noise(), lo, bp, hi);
            out[SNARE] = (0.6f * body + 0.9f * bp * s.m_bp.m_k * static_cast<float>(std::exp(-s.m_age / (0.16 * dk)))) * s.m_velocity;
            s.m_age += dt;
        }
        // Hats: the six squares, band passed and high passed; the envelopes of the two differ.
        const bool hats = m_v[CLOSED].m_age < 0.5 || m_v[OPEN].m_age < 2.0;
        if (hats) {
            float metal = 0.0f;
            for (int m = 0; m < 6; ++m) {
                m_metal[m] += MetalHz[m] * dt;
                if (m_metal[m] >= 1.0) m_metal[m] -= 1.0;
                metal += m_metal[m] < 0.5 ? 1.0f : -1.0f;
            }
            float lo, bp, hi;
            m_hatBp.tick(metal * (1.0f / 6.0f) + 0.3f * noise(), lo, bp, hi);
            float l2, b2, h2;
            m_hatHp.tick(bp * m_hatBp.m_k, l2, b2, h2);
            if (Voice& c = m_v[CLOSED]; c.m_age < 0.5) { out[CLOSED] = h2 * static_cast<float>(std::exp(-c.m_age / (0.035 * dk))) * c.m_velocity; c.m_age += dt; }
            if (Voice& o = m_v[OPEN]; o.m_age < 2.0) { out[OPEN] = h2 * static_cast<float>(std::exp(-o.m_age / (0.3 * dk))) * o.m_velocity; o.m_age += dt; }
        }
        // Toms: falling onto their pitch.
        for (int t : { TOM_LOW, TOM_HIGH }) {
            Voice& v = m_v[t];
            if (v.m_age >= 1.5) continue;
            const double f = v.m_hz * (1.0 + 0.5 * std::exp(-v.m_age / 0.04));
            v.m_phase += f * dt;
            out[t] = static_cast<float>(std::exp(-v.m_age / (0.35 * dk))) * sin01(v.m_phase) * v.m_velocity;
            v.m_age += dt;
        }
        // Rim: a click and a short tone.
        if (Voice& r = m_v[RIM]; r.m_age < 0.3) {
            r.m_phase += 1700.0 * dt;
            out[RIM] = (0.7f * sin01(r.m_phase) + (r.m_age < 0.001 ? noise() : 0.0f)) * static_cast<float>(std::exp(-r.m_age / 0.018)) * r.m_velocity;
            r.m_age += dt;
        }
        // Shaker: noise with a soft attack.
        if (Voice& sh = m_v[SHAKER]; sh.m_age < 0.6) {
            float lo, bp, hi;
            sh.m_bp.tick(noise(), lo, bp, hi);
            const double env = (1.0 - std::exp(-sh.m_age / 0.006)) * std::exp(-sh.m_age / (0.06 * dk));
            out[SHAKER] = 0.8f * bp * sh.m_bp.m_k * static_cast<float>(env) * sh.m_velocity;
            sh.m_age += dt;
        }
        float l = 0.0f, r = 0.0f;
        for (int k = 0; k < INSTS; ++k) {
            l += out[k] * (0.5f - 0.5f * Pan[k]);
            r += out[k] * (0.5f + 0.5f * Pan[k]);
        }
        L[i] = l;
        R[i] = r;
    }
}

} // namespace eph
