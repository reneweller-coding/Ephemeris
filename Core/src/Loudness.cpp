/**
 * @file Loudness.cpp
 * @brief EBU R128 loudness, true peak and the stereo figures, offline.
 * @author Rene Weller
 */
#include "eph/Loudness.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {

constexpr double PiD = 3.14159265358979323846;   ///< pi

/**
 * @brief A K-weighted mean square in LUFS, -120 for silence.
 * @param[in] meanSquareSum  the K-weighted mean square
 * @return the loudness in LUFS, -120 for silence
 */
double lufs(double meanSquareSum) { return meanSquareSum > 0.0 ? -0.691 + 10.0 * std::log10(meanSquareSum) : -120.0; }

} // namespace

void LoudnessMeter::prepare(double sampleRate)
{
    m_sr = sampleRate;
    m_hopLength = std::max(1, static_cast<int>(std::lround(0.1 * sampleRate)));
    // The two K-weighting stages of BS.1770-4, designed for any rate (the 48 kHz coefficients of the standard
    // come out of these formulas): a high shelf of +4 dB above 1.7 kHz, a high pass at 38 Hz.
    for (int c = 0; c < 2; ++c) {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(PiD * f0 / sampleRate), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            Biquad& b = m_shelf[c];
            b = Biquad{};
            b.m_b0 = (Vh + Vb * K / Q + K * K) / a0;
            b.m_b1 = 2.0 * (K * K - Vh) / a0;
            b.m_b2 = (Vh - Vb * K / Q + K * K) / a0;
            b.m_a1 = 2.0 * (K * K - 1.0) / a0;
            b.m_a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(PiD * f0 / sampleRate), a0 = 1.0 + K / Q + K * K;
            Biquad& b = m_highPass[c];
            b = Biquad{};
            b.m_b0 = 1.0; b.m_b1 = -2.0; b.m_b2 = 1.0;
            b.m_a1 = 2.0 * (K * K - 1.0) / a0;
            b.m_a2 = (1.0 - K / Q + K * K) / a0;
        }
    }
    m_hops.clear();
    m_cur = Hop{};
    m_histL.assign(2 * TruePeakInterpolator::Taps, 0.0f);
    m_histR.assign(2 * TruePeakInterpolator::Taps, 0.0f);
    m_pos = 0;
}

void LoudnessMeter::truePeak(std::vector<float>& hist, float x)
{
    // A window of Taps samples, contiguous thanks to the doubled buffer; the interpolator looks between its
    // middle two samples.
    constexpr int T = TruePeakInterpolator::Taps, H = TruePeakInterpolator::Half;
    hist[static_cast<size_t>(m_pos)] = x;
    hist[static_cast<size_t>(m_pos + T)] = x;
    const float* w = hist.data() + m_pos + 1;   // oldest .. newest: w[0] .. w[T - 1]
    const float* mid = w + H - 1;
    m_cur.m_peak = std::max(m_cur.m_peak, std::fabs(x));
    if (TruePeakInterpolator::windowPeak(mid) * m_tp.gainBound() <= m_cur.m_peak) return;   // cannot beat it: exact
    m_cur.m_peak = std::max(m_cur.m_peak, static_cast<float>(m_tp.between(mid)));
}

void LoudnessMeter::process(const float* L, const float* R, int n)
{
    constexpr int T = TruePeakInterpolator::Taps;
    for (int i = 0; i < n; ++i) {
        const double l = L[i], r = R[i];
        const double kl = m_highPass[0].process(m_shelf[0].process(l));
        const double kr = m_highPass[1].process(m_shelf[1].process(r));
        m_cur.m_kL += kl * kl;
        m_cur.m_kR += kr * kr;
        m_cur.m_lr += l * r;
        m_cur.m_ll += l * l;
        m_cur.m_rr += r * r;
        const double m = 0.5 * (l + r), s = 0.5 * (l - r);
        m_cur.m_mid += m * m;
        m_cur.m_side += s * s;
        m_pos = (m_pos + 1) % T;
        truePeak(m_histL, L[i]);
        truePeak(m_histR, R[i]);
        if (++m_cur.m_samples == m_hopLength) {
            m_hops.push_back(m_cur);
            m_cur = Hop{};
        }
    }
}

