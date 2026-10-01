/**
 * @file Dynamics.cpp
 * @brief True-peak interpolator and lookahead limiter.
 * @note Copied from Phosphene `Core/src/Dynamics.cpp` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 */
#include "eph/fx/Dynamics.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {
constexpr double kPiD = 3.141592653589793;   ///< pi

/** @brief Modified Bessel function of the first kind, order zero (power series). */
double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k) {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-17 * sum) break;
    }
    return sum;
}
} // namespace

TruePeakInterpolator::TruePeakInterpolator()
{
    // beta = 4 (Kaiser, "Nonrecursive digital filter design using the I0-sinh window function",
    // Proc. IEEE ISCAS 1974). The reasoning for the value is in Dynamics.h: only the passband matters
    // for a fractional-delay filter read at one point, and a larger beta trades passband flatness for a
    // stopband that has nothing to suppress. Measured over 0 .. 0.45 fs, twenty-four taps: beta 4 is
    // flat within 0.14 dB, beta 5 within 0.51 dB, beta 8 (what this was) within 1.79 dB.
    // The window's half width is half the tap span plus half a sample, so the outermost tap of every
    // phase still carries weight rather than being multiplied by an exact zero.
    constexpr double beta = 4.0;
    constexpr double half = kTaps / 2.0 + 0.5;
    for (int k = 0; k < kPhases - 1; ++k) {
        const double t = static_cast<double>(k + 1) / static_cast<double>(kPhases);
        double sum = 0.0;
        for (int m = 0; m < kTaps; ++m) {
            const double u = t - static_cast<double>(m - kHalf + 1);   // distance from the tap's sample
            const double sinc = std::fabs(u) < 1e-12 ? 1.0 : std::sin(kPiD * u) / (kPiD * u);
            const double r = u / half;
            const double w = std::fabs(r) < 1.0 ? besselI0(beta * std::sqrt(1.0 - r * r)) / besselI0(beta) : 0.0;
            h_[k][m] = sinc * w;
            sum += h_[k][m];
        }
        double absSum = 0.0;
        for (int m = 0; m < kTaps; ++m) {
            h_[k][m] /= sum;   // unity at DC; the sums are 1.000000000 before this, so it changes nothing
            absSum += std::fabs(h_[k][m]);
        }
        bound_ = std::max(bound_, absSum);
    }
}

void TruePeakLimiter::prepare(double sampleRate, float lookaheadMs)
{
    sr_ = sampleRate;
    window_ = std::max(8, static_cast<int>(std::lround(lookaheadMs * 0.001 * sampleRate)));
    // Twice the filter's length: every sample is written into both halves, so the window the filter
    // needs is always a contiguous run and process() needs neither a modulo nor a copy per sample.
    histL_.assign(static_cast<size_t>(2 * TruePeakInterpolator::kTaps), 0.0f);
    histR_.assign(static_cast<size_t>(2 * TruePeakInterpolator::kTaps), 0.0f);
    req_.assign(static_cast<size_t>(window_), 1.0);
    dq_.assign(static_cast<size_t>(window_ + 1), 0);
    minRing_.assign(static_cast<size_t>(window_), 1.0);
    // A ring read before it is written delays by its length: exactly the latency.
    delayL_.assign(static_cast<size_t>(latency()), 0.0f);
    delayR_.assign(static_cast<size_t>(latency()), 0.0f);
    set(-1.0f, 80.0f);
    reset();
}

void TruePeakLimiter::reset()
{
    std::fill(histL_.begin(), histL_.end(), 0.0f);
    std::fill(histR_.begin(), histR_.end(), 0.0f);
    histPos_ = 0;
    prevBetween_ = 0.0;
    std::fill(req_.begin(), req_.end(), 1.0);
    dqHead_ = dqTail_ = 0;
    std::fill(minRing_.begin(), minRing_.end(), 1.0);
    minSum_ = static_cast<double>(window_);
    t_ = 0;
    sinceRecompute_ = 0;
    gain_ = 1.0;
    std::fill(delayL_.begin(), delayL_.end(), 0.0f);
    std::fill(delayR_.begin(), delayR_.end(), 0.0f);
    delayPos_ = 0;
    reduction_ = 0.0f;
}

void TruePeakLimiter::set(float ceilingDb, float releaseMs)
{
    ceiling_ = dbToGain(ceilingDb);
    release_ = std::exp(-1.0 / (std::max(1.0f, releaseMs) * 0.001 * sr_));
}

