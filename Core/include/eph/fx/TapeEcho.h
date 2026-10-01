/**
 * @file TapeEcho.h
 * @brief The tape echo (PLAN 5.8): a delay whose head wanders, whose loop saturates and loses its top.
 *
 * In this music the echo is part of the composition: an eighth-note sequence into an echo of a dotted
 * eighth becomes an interlocking sixteenth pattern (PLAN 2.4), and the feedback thrown up at the end of
 * a phrase is a gesture of its own.
 *
 * **Model.** After the tape delays of Arnardottir, Abel and Smith ("A digital model of the Echoplex tape
 * delay", AES 125, 2008) in the parts that matter to the ear, not in circuit detail:
 * - the playback head reads the tape at a delay that wanders: *wow* as a slow sine near 0.7 Hz plus a
 *   slower random drift, *flutter* as a small fast sine near 9 Hz, both in milliseconds of delay;
 * - the loop runs through a low pass (the tape's and the head's loss of highs, so every repeat is darker
 *   than the one before), a high pass at 70 Hz (no build-up of rumble) and a tanh saturation, so a
 *   feedback above one does not explode but settles into the self-oscillation a player uses as an
 *   effect;
 * - a change of time glides, as the tape speed does, and so it bends the pitch of what is in the loop.
 * Reading is by four-point Hermite interpolation. Ping-pong feeds each side's loop into the other.
 *
 * Deterministic: the drift has its own stream, seeded once.
 */
#pragma once
#include "eph/Dsp.h"
#include <cstdint>
#include <vector>

namespace eph {

/** @brief Settings of the echo for the following samples. */
struct EchoSettings {
    double delaySeconds = 0.3;   ///< nominal delay
    float feedback = 0.45f;      ///< loop gain (above 1 runs into saturation)
    float toneHz = 3500.0f;      ///< loop low pass
    float wowMs = 0.6f;          ///< depth of the slow wander
    float flutterMs = 0.06f;     ///< depth of the fast wander
    float driveDb = 4.0f;        ///< saturation drive in the loop
    bool pingPong = true;        ///< cross the loops
    float lowCutHz = 70.0f;      ///< the loop's high pass: each repeat thinner (the production guide's 5.4)
};

/** @brief A stereo tape echo; returns only the wet signal. */
class TapeEcho {
public:
    /** @brief Allocates for up to @p maxSeconds of delay. */
    void prepare(double sampleRate, double maxSeconds, uint64_t seed);
    /** @brief Clears the tape. */
    void reset();
    /** @brief Takes the settings; call at every 32-sample cell. The first call after prepare() or reset()
     *         puts the tape at its speed at once; later changes of time glide, as a tape's speed does. */
    void set(const EchoSettings& s);
    /** @brief Adds the echo of @p inL, @p inR to @p outL, @p outR. */
    void process(const float* inL, const float* inR, float* outL, float* outR, int n);

private:
    /** @brief @p buf read @p delaySamples ago (interpolated). */
    float read(const std::vector<float>& buf, double delaySamples) const;
    double sr_ = 48000.0;   ///< the sample rate, Hz
    std::vector<float> bufL_;   ///< the tape, left
    std::vector<float> bufR_;   ///< the tape, right
    size_t mask_ = 0;   ///< its size - 1
    size_t write_ = 0;   ///< where the next sample goes
    EchoSettings s_;   ///< the settings
    Smoother delay_;             ///< the tape speed: the delay in samples glides to its target
    double target_ = 0.0;   ///< the delay asked for, samples
    bool fresh_ = true;          ///< no settings taken since prepare() or reset()
    double wowPhase_ = 0.0;   ///< the wow's phase, cycles
    double flutterPhase_ = 0.0;   ///< the flutter's phase, cycles
    double drift_ = 0.0;   ///< the slow drift of the speed, -1..1
    double driftTarget_ = 0.0;   ///< where it drifts to (a new one every 32768 samples)
    int64_t count_ = 0;   ///< samples processed
    Svf lpL_;   ///< the feedback's low pass, left
    Svf lpR_;   ///< ... right
    Svf hpL_;   ///< the feedback's high pass, left
    Svf hpR_;   ///< ... right
    float drive_ = 1.5f;   ///< the tape's drive
    float driveNorm_ = 0.66f;   ///< the gain that keeps the level after it
    Rng rng_;   ///< the drift's random stream
};

} // namespace eph
