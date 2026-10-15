/**
 * @file Spring.cpp
 * @brief Two dispersive springs.
 */
#include "eph/fx/Spring.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
constexpr float ApCoef = 0.62f;                        ///< the all-passes' coefficient (Välimäki et al. use about 0.6)
constexpr double TransitMs[2] = { 33.0, 41.0 };        ///< the two springs' transit times
}

void Spring::prepare(double sampleRate)
{
    m_sr = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (int s = 0; s < 2; ++s) {
        Tank& t = m_tanks[s];
        t.m_delay = static_cast<int>(TransitMs[s] * 0.001 * m_sr);
        size_t n = 4;
        while (n < static_cast<size_t>(t.m_delay) + 4) n <<= 1;
        t.m_line.assign(n, 0.0f);
        t.m_mask = n - 1;
        t.m_write = 0;
        std::fill(std::begin(t.m_ap), std::end(t.m_ap), 0.0f);
        t.m_lp.reset();
        t.m_hp.reset();
        t.m_y = 0.0f;
    }
    set(2.5f, 4500.0f);
}

void Spring::set(float decaySeconds, float toneHz)
{
    const float sr = static_cast<float>(m_sr);
    for (Tank& t : m_tanks) {
        // Loop gain for the decay: each pass through the loop loses 60 dB times transit / T60.
        const double transit = static_cast<double>(t.m_delay) / m_sr;
        t.m_gain = static_cast<float>(std::pow(10.0, -3.0 * transit / std::max(0.2, static_cast<double>(decaySeconds))));
        t.m_lp.set(std::min(toneHz, 0.45f * sr), 0.0f, sr);
        t.m_hp.set(90.0f, 0.0f, sr);
    }
}

void Spring::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    // The loop of each spring: delay -> dispersion -> tone -> gain, fed back; the output is the dispersed wave.
    // The 48 all-passes are a chain -- each waits for the one before -- so the two springs go through them
    // side by side, and the processor works on both chains at once. Each stage is two fused multiply-adds.
    Tank& a = m_tanks[0];
    Tank& b = m_tanks[1];
    for (int i = 0; i < n; ++i) {
        float va = a.m_line[(a.m_write - static_cast<size_t>(a.m_delay)) & a.m_mask];
        float vb = b.m_line[(b.m_write - static_cast<size_t>(b.m_delay)) & b.m_mask];
        for (int k = 0; k < Stages; ++k) {
            // First-order all-pass y = c x + s; s' = x - c y (transposed direct form II).
            const float ya = std::fma(ApCoef, va, a.m_ap[k]);
            const float yb = std::fma(ApCoef, vb, b.m_ap[k]);
            a.m_ap[k] = std::fma(-ApCoef, ya, va);
            b.m_ap[k] = std::fma(-ApCoef, yb, vb);
            va = ya;
            vb = yb;
        }
        float lo, bp, hi;
        a.m_hp.tick(a.m_lp.lp(va), lo, bp, hi);
        a.m_y = hi;
        b.m_hp.tick(b.m_lp.lp(vb), lo, bp, hi);
        b.m_y = hi;
        a.m_line[a.m_write] = inL[i] + a.m_gain * a.m_y;
        b.m_line[b.m_write] = inR[i] + b.m_gain * b.m_y;
        a.m_write = (a.m_write + 1) & a.m_mask;
        b.m_write = (b.m_write + 1) & b.m_mask;
        outL[i] += a.m_y;
        outR[i] += b.m_y;
    }
}

} // namespace eph
