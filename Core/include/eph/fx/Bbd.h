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
    double m_sr = 48000.0;   ///< the sample rate, Hz
    std::vector<float> m_bufL;   ///< the bucket chain, left
    std::vector<float> m_bufR;   ///< ... right
    size_t m_mask = 0;   ///< its size - 1
    size_t m_write = 0;   ///< where the next sample goes
    EchoSettings m_s;   ///< the settings
    Smoother m_delay;   ///< the delay in samples, gliding to its target (the clock)
    double m_target = 0.0;   ///< the delay asked for, samples
    bool m_fresh = true;   ///< no settings taken since prepare() or reset()
    double m_lfo = 0.0;   ///< the clock's wander: its phase
    Svf m_lp1L;   ///< the chain's fourth-order low pass: its first section, left
    Svf m_lp2L;   ///< ... its second section, left
    Svf m_lp1R;   ///< ... first section, right
    Svf m_lp2R;   ///< ... second section, right
    Svf m_hpL;   ///< the feedback's high pass, left
    Svf m_hpR;   ///< ... right
    float m_drive = 1.5f;   ///< the chain's drive
    float m_driveNorm = 0.66f;   ///< the gain that keeps the level after it
    float m_hiss = 0.0f;   ///< the chain's hiss, linear
    Rng m_rng;   ///< the hiss's stream
};

} // namespace eph
