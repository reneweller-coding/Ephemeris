/**
 * @file Bbd.h
 * @brief A bucket-brigade delay (PLAN 5.8): the analogue echo beside the tape echo.
 *
 * A BBD (the MN3005 of the Boss DM-2 and the Memory Man: 4096 stages) moves the signal through a chain of
 * capacitors at a clock rate. For a delay d the clock runs at N / (2 d), and the anti-aliasing and reconstruction
 * filters around the chain have to stay below half of it -- so a BBD echo gets darker the longer it is set, the
 * character of those units. Modelled here: the delay (read with a four-point Hermite as the tape echo does, the
 * time gliding when it is changed), two cascaded low passes at a third of the clock (never above the tone knob),
 * a slow chorus-like wander of the clock instead of a tape's wow and flutter, the compander's soft saturation and
 * its faint hiss in the loop, and a high pass against rumble. Same settings as the tape echo (EchoSettings).
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/fx/TapeEcho.h"
#include <vector>

namespace eph {

/** @brief A stereo BBD echo; returns only the wet signal. */
class BbdEcho {
public:
    /** @brief Allocates for up to @p maxSeconds of delay. */
    void prepare(double sampleRate, double maxSeconds, uint64_t seed);
    /** @brief Clears the chain. */
    void reset();
    /** @brief Takes the settings; call at every 32-sample cell (wowMs: depth of the clock's wander). */
    void set(const EchoSettings& s);
    /** @brief Adds the echo of @p inL, @p inR to @p outL, @p outR. */
    void process(const float* inL, const float* inR, float* outL, float* outR, int n);

private:
    /** @brief @p buf read @p delaySamples ago (interpolated). */
    float read(const std::vector<float>& buf, double delaySamples) const;
    double sr_ = 48000.0;   ///< the sample rate, Hz
    std::vector<float> bufL_;   ///< the bucket chain, left
    std::vector<float> bufR_;   ///< ... right
    size_t mask_ = 0;   ///< its size - 1
    size_t write_ = 0;   ///< where the next sample goes
    EchoSettings s_;   ///< the settings
    Smoother delay_;   ///< the delay in samples, gliding to its target (the clock)
    double target_ = 0.0;   ///< the delay asked for, samples
    bool fresh_ = true;   ///< no settings taken since prepare() or reset()
    double lfo_ = 0.0;   ///< the clock's wander: its phase
    Svf lp1L_;   ///< the chain's fourth-order low pass: its first section, left
    Svf lp2L_;   ///< ... its second section, left
    Svf lp1R_;   ///< ... first section, right
    Svf lp2R_;   ///< ... second section, right
    Svf hpL_;   ///< the feedback's high pass, left
    Svf hpR_;   ///< ... right
    float drive_ = 1.5f;   ///< the chain's drive
    float driveNorm_ = 0.66f;   ///< the gain that keeps the level after it
    float hiss_ = 0.0f;   ///< the chain's hiss, linear
    Rng rng_;   ///< the hiss's stream
};

} // namespace eph
