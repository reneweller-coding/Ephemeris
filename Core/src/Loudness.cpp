/**
 * @file Loudness.cpp
 * @brief EBU R128 loudness, true peak and the stereo figures, offline.
 */
#include "eph/Loudness.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {

constexpr double kPiD = 3.14159265358979323846;   ///< pi

/** @brief A K-weighted mean square in LUFS, -120 for silence. */
double lufs(double meanSquareSum) { return meanSquareSum > 0.0 ? -0.691 + 10.0 * std::log10(meanSquareSum) : -120.0; }

} // namespace

void LoudnessMeter::prepare(double sampleRate)
{
    sr_ = sampleRate;
    hopLength_ = std::max(1, static_cast<int>(std::lround(0.1 * sampleRate)));
    // The two K-weighting stages of BS.1770-4, designed for any rate (the 48 kHz coefficients of the standard
    // come out of these formulas): a high shelf of +4 dB above 1.7 kHz, a high pass at 38 Hz.
    for (int c = 0; c < 2; ++c) {
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(kPiD * f0 / sampleRate), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            Biquad& b = shelf_[c];
            b = Biquad{};
            b.b0 = (Vh + Vb * K / Q + K * K) / a0;
            b.b1 = 2.0 * (K * K - Vh) / a0;
            b.b2 = (Vh - Vb * K / Q + K * K) / a0;
            b.a1 = 2.0 * (K * K - 1.0) / a0;
            b.a2 = (1.0 - K / Q + K * K) / a0;
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(kPiD * f0 / sampleRate), a0 = 1.0 + K / Q + K * K;
            Biquad& b = highPass_[c];
            b = Biquad{};
            b.b0 = 1.0; b.b1 = -2.0; b.b2 = 1.0;
            b.a1 = 2.0 * (K * K - 1.0) / a0;
            b.a2 = (1.0 - K / Q + K * K) / a0;
        }
    }
    hops_.clear();
    cur_ = Hop{};
    histL_.assign(2 * TruePeakInterpolator::kTaps, 0.0f);
    histR_.assign(2 * TruePeakInterpolator::kTaps, 0.0f);
    pos_ = 0;
}

void LoudnessMeter::truePeak(std::vector<float>& hist, float x)
{
    // A window of kTaps samples, contiguous thanks to the doubled buffer; the interpolator looks between its
    // middle two samples.
    constexpr int T = TruePeakInterpolator::kTaps, H = TruePeakInterpolator::kHalf;
    hist[static_cast<size_t>(pos_)] = x;
    hist[static_cast<size_t>(pos_ + T)] = x;
    const float* w = hist.data() + pos_ + 1;   // oldest .. newest: w[0] .. w[T - 1]
    const float* mid = w + H - 1;
    cur_.peak = std::max(cur_.peak, std::fabs(x));
    if (TruePeakInterpolator::windowPeak(mid) * tp_.gainBound() <= cur_.peak) return;   // cannot beat it: exact
    cur_.peak = std::max(cur_.peak, static_cast<float>(tp_.between(mid)));
}

void LoudnessMeter::process(const float* L, const float* R, int n)
{
    constexpr int T = TruePeakInterpolator::kTaps;
    for (int i = 0; i < n; ++i) {
        const double l = L[i], r = R[i];
        const double kl = highPass_[0].process(shelf_[0].process(l));
        const double kr = highPass_[1].process(shelf_[1].process(r));
        cur_.kL += kl * kl;
        cur_.kR += kr * kr;
        cur_.lr += l * r;
        cur_.ll += l * l;
        cur_.rr += r * r;
        const double m = 0.5 * (l + r), s = 0.5 * (l - r);
        cur_.mid += m * m;
        cur_.side += s * s;
        pos_ = (pos_ + 1) % T;
        truePeak(histL_, L[i]);
        truePeak(histR_, R[i]);
        if (++cur_.samples == hopLength_) {
            hops_.push_back(cur_);
            cur_ = Hop{};
        }
    }
}

LoudnessReport LoudnessMeter::report() const
{
    LoudnessReport r;
    const size_t n = hops_.size();
    r.seconds = static_cast<double>(n) * 0.1;
    if (n == 0) return r;
    // Mean square sums (left plus right, both weighted 1) of windows of w hops ending at hop j.
    auto window = [&](size_t j, size_t w) {
        double sum = 0.0;
        int samples = 0;
        for (size_t k = j + 1 - w; k <= j; ++k) { sum += hops_[k].kL + hops_[k].kR; samples += hops_[k].samples; }
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
        r.integrated = lufs(gatedMean(blocks, std::max(-70.0, lufs(abs) - 10.0)));
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
    for (const Hop& h : hops_) peak = std::max(peak, h.peak);
    r.truePeak = peak > 0.0f ? 20.0 * std::log10(peak) : -120.0;
    if (!shortTerm.empty()) {
        const double st = window(loudest, 30);
        r.shortTermMax = lufs(st);
        float p3 = 0.0f;
        for (size_t k = loudest - 29; k <= loudest; ++k) p3 = std::max(p3, hops_[k].peak);
        r.psr = (p3 > 0.0f ? 20.0 * std::log10(p3) : -120.0) - r.shortTermMax;
        std::vector<double> gated;
        const double rel = lufs(gatedMean(shortTerm, -70.0)) - 20.0;
        for (double z : shortTerm) if (lufs(z) > -70.0 && lufs(z) > rel) gated.push_back(lufs(z));
        if (gated.size() > 1) {
            std::sort(gated.begin(), gated.end());
            auto centile = [&](double q) { return gated[static_cast<size_t>(std::lround(q * static_cast<double>(gated.size() - 1)))]; };
            r.range = centile(0.95) - centile(0.10);
        }
    } else {
        r.shortTermMax = r.integrated;
    }
    r.plr = r.truePeak - r.integrated;
    {
        double ms = 0.0;
        int samples = 0;
        for (const Hop& h : hops_) { ms += h.ll + h.rr; samples += h.samples; }
        ms /= std::max(1, 2 * samples);
        r.crest = r.truePeak - (ms > 0.0 ? 10.0 * std::log10(ms) : -120.0);
    }
    // Stereo: the correlation over the whole and over each second, the side under the mid.
    double lr = 0.0, ll = 0.0, rr = 0.0, mid = 0.0, side = 0.0;
    for (const Hop& h : hops_) { lr += h.lr; ll += h.ll; rr += h.rr; mid += h.mid; side += h.side; }
    r.correlation = ll > 0.0 && rr > 0.0 ? lr / std::sqrt(ll * rr) : 1.0;
    r.sideUnderMid = side > 0.0 && mid > 0.0 ? 10.0 * std::log10(mid / side) : (side > 0.0 ? -120.0 : 120.0);
    for (size_t j = 9; j < n; j += 10) {
        double a = 0.0, b = 0.0, c = 0.0;
        for (size_t k = j - 9; k <= j; ++k) { a += hops_[k].lr; b += hops_[k].ll; c += hops_[k].rr; }
        const double samples = 0.5 * static_cast<double>(hopLength_) * 10.0;
        if (b / samples < 1e-5 || c / samples < 1e-5) continue;   // a second under -50 dBFS (a last tail) says nothing
        if (a / std::sqrt(b * c) < r.correlationLow) {
            r.correlationLow = a / std::sqrt(b * c);
            r.correlationLowAt = static_cast<double>(j - 9) * 0.1;
        }
    }
    return r;
}

} // namespace eph
