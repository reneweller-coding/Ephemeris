/**
 * @file Atmos.h
 * @brief The atmosphere (PLAN 5.6): wind, sweeps and the bleeps of space, for the cosmic intros and codas.
 *
 * Three layers, each quiet on its own and meant to be moved by the hands:
 * - **Wind**: two decorrelated noises, each through a resonant band pass whose centre wanders on an
 *   Ornstein-Uhlenbeck process around `windHz` (half an octave, a quarter-minute time constant), so the
 *   wind breathes without repeating; left and right wander apart.
 * - **Sweeps**: now and then a resonant filter sweeps over pink-ish noise, up or down over four to twelve
 *   seconds -- the "cosmic sweep" of the style's intros. Their rate is per minute; their start times are
 *   a Poisson process on the atmosphere's own stream.
 * - **Bleeps**: sample-and-hold tones -- short sines on random notes of the scale over the current root,
 *   two or three octaves up, a few in a burst -- the sound of a modular's random voltage into an
 *   oscillator. They go to the echo most of all.
 * Deterministic: one stream, seeded once; the rates are drawn per 32-sample cell.
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/synth/ModVoice.h"   // OuProcess
#include <cstdint>

namespace eph {

/** @brief Settings for the following samples. */
struct AtmosSettings {
    float windGain = 0.0f;         ///< linear
    float windHz = 700.0f;         ///< centre of the wind's band
    float sweepsPerMinute = 0.0f;  ///< rate of sweeps
    float sweepGain = 0.0f;        ///< linear
    float bleepsPerMinute = 0.0f;  ///< rate of bleep bursts
    float bleepGain = 0.0f;        ///< linear
    int rootPc = 9;                ///< the root the bleeps take their notes from
    int scale = 0;                 ///< compose.scale order
};

/** @brief The atmosphere: stereo out, plus a separate bleep output for the echo send. */
class Atmos {
public:
    /** @brief Sample rate and seed. */
    void prepare(double sampleRate, uint64_t seed);
    /** @brief Takes the settings; call at every 32-sample cell. */
    void set(const AtmosSettings& s);
    /** @brief Whether any layer is audible or ringing. */
    bool active() const;
    /** @brief Adds @p n samples to @p L, @p R; the bleeps also to @p bleepL, @p bleepR. */
    void process(float* L, float* R, float* bleepL, float* bleepR, int n);

private:
    double sr_ = 48000.0;
    AtmosSettings s_;
    Rng rng_;
    Svf windL_, windR_;
    OuProcess wanderL_, wanderR_;
    float pinkL_ = 0.0f, pinkR_ = 0.0f;
    // One sweep at a time.
    Svf sweep_;
    double sweepAge_ = -1.0, sweepLen_ = 0.0, sweepFrom_ = 0.0, sweepTo_ = 0.0;
    float sweepPan_ = 0.0f;
    // One bleep burst at a time.
    int bleepsLeft_ = 0;
    double bleepAge_ = 0.0, bleepHz_ = 1000.0, bleepPhase_ = 0.0, bleepEvery_ = 0.1;
    float bleepPan_ = 0.0f;
    int64_t count_ = 0;
    float windNow_ = 0.0f;   ///< smoothed wind gain, so a knob's step does not click
};

} // namespace eph
