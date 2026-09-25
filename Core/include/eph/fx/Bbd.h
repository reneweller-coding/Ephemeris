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
    float read(const std::vector<float>& buf, double delaySamples) const;
    double sr_ = 48000.0;
    std::vector<float> bufL_, bufR_;
    size_t mask_ = 0, write_ = 0;
    EchoSettings s_;
    Smoother delay_;
    double target_ = 0.0;
    bool fresh_ = true;
    double lfo_ = 0.0;
    Svf lp1L_, lp2L_, lp1R_, lp2R_, hpL_, hpR_;
    float drive_ = 1.5f, driveNorm_ = 0.66f, hiss_ = 0.0f;
    Rng rng_;
};

} // namespace eph