void TruePeakLimiter::process(float* L, float* R, int n)
{
    constexpr int T = TruePeakInterpolator::kTaps;
    constexpr int H = TruePeakInterpolator::kHalf;
    const int W = window_;
    const int D = static_cast<int>(delayL_.size());
    const double bound = interp_.gainBound();
    for (int i = 0; i < n; ++i) {
        // The window the filter reads, oldest first: w[H - 1] is the sample under examination, w[T - 1]
        // the newest. Writing every sample into both halves of the double-length buffer keeps that
        // window contiguous.
        histL_[static_cast<size_t>(histPos_)] = L[i];
        histL_[static_cast<size_t>(histPos_ + T)] = L[i];
        histR_[static_cast<size_t>(histPos_)] = R[i];
        histR_[static_cast<size_t>(histPos_ + T)] = R[i];
        const float* wl = histL_.data() + histPos_ + 1;
        const float* wr = histR_.data() + histPos_ + 1;
        histPos_ = histPos_ + 1 == T ? 0 : histPos_ + 1;

        // The bank is 7 x 24 multiplies per channel, and most of a master's samples cannot reach the
        // ceiling at all. No interpolated point can exceed gainBound() times the largest sample the
        // filter sees, so when that product stays at or below the ceiling the bank is skipped and the
        // interpolated peak recorded as zero. That is exact rather than approximate: every value so
        // dropped is at or below the ceiling, and a peak at or below the ceiling asks for a gain of 1,
        // which is what the maximum below then yields. Measured on an eight-minute render the bank runs
        // for 30 % of the samples.
        double between = 0.0;
        if (static_cast<double>(TruePeakInterpolator::windowPeak(wl + H - 1)) * bound > ceiling_)
            between = interp_.between(wl + H - 1);
        if (static_cast<double>(TruePeakInterpolator::windowPeak(wr + H - 1)) * bound > ceiling_)
            between = std::max(between, interp_.between(wr + H - 1));
        const double sample = std::max(std::fabs(static_cast<double>(wl[H - 1])), std::fabs(static_cast<double>(wr[H - 1])));
        const double peak = std::max({ sample, between, prevBetween_ });
        prevBetween_ = between;
        const double r = peak > ceiling_ ? ceiling_ / peak : 1.0;

        // Sliding minimum over the last W required gains (monotonic deque of positions).
        const int slot = static_cast<int>(t_ % W);
        req_[static_cast<size_t>(slot)] = r;
        const int cap = W + 1;
        while (dqTail_ != dqHead_) {
            const int last = dq_[static_cast<size_t>((dqTail_ - 1 + cap) % cap)];
            if (req_[static_cast<size_t>(last % W)] >= r) dqTail_ = (dqTail_ - 1 + cap) % cap; else break;
        }
        dq_[static_cast<size_t>(dqTail_)] = static_cast<int>(t_ % (static_cast<long long>(W) * 1024));
        dqTail_ = (dqTail_ + 1) % cap;
        // Drop the head once it has left the window (positions kept modulo W * 1024).
        {
            const long long now = t_ % (static_cast<long long>(W) * 1024);
            long long age = now - dq_[static_cast<size_t>(dqHead_)];
            if (age < 0) age += static_cast<long long>(W) * 1024;
            if (age >= W) dqHead_ = (dqHead_ + 1) % cap;
        }
        const double m = req_[static_cast<size_t>(dq_[static_cast<size_t>(dqHead_)] % W)];

        // Moving average of the minima over the same window.
        const double old = minRing_[static_cast<size_t>(slot)];
        minRing_[static_cast<size_t>(slot)] = m;
        minSum_ += m - old;
        if (++sinceRecompute_ >= 65536) {
            sinceRecompute_ = 0;
            minSum_ = 0.0;
            for (double v : minRing_) minSum_ += v;
        }
        const double target = std::min(1.0, minSum_ / W);
        gain_ = target < gain_ ? target : target + (gain_ - target) * release_;
        ++t_;

        // The audio, delayed so that the gain arrives with its peak.
        const float dl = delayL_[static_cast<size_t>(delayPos_)], dr = delayR_[static_cast<size_t>(delayPos_)];
        delayL_[static_cast<size_t>(delayPos_)] = L[i];
        delayR_[static_cast<size_t>(delayPos_)] = R[i];
        delayPos_ = (delayPos_ + 1) % D;
        L[i] = dl * static_cast<float>(gain_);
        R[i] = dr * static_cast<float>(gain_);
    }
    reduction_ = static_cast<float>(-20.0 * std::log10(std::max(gain_, 1e-9)));
}

} // namespace eph
