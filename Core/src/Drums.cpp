/**
 * @file Drums.cpp
 * @brief The analogue kit.
 */
#include "eph/Drums.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
constexpr double kMetalHz[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };   ///< the TR-808's hat oscillators
constexpr float kPan[8] = { 0.0f, 0.05f, 0.3f, 0.3f, -0.4f, 0.4f, -0.2f, 0.5f }; ///< kick .. shaker
}

void DrumKit::prepare(double sampleRate, uint64_t seed)
{
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    rng_.seed(seed);
    for (Voice& v : v_) v = Voice{};
    for (double& m : metal_) m = 0.0;
    hatBp_.reset(); hatHp_.reset();
    set(s_);
}

void DrumKit::set(const DrumSettings& s)
{
    s_ = s;
    const float sr = static_cast<float>(sr_);
    hatBp_.setQ(std::min(0.45f * sr, 7000.0f + 4000.0f * s.tone), 1.2f, sr);
    hatHp_.setQ(std::min(0.45f * sr, 6000.0f + 2000.0f * s.tone), 0.7f, sr);
    v_[Snare].bp.setQ(std::min(0.45f * sr, 2500.0f + 3000.0f * s.tone), 0.8f, sr);
    v_[Shaker].bp.setQ(std::min(0.45f * sr, 6000.0f), 1.5f, sr);
}

void DrumKit::hit(int note, float velocity, float tomHz)
{
    Inst i;
    switch (note) {
    case 36: i = Kick; break;
    case 38: case 40: i = Snare; break;
    case 42: case 44: i = Closed; break;
    case 46: i = Open; break;
    case 45: case 41: case 43: i = TomLow; break;
    case 48: case 47: case 50: i = TomHigh; break;
    case 37: i = Rim; break;
    case 70: case 69: i = Shaker; break;
    default: return;
    }
    Voice& v = v_[i];
    v.age = 0.0;
    v.velocity = velocity;
    v.phase = v.phase2 = 0.0;
    if (i == TomLow || i == TomHigh) v.hz = tomHz;
    if (i == Closed) v_[Open].age = 1e9;   // the closed hat chokes the open one
}

bool DrumKit::active() const
{
    for (const Voice& v : v_) if (v.age < 2.0) return true;
    return false;
}

void DrumKit::process(float* L, float* R, int n)
{
    const double dt = 1.0 / sr_;
    const float dk = std::max(0.2f, s_.decay);
    for (int i = 0; i < n; ++i) {
        float out[kInsts] = {};
        // Kick: falling sine, click, drive.
        if (Voice& k = v_[Kick]; k.age < 1.5) {
            const double f = s_.kickHz + (170.0 - s_.kickHz) * std::exp(-k.age / 0.035);
            k.phase += f * dt;
            const float env = static_cast<float>(std::exp(-k.age / (0.28 * dk)));
            const float click = k.age < 0.002 ? 0.6f * noise() : 0.0f;
            out[Kick] = std::tanh(1.8f * (env * sin01(k.phase) + click)) * k.velocity;
            k.age += dt;
        }
        // Snare: two modes and noise.
        if (Voice& s = v_[Snare]; s.age < 1.0) {
            s.phase += 180.0 * dt;
            s.phase2 += 330.0 * dt;
            const float body = static_cast<float>(std::exp(-s.age / (0.08 * dk))) * (0.6f * sin01(s.phase) + 0.4f * sin01(s.phase2));
            float lo, bp, hi;
            s.bp.tick(noise(), lo, bp, hi);
            out[Snare] = (0.6f * body + 0.9f * bp * s.bp.k * static_cast<float>(std::exp(-s.age / (0.16 * dk)))) * s.velocity;
            s.age += dt;
        }
        // Hats: the six squares, band passed and high passed; the envelopes of the two differ.
        const bool hats = v_[Closed].age < 0.5 || v_[Open].age < 2.0;
        if (hats) {
            float metal = 0.0f;
            for (int m = 0; m < 6; ++m) {
                metal_[m] += kMetalHz[m] * dt;
                if (metal_[m] >= 1.0) metal_[m] -= 1.0;
                metal += metal_[m] < 0.5 ? 1.0f : -1.0f;
            }
            float lo, bp, hi;
            hatBp_.tick(metal * (1.0f / 6.0f) + 0.3f * noise(), lo, bp, hi);
            float l2, b2, h2;
            hatHp_.tick(bp * hatBp_.k, l2, b2, h2);
            if (Voice& c = v_[Closed]; c.age < 0.5) { out[Closed] = h2 * static_cast<float>(std::exp(-c.age / (0.035 * dk))) * c.velocity; c.age += dt; }
            if (Voice& o = v_[Open]; o.age < 2.0) { out[Open] = h2 * static_cast<float>(std::exp(-o.age / (0.3 * dk))) * o.velocity; o.age += dt; }
        }
        // Toms: falling onto their pitch.
        for (int t : { TomLow, TomHigh }) {
            Voice& v = v_[t];
            if (v.age >= 1.5) continue;
            const double f = v.hz * (1.0 + 0.5 * std::exp(-v.age / 0.04));
            v.phase += f * dt;
            out[t] = static_cast<float>(std::exp(-v.age / (0.35 * dk))) * sin01(v.phase) * v.velocity;
            v.age += dt;
        }
        // Rim: a click and a short tone.
        if (Voice& r = v_[Rim]; r.age < 0.3) {
            r.phase += 1700.0 * dt;
            out[Rim] = (0.7f * sin01(r.phase) + (r.age < 0.001 ? noise() : 0.0f)) * static_cast<float>(std::exp(-r.age / 0.018)) * r.velocity;
            r.age += dt;
        }
        // Shaker: noise with a soft attack.
        if (Voice& sh = v_[Shaker]; sh.age < 0.6) {
            float lo, bp, hi;
            sh.bp.tick(noise(), lo, bp, hi);
            const double env = (1.0 - std::exp(-sh.age / 0.006)) * std::exp(-sh.age / (0.06 * dk));
            out[Shaker] = 0.8f * bp * sh.bp.k * static_cast<float>(env) * sh.velocity;
            sh.age += dt;
        }
        float l = 0.0f, r = 0.0f;
        for (int k = 0; k < kInsts; ++k) {
            l += out[k] * (0.5f - 0.5f * kPan[k]);
            r += out[k] * (0.5f + 0.5f * kPan[k]);
        }
        L[i] = l;
        R[i] = r;
    }
}

} // namespace eph
