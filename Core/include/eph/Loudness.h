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
 * @author Rene Weller
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
    double m_integrated = -120.0;     ///< LUFS, gated
    double m_shortTermMax = -120.0;   ///< LUFS, the loudest 3 s
    double m_range = 0.0;             ///< LU, EBU Tech 3342
    double m_truePeak = -120.0;       ///< dBTP
    double m_psr = 0.0;               ///< dB: true peak in the loudest 3 s minus their loudness
    double m_plr = 0.0;               ///< dB: true peak minus integrated loudness
    double m_correlation = 1.0;       ///< of left and right over the whole
    double m_correlationLow = 1.0;    ///< the lowest one-second value (of seconds above -50 dBFS RMS)
    double m_correlationLowAt = 0.0;  ///< where that second starts, in seconds
    double m_sideUnderMid = 0.0;      ///< dB the side's energy lies under the mid's
    double m_crest = 0.0;             ///< dB: true peak over the RMS (the addon's 9: at least 12 keeps the transients)
    double m_seconds = 0.0;           ///< how much was measured
};

/**
 * @brief The gain of an exported file at sample @p i of @p total (the addon's 9): an S-shaped fade-in over the first
 *        two seconds and an S-shaped fade-out over the last ten, never linear, so the last tail of the hall fades to
 *        nothing instead of being cut.
 * @param[in] i           the sample
 * @param[in] total       the length of the file in samples
 * @param[in] sampleRate  the sample rate in Hz
 * @return the gain at that sample, 0..1
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
    /**
     * @brief Designs the filters for @p sampleRate and clears everything.
     * @param[in] sampleRate  the sample rate in Hz
     */
    void prepare(double sampleRate);
    /**
     * @brief Measures @p n samples.
     * @param[in] L  the left channel
     * @param[in] R  the right channel
     * @param[in] n  how many samples
     */
    void process(const float* L, const float* R, int n);
    /**
     * @brief The figures of everything measured so far.
     * @return the figures of everything measured so far
     */
    LoudnessReport report() const;

private:
    /** @brief A biquad in transposed direct form II (the K weighting). */
    struct Biquad {
        double m_b0 = 1;   ///< feed-forward coefficient of x[n]
        double m_b1 = 0;   ///< ... of x[n-1]
        double m_b2 = 0;   ///< ... of x[n-2]
        double m_a1 = 0;   ///< feedback coefficient of y[n-1]
        double m_a2 = 0;   ///< ... of y[n-2]
        double m_z1 = 0;   ///< the first state
        double m_z2 = 0;   ///< the second state
        /**
         * @brief One sample @p x through the filter.
         * @param[in] x  the input sample
         * @return the filtered sample
         */
        double process(double x) { const double y = m_b0 * x + m_z1; m_z1 = m_b1 * x - m_a1 * y + m_z2; m_z2 = m_b2 * x - m_a2 * y; return y; }
    };
    /** @brief The sums of one 100 ms hop. */
    struct Hop {
        double m_kL = 0.0;   ///< the K-weighted sum of squares, left
        double m_kR = 0.0;   ///< ... right
        double m_lr = 0.0;   ///< the sum of L R, for the correlation
        double m_ll = 0.0;   ///< the sum of L L
        double m_rr = 0.0;   ///< the sum of R R
        double m_mid = 0.0;   ///< the sum of squares of (L + R) / 2
        double m_side = 0.0;   ///< the sum of squares of (L - R) / 2
        float m_peak = 0.0f;                   ///< true peak (linear)
        int m_samples = 0;   ///< how many samples the hop has
    };
    /**
     * @brief Writes @p x into the channel history @p hist and raises the hop's true peak by what lies around it.
     * @param[in,out] hist  the channel's recent samples
     * @param[in]     x     the new sample
     */
    void truePeak(std::vector<float>& hist, float x);

    double m_sr = 48000.0;   ///< the sample rate, Hz
    int m_hopLength = 4800;   ///< a hop, samples (100 ms)
    Biquad m_shelf[2];   ///< the K weighting's high shelf, per channel
    Biquad m_highPass[2];   ///< the K weighting's high pass, per channel
    std::vector<Hop> m_hops;   ///< the finished hops
    Hop m_cur;   ///< the hop being summed
    TruePeakInterpolator m_tp;   ///< the true-peak interpolator
    std::vector<float> m_histL;   ///< the left channel's last Taps samples, twice over (a contiguous window at m_pos + 1)
    std::vector<float> m_histR;   ///< ... the right channel's
    int m_pos = 0;   ///< where the histories are written
};

} // namespace eph
