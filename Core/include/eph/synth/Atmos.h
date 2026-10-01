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
 * - **Grains** (25.09.2026): a cloud of short tones -- each a sine in a Hann window, 40 to 250 ms, on a note of
 *   the scale over the current root two to three octaves up, at its own place in the stereo field -- started
 *   as a Poisson process at `grainsPerSecond`, up to 24 at once. The granular shimmer of the space sections.
 *   They draw on a stream of their own, so switching them on moves no other layer's randomness.
 * Deterministic: one stream (and the grains' own), seeded once; the rates are drawn per 32-sample cell.
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
    int rootPc = 9;                ///< the root the bleeps and the grains take their notes from
    int scale = 0;                 ///< compose.scale order
    float grainGain = 0.0f;        ///< linear, 0 = no grains
    float grainsPerSecond = 12.0f; ///< the cloud's density
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
    double sr_ = 48000.0;   ///< the sample rate, Hz
    AtmosSettings s_;   ///< the settings
    Rng rng_;   ///< the noise's stream
    Svf windL_;   ///< the wind's band pass, left
    Svf windR_;   ///< ... right
    OuProcess wanderL_;   ///< the wind's slow wandering, left
    OuProcess wanderR_;   ///< ... right
    float pinkL_ = 0.0f;   ///< the pinking filter's state, left
    float pinkR_ = 0.0f;   ///< ... right
    /// One sweep at a time.
    Svf sweep_;
    double sweepAge_ = -1.0;   ///< seconds into the sweep, -1 none
    double sweepLen_ = 0.0;   ///< its length, s
    double sweepFrom_ = 0.0;   ///< its start frequency, Hz
    double sweepTo_ = 0.0;   ///< its end frequency, Hz
    float sweepPan_ = 0.0f;   ///< its place, -1 .. 1
    /// One bleep burst at a time.
    int bleepsLeft_ = 0;
    double bleepAge_ = 0.0;   ///< seconds since the last bleep
    double bleepHz_ = 1000.0;   ///< the bleep's pitch, Hz
    double bleepPhase_ = 0.0;   ///< the bleep's phase
    double bleepEvery_ = 0.1;   ///< seconds between the bleeps of the burst
    float bleepPan_ = 0.0f;   ///< the burst's place, -1 .. 1
    int64_t count_ = 0;   ///< samples rendered
    float windNow_ = 0.0f;   ///< smoothed wind gain, so a knob's step does not click
    /** @brief One grain of the cloud. */
    struct Grain {
        bool on = false;   ///< it sounds
        double phase = 0.0;   ///< the sine's phase, cycles
        double inc = 0.0;   ///< its step, cycles per sample
        double age = 0.0;   ///< seconds since it began
        double length = 0.1;   ///< its length, s
        float amp = 0.0f;   ///< its amplitude
        float gainL = 0.0f;   ///< its place: gain left
        float gainR = 0.0f;   ///< ... gain right
    };
    static constexpr int kGrains = 24;   ///< grains at most
    Grain grains_[kGrains];   ///< the grains
    int grainsOn_ = 0;       ///< grains sounding
    Rng grainRng_;           ///< the grains' own stream
};

} // namespace eph
