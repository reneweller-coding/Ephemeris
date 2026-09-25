/**
 * @file Spring.cpp
 * @brief Two dispersive springs.
 */
#include "eph/fx/Spring.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
constexpr float kApCoef = 0.62f;                        ///< the all-passes' coefficient (Välimäki et al. use about 0.6)
constexpr double kTransitMs[2] = { 33.0, 41.0 };        ///< the two springs' transit times
}

void Spring::prepare(double sampleRate)
{
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (int s = 0; s < 2; ++s) {
        Tank& t = tanks_[s];
        t.delay = static_cast<int>(kTransitMs[s] * 0.001 * sr_);
        size_t n = 4;
        while (n < static_cast<size_t>(t.delay) + 4) n <<= 1;
        t.line.assign(n, 0.0f);
        t.mask = n - 1;
        t.write = 0;
        std::fill(std::begin(t.ap), std::end(t.ap), 0.0f);
        t.lp.reset();
        t.hp.reset();
        t.y = 0.0f;
    }
    set(2.5f, 4500.0f);
}

void Spring::set(float decaySeconds, float toneHz)
{
    const float sr = static_cast<float>(sr_);
    for (Tank& t : tanks_) {
        // Loop gain for the decay: each pass through the loop loses 60 dB times transit / T60.
        const double transit = static_cast<double>(t.delay) / sr_;
        t.gain = static_cast<float>(std::pow(10.0, -3.0 * transit / std::max(0.2, static_cast<double>(decaySeconds))));
        t.lp.set(std::min(toneHz, 0.45f * sr), 0.0f, sr);
        t.hp.set(90.0f, 0.0f, sr);
    }
}

void Spring::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    // The loop of each spring: delay -> dispersion -> tone -> gain, fed back; the output is the dispersed wave.
    // The 48 all-passes are a chain -- each waits for the one before -- so the two springs go through them
    // side by side, and the processor works on both chains at once. Each stage is two fused multiply-adds.
    Tank& a = tanks_[0];
    Tank& b = tanks_[1];
    for (int i = 0; i < n; ++i) {
        float va = a.line[(a.write - static_cast<size_t>(a.delay)) & a.mask];
        float vb = b.line[(b.write - static_cast<size_t>(b.delay)) & b.mask];
        for (int k = 0; k < kStages; ++k) {
            // First-order all-pass y = c x + s; s' = x - c y (transposed direct form II).
            const float ya = std::fma(kApCoef, va, a.ap[k]);
            const float yb = std::fma(kApCoef, vb, b.ap[k]);
            a.ap[k] = std::fma(-kApCoef, ya, va);
            b.ap[k] = std::fma(-kApCoef, yb, vb);
            va = ya;
            vb = yb;
        }
        float lo, bp, hi;
        a.hp.tick(a.lp.lp(va), lo, bp, hi);
        a.y = hi;
        b.hp.tick(b.lp.lp(vb), lo, bp, hi);
        b.y = hi;
        a.line[a.write] = inL[i] + a.gain * a.y;
        b.line[b.write] = inR[i] + b.gain * b.y;
        a.write = (a.write + 1) & a.mask;
        b.write = (b.write + 1) & b.mask;
        outL[i] += a.y;
        outR[i] += b.y;
    }
}

} // namespace eph
