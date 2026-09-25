/**
 * @file TapeEcho.cpp
 * @brief The tape echo.
 */
#include "eph/fx/TapeEcho.h"
#include <algorithm>
#include <cmath>

namespace eph {

void TapeEcho::prepare(double sampleRate, double maxSeconds, uint64_t seed)
{
    sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    size_t n = 4;
    while (static_cast<double>(n) < (maxSeconds + 0.05) * sr_ + 8.0) n <<= 1;
    bufL_.assign(n, 0.0f);
    bufR_.assign(n, 0.0f);
    mask_ = n - 1;
    rng_.seed(seed);
    delay_.setTime(0.12f, sr_);   // the tape takes about a tenth of a second to change speed
    reset();
}

void TapeEcho::reset()
{
    std::fill(bufL_.begin(), bufL_.end(), 0.0f);
    std::fill(bufR_.begin(), bufR_.end(), 0.0f);
    write_ = 0;
    wowPhase_ = flutterPhase_ = 0.0;
    drift_ = driftTarget_ = 0.0;
    count_ = 0;
    fresh_ = true;
    lpL_.reset(); lpR_.reset(); hpL_.reset(); hpR_.reset();
}

void TapeEcho::set(const EchoSettings& s)
{
    s_ = s;
    const double maxDelay = static_cast<double>(mask_) - 8.0;
    target_ = std::clamp(s.delaySeconds * sr_, 4.0, maxDelay);
    if (fresh_) { delay_.snap(static_cast<float>(target_)); fresh_ = false; }
    const float sr = static_cast<float>(sr_);
    lpL_.set(s.toneHz, 0.0f, sr); lpR_.copyCoefficients(lpL_);
    hpL_.set(s.lowCutHz, 0.0f, sr);    hpR_.copyCoefficients(hpL_);
    drive_ = dbToGain(s.driveDb);
    driveNorm_ = 1.0f / drive_;
}

float TapeEcho::read(const std::vector<float>& buf, double d) const
{
    // Four-point Hermite around the fractional read position.
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

void TapeEcho::process(const float* inL, const float* inR, float* outL, float* outR, int n)
{
    const double wowHz = 0.7, flutterHz = 9.0;
    for (int i = 0; i < n; ++i) {
        if ((count_ & 31) == 0) {
            // A new drift target every ~0.7 s, approached slowly: the part of wow that is not periodic.
            if ((count_ % 32768) == 0) driftTarget_ = static_cast<double>(rng_.bipolar());
            drift_ += (driftTarget_ - drift_) * 0.002;
        }
        ++count_;
        wowPhase_ += wowHz / sr_;
        if (wowPhase_ >= 1.0) wowPhase_ -= 1.0;
        flutterPhase_ += flutterHz / sr_;
        if (flutterPhase_ >= 1.0) flutterPhase_ -= 1.0;
        const double wander = 0.001 * sr_ * (static_cast<double>(s_.wowMs) * (0.7 * sin01(wowPhase_) + 0.3 * drift_)
                                             + static_cast<double>(s_.flutterMs) * sin01(flutterPhase_));
        const double d = std::max(4.0, static_cast<double>(delay_.next(static_cast<float>(target_))) + wander);
        const float yl = read(bufL_, d), yr = read(bufR_, d);
        // The loop: tone, rumble filter, saturation, gain.
        float lo, bo, hl, hr;
        hpL_.tick(lpL_.lp(yl), lo, bo, hl);
        hpR_.tick(lpR_.lp(yr), lo, bo, hr);
        const float ll = std::tanh(drive_ * hl) * driveNorm_;
        const float lr = std::tanh(drive_ * hr) * driveNorm_;
        const float fb = s_.feedback;
        const float inMono = 0.5f * (inL[i] + inR[i]);
        if (s_.pingPong) {
            bufL_[write_] = inMono + fb * lr;
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
