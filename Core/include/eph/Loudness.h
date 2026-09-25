/**
 * @file Loudness.h
 * @brief What a mix measures (the production guide's 9.1, 9.2 and 6.5): loudness after EBU R128 and ITU-R
 *        BS.1770-4, true peak, the dynamic figures PSR and PLR, and the stereo image's correlation and width.
 *
 * - **Loudness**: K-weighting (the two BS.1770 filters, designed for the sample rate), mean squares over
 *   400 ms blocks every 100 ms; the integrated loudness gated absolutely at -70 LUFS and relatively 10 LU
 *   under the mean of what passed; the short-term loudness over 3 s; the loudness range (EBU Tech 3342)
 *   from the 10th to the 95th centile of the short-term values gated at -70 LUFS and 20 LU under their mean.
 * - **True peak**: the 8x interpolator of Dynamics.h; a window whose samples cannot reach the running
 *   maximum is skipped, which is exact (TruePeakInterpolator::gainBound).
 * - **PSR** (peak to short-term loudness ratio): the true peak within the loudest 3 s against their
 *   loudness; under 8 the guide calls a master crushed. **PLR**: the true peak against the integrated.
 * - **Stereo**: the correlation of left and right over the whole and its lowest one-second value (the
 *   guide: between 0 and +1, never below 0 for longer than a second), and how far the side lies under the mid.
 *
 * Offline: the meter keeps every 100 ms hop (a few hundred kilobytes an hour) and computes its report at the end.
 */
#pragma once
#include "eph/fx/Dynamics.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace eph {

/** @brief The figures of a measured programme. */
struct LoudnessReport {
    double integrated = -120.0;     ///< LUFS, gated
    double shortTermMax = -120.0;   ///< LUFS, the loudest 3 s
    double range = 0.0;             ///< LU, EBU Tech 3342
    double truePeak = -120.0;       ///< dBTP
    double psr = 0.0;               ///< dB: true peak in the loudest 3 s minus their loudness
    double plr = 0.0;               ///< dB: true peak minus integrated loudness
    double correlation = 1.0;       ///< of left and right over the whole
    double correlationLow = 1.0;    ///< the lowest one-second value (of seconds above -50 dBFS RMS)
    double correlationLowAt = 0.0;  ///< where that second starts, in seconds
    double sideUnderMid = 0.0;      ///< dB the side's energy lies under the mid's
    double crest = 0.0;             ///< dB: true peak over the RMS (the addon's 9: at least 12 keeps the transients)
    double seconds = 0.0;           ///< how much was measured
};

/**
 * @brief The gain of an exported file at sample @p i of @p total (the addon's 9): an S-shaped fade-in over the first
 *        two seconds and an S-shaped fade-out over the last ten, never linear, so the last tail of the hall fades to
 *        nothing instead of being cut.
 */
inline float exportFade(int64_t i, int64_t total, double sampleRate)
{
    const double in = static_cast<double>(i) / (2.0 * sampleRate), out = static_cast<double>(total - i) / (10.0 * sampleRate);
    const double x = std::min(1.0, std::min(in, out));
    return static_cast<float>(x >= 1.0 ? 1.0 : 0.5 - 0.5 * std::cos(3.14159265358979 * std::max(0.0, x)));
}

/** @brief Measures a stereo programme; call process() for every block, then report(). */
class LoudnessMeter {
public:
    /** @brief Designs the filters for @p sampleRate and clears everything. */
    void prepare(double sampleRate);
    /** @brief Measures @p n samples. */
    void process(const float* L, const float* R, int n);
    /** @brief The figures of everything measured so far. */
    LoudnessReport report() const;

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double process(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    /** @brief The sums of one 100 ms hop. */
    struct Hop {
        double kL = 0.0, kR = 0.0;           ///< K-weighted sums of squares
        double lr = 0.0, ll = 0.0, rr = 0.0; ///< for the correlation
        double mid = 0.0, side = 0.0;        ///< sums of squares of (L + R) / 2 and (L - R) / 2
        float peak = 0.0f;                   ///< true peak (linear)
        int samples = 0;
    };
    void truePeak(std::vector<float>& hist, float x);

    double sr_ = 48000.0;
    int hopLength_ = 4800;
    Biquad shelf_[2], highPass_[2];
    std::vector<Hop> hops_;
    Hop cur_;
    TruePeakInterpolator tp_;
    std::vector<float> histL_, histR_;   ///< each channel's last kTaps samples, twice over (a contiguous window at pos_ + 1)
    int pos_ = 0;
};

} // namespace eph
