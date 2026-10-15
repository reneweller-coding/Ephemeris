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
constexpr double PiD = 3.141592653589793;   ///< pi

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
    constexpr double half = Taps / 2.0 + 0.5;
    for (int k = 0; k < Phases - 1; ++k) {
        const double t = static_cast<double>(k + 1) / static_cast<double>(Phases);
        double sum = 0.0;
        for (int m = 0; m < Taps; ++m) {
            const double u = t - static_cast<double>(m - Half + 1);   // distance from the tap's sample
            const double sinc = std::fabs(u) < 1e-12 ? 1.0 : std::sin(PiD * u) / (PiD * u);
            const double r = u / half;
            const double w = std::fabs(r) < 1.0 ? besselI0(beta * std::sqrt(1.0 - r * r)) / besselI0(beta) : 0.0;
            m_h[k][m] = sinc * w;
            sum += m_h[k][m];
        }
        double absSum = 0.0;
        for (int m = 0; m < Taps; ++m) {
            m_h[k][m] /= sum;   // unity at DC; the sums are 1.000000000 before this, so it changes nothing
            absSum += std::fabs(m_h[k][m]);
        }
        m_bound = std::max(m_bound, absSum);
    }
}

void TruePeakLimiter::prepare(double sampleRate, float lookaheadMs)
{
    m_sr = sampleRate;
    m_window = std::max(8, static_cast<int>(std::lround(lookaheadMs * 0.001 * sampleRate)));
    // Twice the filter's length: every sample is written into both halves, so the window the filter
    // needs is always a contiguous run and process() needs neither a modulo nor a copy per sample.
    m_histL.assign(static_cast<size_t>(2 * TruePeakInterpolator::Taps), 0.0f);
    m_histR.assign(static_cast<size_t>(2 * TruePeakInterpolator::Taps), 0.0f);
    m_req.assign(static_cast<size_t>(m_window), 1.0);
    m_dq.assign(static_cast<size_t>(m_window + 1), 0);
    m_minRing.assign(static_cast<size_t>(m_window), 1.0);
    // A ring read before it is written delays by its length: exactly the latency.
    m_delayL.assign(static_cast<size_t>(latency()), 0.0f);
    m_delayR.assign(static_cast<size_t>(latency()), 0.0f);
    set(-1.0f, 80.0f);
    reset();
}

void TruePeakLimiter::reset()
{
    std::fill(m_histL.begin(), m_histL.end(), 0.0f);
    std::fill(m_histR.begin(), m_histR.end(), 0.0f);
    m_histPos = 0;
    m_prevBetween = 0.0;
    std::fill(m_req.begin(), m_req.end(), 1.0);
    m_dqHead = m_dqTail = 0;
    std::fill(m_minRing.begin(), m_minRing.end(), 1.0);
    m_minSum = static_cast<double>(m_window);
    m_t = 0;
    m_sinceRecompute = 0;
    m_gain = 1.0;
    std::fill(m_delayL.begin(), m_delayL.end(), 0.0f);
    std::fill(m_delayR.begin(), m_delayR.end(), 0.0f);
    m_delayPos = 0;
    m_reduction = 0.0f;
}

void TruePeakLimiter::set(float ceilingDb, float releaseMs)
{
    m_ceiling = dbToGain(ceilingDb);
    m_release = std::exp(-1.0 / (std::max(1.0f, releaseMs) * 0.001 * m_sr));
}

