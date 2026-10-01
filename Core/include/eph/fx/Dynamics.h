/**
 * @file Dynamics.h
 * @brief The master's dynamics: a log-domain bus compressor, an 8x interpolated true-peak estimate and
 *        a lookahead true-peak limiter.
 *
 * **Bus compressor** after Giannoulis, Massberg and Reiss, "Digital dynamic range compressor design --
 * a tutorial and analysis" (JAES 60(6), 2012): feed-forward, the gain computer in the log domain with a
 * quadratic soft knee (their eq. 4), and the smooth decoupled peak detector placed after the gain
 * computer (eq. 17 on the gain reduction), which they recommend for its artefact-free release and
 * because attack and release then act on decibels, not on the waveform. The two channels share one
 * detector (the larger of the two), so the stereo image does not move.
 *
 * **True peak.** A signal's peak between the samples can lie several decibels above its largest sample
 * -- a sine at a quarter of the sampling rate sampled at 45 degrees reads 3 dB low, and at 0.45 of the
 * sampling rate a crest that falls midway between two samples reads 16 dB low. Nielsen and Lund,
 * "0 dBFS+ Levels in Digital Mastering" (AES 109th Convention, 2000), are why the ceiling is set on
 * this quantity and not on the largest sample. The estimate follows the method of ITU-R BS.1770-4
 * Annex 2 -- interpolate a grid of points between the samples and take the largest -- with a filter
 * bank of our own rather than the standard's table: seven points between every pair of samples (8x),
 * each from a Kaiser-windowed sinc of twenty-four taps.
 *
 * *Why these numbers, and why the previous ones were wrong* (measured 16.09.2026, docs/PLAN.md). This
 * is a fractional-delay filter bank, not an interpolating low-pass, and for a fractional-delay filter
 * evaluated at a single point only the **passband** deviation |H(f)| - 1 over 0 .. 0.5 fs is an error:
 * the input is already band-limited, so there is no image for a stopband to suppress (Laakso,
 * Valimaki, Karjalainen and Laine, "Splitting the unit delay", IEEE Signal Processing Magazine 13(1),
 * 1996, section on windowed-sinc designs). The old bank spent its length on a stopband it did not
 * need: a Kaiser window of beta = 8 buys about 80 dB of attenuation above Nyquist and pays for it with
 * a transition band so wide that |H| was already 1.3 dB down at 0.40 fs and 5.6 dB down at 0.45 fs.
 * With beta = 4 and twenty-four taps the same bank is flat to within 0.14 dB over 0 .. 0.45 fs. The
 * grid was the other half: three points per sample can miss a crest by cos(pi f / 4), which is 0.44 dB
 * at 0.40 fs, so eight points (cos(pi f / 8), 0.14 dB at 0.45 fs) are the matching choice. The two
 * together read a crest placed anywhere between two samples, up to 0.45 fs, within 0.15 dB, against
 * 4.2 dB for the old bank; on an eight-minute render the reported true peak rose by 1.30 dB.
 *
 * Above 0.45 fs (21.6 kHz at 48 kHz) no claim is made and none is affordable: reconstructing a crest
 * there needs hundreds of taps per phase, because the sine's own samples carry almost none of its
 * amplitude. The normalisation is unity at DC, which is a no-op here -- the taps of every phase sum to
 * 1.000000000 before it is applied -- and is kept only so that a future window cannot change the DC
 * gain silently.
 *
 * **Limiter.** For every sample the gain that keeps its true peak at the ceiling is computed; a
 * sliding minimum over the lookahead window followed by a moving average of the same length gives a
 * gain that has fully arrived when the peak does -- each average contains the peak's own required
 * gain, and every value in it is at most that -- and moves smoothly, without overshoot; the recovery
 * afterwards is exponential. The audio is delayed by the window (plus the interpolator's half length),
 * which the engine reports as its latency.
 * @note Copied from Phosphene `Core/include/phos/Dynamics.h` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 */
#pragma once
#include "eph/Dsp.h"
#include <vector>

