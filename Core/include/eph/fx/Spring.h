/**
 * @file Spring.h
 * @brief The spring reverb (PLAN 5.8), fed from the echo's send as in a tape echo with springs.
 *
 * A spring does not scatter sound like a room; it carries it as a dispersive wave: the low frequencies
 * of a transient arrive later than the highs, so every reflection is a falling chirp -- the "boing".
 * After Välimäki, Parker and Abel ("Parametric spring reverberation effect", JAES 58(7/8), 2010): a
 * cascade of first-order all-passes produces the chirp and sits in a feedback loop with the spring's
 * transit delay, a low pass (a spring passes little above 5 kHz) and a high pass. Two springs of
 * different lengths, one per side, as in the tanks of the time.
 *
 * The tape echoes of the seventies that players of this music used had springs built in; here the
 * spring takes the echo's send, so a row sent to the echo is also sent to the spring.
 */
#pragma once
#include "eph/Dsp.h"
#include <vector>

namespace eph {

/** @brief Two springs, one per side; returns the wet signal. */
class Spring {
public:
    static constexpr int kStages = 48;   ///< all-passes per spring: the chirp's strength
    /** @brief Sample rate; allocates the loop delays. */
    void prepare(double sampleRate);
    /** @brief Decay time (T60) and the loop's low pass; call at every 32-sample cell. */
    void set(float decaySeconds, float toneHz);
    /** @brief Adds the springs' return of @p inL, @p inR to @p outL, @p outR. */
    void process(const float* inL, const float* inR, float* outL, float* outR, int n);

private:
    /** @brief One spring: a delay line, a chain of first-order all-passes (the chirp), its filters in the loop. */
    struct Tank {
        std::vector<float> line;   ///< the delay line, a ring
        size_t mask = 0;   ///< its size - 1
        size_t write = 0;   ///< where the next sample goes
        int delay = 1000;   ///< the loop delay, samples
        float ap[kStages] = {};   ///< one state per first-order all-pass
        float gain = 0.5f;   ///< the loop's gain (from the decay)
        Svf lp;   ///< the loop's low pass
        Svf hp;   ///< the loop's high pass
        float y = 0.0f;   ///< the loop's last output
    };
    double sr_ = 48000.0;   ///< the sample rate, Hz
    Tank tanks_[2];   ///< left, right
};

} // namespace eph