LoudnessReport LoudnessMeter::report() const
{
    LoudnessReport r;
    const size_t n = m_hops.size();
    r.m_seconds = static_cast<double>(n) * 0.1;
    if (n == 0) return r;
    // Mean square sums (left plus right, both weighted 1) of windows of w hops ending at hop j.
    auto window = [&](size_t j, size_t w) {
        double sum = 0.0;
        int samples = 0;
        for (size_t k = j + 1 - w; k <= j; ++k) { sum += m_hops[k].m_kL + m_hops[k].m_kR; samples += m_hops[k].m_samples; }
        return samples > 0 ? sum / samples : 0.0;
    };
    // Integrated: 400 ms blocks every 100 ms, gated at -70 LUFS and 10 LU under the mean of what passed.
    std::vector<double> blocks;
    for (size_t j = 3; j < n; ++j) blocks.push_back(window(j, 4));
    auto gatedMean = [](const std::vector<double>& v, double gateLufs) {
        double sum = 0.0;
        int count = 0;
        for (double z : v) if (lufs(z) > gateLufs) { sum += z; ++count; }
        return count > 0 ? sum / count : 0.0;
    };
    if (!blocks.empty()) {
        const double abs = gatedMean(blocks, -70.0);
        r.m_integrated = lufs(gatedMean(blocks, std::max(-70.0, lufs(abs) - 10.0)));
    }
    // Short-term: 3 s every 100 ms; the loudest, its PSR, and the loudness range.
    std::vector<double> shortTerm;
    size_t loudest = 0;
    double best = -1.0;
    for (size_t j = 29; j < n; ++j) {
        const double z = window(j, 30);
        shortTerm.push_back(z);
        if (z > best) { best = z; loudest = j; }
    }
    float peak = 0.0f;
    for (const Hop& h : m_hops) peak = std::max(peak, h.m_peak);
    r.m_truePeak = peak > 0.0f ? 20.0 * std::log10(peak) : -120.0;
    if (!shortTerm.empty()) {
        const double st = window(loudest, 30);
        r.m_shortTermMax = lufs(st);
        float p3 = 0.0f;
        for (size_t k = loudest - 29; k <= loudest; ++k) p3 = std::max(p3, m_hops[k].m_peak);
        r.m_psr = (p3 > 0.0f ? 20.0 * std::log10(p3) : -120.0) - r.m_shortTermMax;
        std::vector<double> gated;
        const double rel = lufs(gatedMean(shortTerm, -70.0)) - 20.0;
        for (double z : shortTerm) if (lufs(z) > -70.0 && lufs(z) > rel) gated.push_back(lufs(z));
        if (gated.size() > 1) {
            std::sort(gated.begin(), gated.end());
            auto centile = [&](double q) { return gated[static_cast<size_t>(std::lround(q * static_cast<double>(gated.size() - 1)))]; };
            r.m_range = centile(0.95) - centile(0.10);
        }
    } else {
        r.m_shortTermMax = r.m_integrated;
    }
    r.m_plr = r.m_truePeak - r.m_integrated;
    {
        double ms = 0.0;
        int samples = 0;
        for (const Hop& h : m_hops) { ms += h.m_ll + h.m_rr; samples += h.m_samples; }
        ms /= std::max(1, 2 * samples);
        r.m_crest = r.m_truePeak - (ms > 0.0 ? 10.0 * std::log10(ms) : -120.0);
    }
    // Stereo: the correlation over the whole and over each second, the side under the mid.
    double lr = 0.0, ll = 0.0, rr = 0.0, mid = 0.0, side = 0.0;
    for (const Hop& h : m_hops) { lr += h.m_lr; ll += h.m_ll; rr += h.m_rr; mid += h.m_mid; side += h.m_side; }
    r.m_correlation = ll > 0.0 && rr > 0.0 ? lr / std::sqrt(ll * rr) : 1.0;
    r.m_sideUnderMid = side > 0.0 && mid > 0.0 ? 10.0 * std::log10(mid / side) : (side > 0.0 ? -120.0 : 120.0);
    for (size_t j = 9; j < n; j += 10) {
        double a = 0.0, b = 0.0, c = 0.0;
        for (size_t k = j - 9; k <= j; ++k) { a += m_hops[k].m_lr; b += m_hops[k].m_ll; c += m_hops[k].m_rr; }
        const double samples = 0.5 * static_cast<double>(m_hopLength) * 10.0;
        if (b / samples < 1e-5 || c / samples < 1e-5) continue;   // a second under -50 dBFS (a last tail) says nothing
        if (a / std::sqrt(b * c) < r.m_correlationLow) {
            r.m_correlationLow = a / std::sqrt(b * c);
            r.m_correlationLowAt = static_cast<double>(j - 9) * 0.1;
        }
    }
    return r;
}

} // namespace eph
