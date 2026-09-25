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

float Spring::tick(Tank& t, float x)
{
    // The loop: delay -> dispersion -> tone -> gain, fed back; the output is the dispersed wave.
    float v = t.line[(t.write - static_cast<size_t>(t.delay)) & t.mask];
    for (int i = 0; i < kStages; ++i) {
        // First-order all-pass y = a x + s; s' = x - a y (transposed direct form II).
        const float y = kApCoef * v + t.ap[i];
        t.ap[i] = v - kApCoef * y;
        v = y;
    }
    float lo, bp, hi;
    t.hp.tick(t.lp.lp(v), lo, bp, hi);
    t.y = hi;
    t.line[t.write] = x + t.gain * t.y;
    t.write = (t.write + 1) & t.mask;
    return t.y;
}

void Spring::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    for (int i = 0; i < n; ++i) {
        outL[i] += tick(tanks_[0], inL[i]);
        outR[i] += tick(tanks_[1], inR[i]);
    }
}

} // namespace eph