void TruePeakLimiter::process(float* L, float* R, int n)
{
    constexpr int T = TruePeakInterpolator::Taps;
    constexpr int H = TruePeakInterpolator::Half;
    const int W = m_window;
    const int D = static_cast<int>(m_delayL.size());
    const double bound = m_interp.gainBound();
    for (int i = 0; i < n; ++i) {
        // The window the filter reads, oldest first: w[H - 1] is the sample under examination, w[T - 1]
        // the newest. Writing every sample into both halves of the double-length buffer keeps that
        // window contiguous.
        m_histL[static_cast<size_t>(m_histPos)] = L[i];
        m_histL[static_cast<size_t>(m_histPos + T)] = L[i];
        m_histR[static_cast<size_t>(m_histPos)] = R[i];
        m_histR[static_cast<size_t>(m_histPos + T)] = R[i];
        const float* wl = m_histL.data() + m_histPos + 1;
        const float* wr = m_histR.data() + m_histPos + 1;
        m_histPos = m_histPos + 1 == T ? 0 : m_histPos + 1;

        // The bank is 7 x 24 multiplies per channel, and most of a master's samples cannot reach the
        // ceiling at all. No interpolated point can exceed gainBound() times the largest sample the
        // filter sees, so when that product stays at or below the ceiling the bank is skipped and the
        // interpolated peak recorded as zero. That is exact rather than approximate: every value so
        // dropped is at or below the ceiling, and a peak at or below the ceiling asks for a gain of 1,
        // which is what the maximum below then yields. Measured on an eight-minute render the bank runs
        // for 30 % of the samples.
        double between = 0.0;
        if (static_cast<double>(TruePeakInterpolator::windowPeak(wl + H - 1)) * bound > m_ceiling)
            between = m_interp.between(wl + H - 1);
        if (static_cast<double>(TruePeakInterpolator::windowPeak(wr + H - 1)) * bound > m_ceiling)
            between = std::max(between, m_interp.between(wr + H - 1));
        const double sample = std::max(std::fabs(static_cast<double>(wl[H - 1])), std::fabs(static_cast<double>(wr[H - 1])));
        const double peak = std::max({ sample, between, m_prevBetween });
        m_prevBetween = between;
        const double r = peak > m_ceiling ? m_ceiling / peak : 1.0;

        // Sliding minimum over the last W required gains (monotonic deque of positions).
        const int slot = static_cast<int>(m_t % W);
        m_req[static_cast<size_t>(slot)] = r;
        const int cap = W + 1;
        while (m_dqTail != m_dqHead) {
            const int last = m_dq[static_cast<size_t>((m_dqTail - 1 + cap) % cap)];
            if (m_req[static_cast<size_t>(last % W)] >= r) m_dqTail = (m_dqTail - 1 + cap) % cap; else break;
        }
        m_dq[static_cast<size_t>(m_dqTail)] = static_cast<int>(m_t % (static_cast<long long>(W) * 1024));
        m_dqTail = (m_dqTail + 1) % cap;
        // Drop the head once it has left the window (positions kept modulo W * 1024).
        {
            const long long now = m_t % (static_cast<long long>(W) * 1024);
            long long age = now - m_dq[static_cast<size_t>(m_dqHead)];
            if (age < 0) age += static_cast<long long>(W) * 1024;
            if (age >= W) m_dqHead = (m_dqHead + 1) % cap;
        }
        const double m = m_req[static_cast<size_t>(m_dq[static_cast<size_t>(m_dqHead)] % W)];

        // Moving average of the minima over the same window.
        const double old = m_minRing[static_cast<size_t>(slot)];
        m_minRing[static_cast<size_t>(slot)] = m;
        m_minSum += m - old;
        if (++m_sinceRecompute >= 65536) {
            m_sinceRecompute = 0;
            m_minSum = 0.0;
            for (double v : m_minRing) m_minSum += v;
        }
        const double target = std::min(1.0, m_minSum / W);
        m_gain = target < m_gain ? target : target + (m_gain - target) * m_release;
        ++m_t;

        // The audio, delayed so that the gain arrives with its peak.
        const float dl = m_delayL[static_cast<size_t>(m_delayPos)], dr = m_delayR[static_cast<size_t>(m_delayPos)];
        m_delayL[static_cast<size_t>(m_delayPos)] = L[i];
        m_delayR[static_cast<size_t>(m_delayPos)] = R[i];
        m_delayPos = (m_delayPos + 1) % D;
        L[i] = dl * static_cast<float>(m_gain);
        R[i] = dr * static_cast<float>(m_gain);
    }
    m_reduction = static_cast<float>(-20.0 * std::log10(std::max(m_gain, 1e-9)));
}

} // namespace eph