namespace eph {

/** @brief Feed-forward log-domain compressor, stereo-linked. */
class BusCompressor {
public:
    /** @brief Sets the sample rate and clears the detector. */
    void prepare(double sampleRate) { sr_ = sampleRate; reset(); }
    /** @brief Clears the detector. */
    void reset() { y1_ = 0.0; yL_ = 0.0; }
    /**
     * @brief Settings.
     * @param thresholdDb threshold T
     * @param ratio       R (1 = off)
     * @param kneeDb      knee width W
     * @param attackMs,releaseMs detector time constants
     */
    void set(float thresholdDb, float ratio, float kneeDb, float attackMs, float releaseMs)
    {
        T_ = thresholdDb;
        R_ = std::max(1.0f, ratio);
        W_ = std::max(0.0f, kneeDb);
        aA_ = std::exp(-1.0 / (std::max(0.01f, attackMs) * 0.001 * sr_));
        aR_ = std::exp(-1.0 / (std::max(0.01f, releaseMs) * 0.001 * sr_));
    }
    /** @brief The static curve: output level for an input level, both in dB (Giannoulis eq. 4). */
    double curve(double x) const
    {
        const double d = x - T_;
        if (2.0 * d < -W_) return x;
        if (W_ > 0.0f && 2.0 * std::fabs(d) <= W_) return x + (1.0 / R_ - 1.0) * (d + W_ / 2.0) * (d + W_ / 2.0) / (2.0 * W_);
        return T_ + d / R_;
    }
    /** @brief Processes a stereo block in place. */
    void process(float* L, float* R, int n)
    {
        for (int i = 0; i < n; ++i) {
            const double peak = std::max(std::fabs(static_cast<double>(L[i])), std::fabs(static_cast<double>(R[i])));
            const double xG = peak > 1.0e-6 ? 20.0 * std::log10(peak) : -120.0;
            const double xL = xG - curve(xG);   // gain reduction the static curve asks for, >= 0
            y1_ = std::max(xL, aR_ * y1_ + (1.0 - aR_) * xL);
            yL_ = aA_ * yL_ + (1.0 - aA_) * y1_;
            const float g = static_cast<float>(std::pow(10.0, -yL_ / 20.0));
            L[i] *= g;
            R[i] *= g;
            reduction_ = static_cast<float>(yL_);
        }
    }
    /** @brief Gain reduction of the last sample, dB. */
    float reduction() const { return reduction_; }

private:
    double sr_ = 48000.0;   ///< the sample rate, Hz
    double aA_ = 0.99;   ///< the detector's attack coefficient
    double aR_ = 0.999;   ///< the detector's release coefficient
    double y1_ = 0.0;   ///< the detector's peak stage, dB of reduction
    double yL_ = 0.0;   ///< its smoothed stage: the reduction applied
    float T_ = -12.0f;   ///< the threshold, dB
    float R_ = 2.0f;   ///< the ratio
    float W_ = 6.0f;   ///< the knee's width, dB
    float reduction_ = 0.0f;   ///< the reduction of the last sample, dB
};

/** @brief 8x interpolation for true-peak estimates: seven phases of twenty-four taps each. */
class TruePeakInterpolator {
public:
    static constexpr int kPhases = 8;            ///< points per input sample; kPhases - 1 are interpolated
    static constexpr int kTaps = 24;             ///< taps per phase
    static constexpr int kHalf = kTaps / 2;      ///< samples of lookahead the filter needs
    static constexpr int kHistory = kTaps;       ///< samples of input a caller has to keep for between()
    /** @brief Designs the Kaiser-windowed sinc fractional-delay bank (beta 4, full band). */
    TruePeakInterpolator();
    /**
     * @brief Largest magnitude of the kPhases - 1 interpolated points between x[0] and x[1].
     * @param x pointer to the earlier sample; x[-(kHalf - 1)] .. x[kHalf] must be readable
     */
    double between(const float* x) const
    {
        const float* p = x - kHalf + 1;
        double peak = 0.0;
        for (int k = 0; k < kPhases - 1; ++k) {
            // Four partial sums, not one. A single accumulator makes the inner loop a chain of kTaps
            // dependent additions, and the whole bank is then latency-bound: with kTaps at twenty-four
            // that alone cost 64 % of the offline render's time (measured 16.09.2026). Four independent
            // chains of six additions each cut it to a quarter. kTaps is a multiple of four by design.
            double s0 = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0;
            for (int m = 0; m < kTaps; m += 4) {
                s0 += h_[k][m] * static_cast<double>(p[m]);
                s1 += h_[k][m + 1] * static_cast<double>(p[m + 1]);
                s2 += h_[k][m + 2] * static_cast<double>(p[m + 2]);
                s3 += h_[k][m + 3] * static_cast<double>(p[m + 3]);
            }
            peak = std::max(peak, std::fabs((s0 + s1) + (s2 + s3)));
        }
        return peak;
    }
    /**
     * @brief Largest magnitude any interpolated point can reach, per unit of the largest input sample.
     *
     * max_k sum_m |h_k[m]|, the induced l-infinity norm of the bank: if every sample the filter can see
     * is at most @c a, no interpolated point can exceed @c gainBound() * a. A caller that only needs to
     * know whether the peak clears a threshold can skip the whole bank when it does not, which is
     * exact, not an approximation -- see TruePeakLimiter::process and LoudnessMeter::process.
     */
    double gainBound() const { return bound_; }
    /** @brief Largest magnitude among the kTaps samples between() would read from @p x. */
    static float windowPeak(const float* x)
    {
        const float* p = x - kHalf + 1;
        float a = 0.0f, b = 0.0f;
        for (int m = 0; m < kTaps; m += 2) { a = std::max(a, std::fabs(p[m])); b = std::max(b, std::fabs(p[m + 1])); }
        return std::max(a, b);
    }
private:
    double h_[kPhases - 1][kTaps] = {};   ///< the fractional-delay filters, one per interpolated phase
    double bound_ = 1.0;   ///< gainBound()
};

/** @brief Stereo lookahead true-peak limiter. */
class TruePeakLimiter {
public:
    /** @brief Allocates for a sample rate; @p lookaheadMs sets the window. */
    void prepare(double sampleRate, float lookaheadMs = 1.5f);
    /** @brief Clears the delay lines and the gain. */
    void reset();
    /** @brief Ceiling in dBTP and recovery time. */
    void set(float ceilingDb, float releaseMs);
    /** @brief Processes in place. */
    void process(float* L, float* R, int n);
    /** @brief Samples of delay the limiter adds. */
    int latency() const { return window_ - 1 + TruePeakInterpolator::kHalf; }
    /** @brief Gain reduction of the last sample, dB (>= 0). */
    float reduction() const { return reduction_; }

private:
    TruePeakInterpolator interp_;   ///< the true-peak interpolator
    double sr_ = 48000.0;   ///< the sample rate, Hz
    int window_ = 72;   ///< the lookahead window, samples
    float ceiling_ = 0.891f;   ///< the ceiling, linear
    double release_ = 0.999;   ///< the gain's per-sample recovery factor
    // Input history for the interpolator (both channels): 2 * kTaps slots, every sample written into
    // both halves, so the kTaps the filter needs are always contiguous (see process()).
    std::vector<float> histL_;   ///< the interpolator's input history, left
    std::vector<float> histR_;   ///< ... right
    int histPos_ = 0;   ///< where the history is written
    double prevBetween_ = 0.0;   ///< the last sample's interpolated peak (a peak between two samples counts for both)
    /// Required gains, the sliding-minimum deque and the moving average.
    std::vector<double> req_;
    std::vector<int> dq_;   ///< the sliding minimum's deque: positions of required gains
    int dqHead_ = 0;   ///< the deque's head
    int dqTail_ = 0;   ///< the deque's tail
    std::vector<double> minRing_;   ///< the sliding minima of the last window
    double minSum_ = 0.0;   ///< their sum: the moving average times the window
    long long t_ = 0;   ///< samples processed
    int sinceRecompute_ = 0;   ///< samples since minSum_ was summed afresh (rounding)
    double gain_ = 1.0;   ///< the gain applied
    // The audio delay.
    std::vector<float> delayL_;   ///< the audio delay, left
    std::vector<float> delayR_;   ///< the audio delay, right
    int delayPos_ = 0;   ///< where the delay is written
    float reduction_ = 0.0f;   ///< the reduction of the last sample, dB
};

} // namespace eph
