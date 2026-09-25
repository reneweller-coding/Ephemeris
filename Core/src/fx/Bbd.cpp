/**
 * @file Bbd.cpp
 * @brief The bucket-brigade delay (Bbd.h).
 */
#include "eph/fx/Bbd.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
constexpr double kStages = 4096.0;   ///< the MN3005's buckets
}

void BbdEcho::prepare(double sampleRate, double maxSeconds, uint64_t seed)
{
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    size_t n = 4;
    while (static_cast<double>(n) < maxSeconds * sr_ + 16.0) n <<= 1;
    bufL_.assign(n, 0.0f);
    bufR_.assign(n, 0.0f);
    mask_ = n - 1;
    rng_.seed(seed);
    delay_.setTime(0.25f, sr_);
    reset();
}

void BbdEcho::reset()
{
    std::fill(bufL_.begin(), bufL_.end(), 0.0f);
    std::fill(bufR_.begin(), bufR_.end(), 0.0f);
    write_ = 0;
    fresh_ = true;
    lfo_ = 0.0;
    for (Svf* f : { &lp1L_, &lp2L_, &lp1R_, &lp2R_, &hpL_, &hpR_ }) f->reset();
}

void BbdEcho::set(const EchoSettings& s)
{
    s_ = s;
    const double maxDelay = static_cast<double>(mask_) - 8.0;
    target_ = std::clamp(s.delaySeconds * sr_, 4.0, maxDelay);
    if (fresh_) { delay_.snap(static_cast<float>(target_)); fresh_ = false; }
    // The clock for this delay, and the filters at a third of it: longer delays are darker.
    const double clock = kStages / (2.0 * std::max(0.01, s.delaySeconds));
    const float sr = static_cast<float>(sr_);
    const float cutoff = static_cast<float>(std::min({ static_cast<double>(s.toneHz), clock / 3.0, 0.45 * sr_ }));
    lp1L_.setQ(cutoff, 0.54f, sr); lp2L_.setQ(cutoff, 1.31f, sr);   // a fourth-order Butterworth
    lp1R_.copyCoefficients(lp1L_); lp2R_.copyCoefficients(lp2L_);
    hpL_.set(60.0f, 0.0f, sr); hpR_.copyCoefficients(hpL_);
    drive_ = dbToGain(s.driveDb);
    driveNorm_ = 1.0f / drive_;
    hiss_ = 0.00015f * (1.0f + s.feedback);   // the compander's floor, a little more with more repeats
}

float BbdEcho::read(const std::vector<float>& buf, double d) const
{
    const double pos = static_cast<double>(write_) - d;
    const double fl = std::floor(pos);
    const float t = static_cast<float>(pos - fl);
    const size_t i = static_cast<size_t>(static_cast<int64_t>(fl)) & mask_;
    const float y0 = buf[(i - 1) & mask_], y1 = buf[i], y2 = buf[(i + 1) & mask_], y3 = buf[(i + 2) & mask_];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
}

void BbdEcho::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    for (int i = 0; i < n; ++i) {
        // The clock's slow wander (0.4 Hz), a chorus in the repeats rather than a tape's wow.
        lfo_ += 0.4 / sr_;
        if (lfo_ >= 1.0) lfo_ -= 1.0;
        const double wander = 0.001 * sr_ * static_cast<double>(s_.wowMs) * sin01(lfo_);
        const double d = std::max(4.0, static_cast<double>(delay_.next(static_cast<float>(target_))) + wander);
        float yl = read(bufL_, d), yr = read(bufR_, d);
        // Reconstruction filter on the way out of the chain.
        yl = lp2L_.lp(lp1L_.lp(yl));
        yr = lp2R_.lp(lp1R_.lp(yr));
        float lo, bo, hl, hr;
        hpL_.tick(yl, lo, bo, hl);
        hpR_.tick(yr, lo, bo, hr);
        // The compander's saturation and hiss in the loop.
        const float ll = std::tanh(drive_ * hl) * driveNorm_ + hiss_ * rng_.bipolar();
        const float lr = std::tanh(drive_ * hr) * driveNorm_ + hiss_ * rng_.bipolar();
        const float fb = s_.feedback;
        if (s_.pingPong) {
            bufL_[write_] = 0.5f * (inL[i] + inR[i]) + fb * lr;
            bufR_[write_] = fb * ll;
        } else {
            bufL_[write_] = inL[i] + fb * ll;
            bufR_[write_] = inR[i] + fb * lr;
        }
        write_ = (write_ + 1) & mask_;
        outL[i] += yl;
        outR[i] += yr;
    }
}

} // namespace eph
