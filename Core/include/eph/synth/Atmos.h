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
 * @author Rene Weller
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/synth/ModVoice.h"   // OuProcess
#include <cstdint>

namespace eph
{

/** @brief Settings for the following samples. */
struct AtmosSettings
{
    float m_windGain = 0.0f;           ///< linear
    float m_windHz = 700.0f;           ///< centre of the wind's band
    float m_sweepsPerMinute = 0.0f;    ///< rate of sweeps
    float m_sweepGain = 0.0f;          ///< linear
    float m_bleepsPerMinute = 0.0f;    ///< rate of bleep bursts
    float m_bleepGain = 0.0f;          ///< linear
    int   m_rootPc = 9;                ///< the root the bleeps and the grains take their notes from
    int   m_scale = 0;                 ///< compose.scale order
    float m_grainGain = 0.0f;          ///< linear, 0 = no grains
    float m_grainsPerSecond = 12.0f;   ///< the cloud's density
};

/** @brief The atmosphere: stereo out, plus a separate bleep output for the echo send. */
class Atmos
{
public:
    /**
     * @brief Sample rate and seed.
     * @param[in] sampleRate  the sample rate in Hz
     * @param[in] seed        the seed
     */
    void prepare( double sampleRate, uint64_t seed );
    /**
     * @brief Takes the settings; call at every 32-sample cell.
     * @param[in] s  the settings
     */
    void set( const AtmosSettings &s );
    /**
     * @brief Whether any layer is audible or ringing.
     * @return true while any layer is audible or ringing
     */
    bool active() const;
    /**
     * @brief Adds @p n samples to @p L, @p R; the bleeps also to @p bleepL, @p bleepR.
     * @param[in,out] L       the left channel, added to
     * @param[in,out] R       the right channel, added to
     * @param[in,out] bleepL  the bleeps' left channel, added to
     * @param[in,out] bleepR  the bleeps' right channel, added to
     * @param[in]     n       how many samples
     */
    void process( float *L, float *R, float *bleepL, float *bleepR, int n );

private:
    double        m_sr = 48000.0;   ///< the sample rate, Hz
    AtmosSettings m_s;              ///< the settings
    Rng           m_rng;            ///< the noise's stream
    Svf           m_windL;          ///< the wind's band pass, left
    Svf           m_windR;          ///< ... right
    OuProcess     m_wanderL;        ///< the wind's slow wandering, left
    OuProcess     m_wanderR;        ///< ... right
    float         m_pinkL = 0.0f;   ///< the pinking filter's state, left
    float         m_pinkR = 0.0f;   ///< ... right
    /// One sweep at a time.
    Svf    m_sweep;
    double m_sweepAge = -1.0;   ///< seconds into the sweep, -1 none
    double m_sweepLen = 0.0;    ///< its length, s
    double m_sweepFrom = 0.0;   ///< its start frequency, Hz
    double m_sweepTo = 0.0;     ///< its end frequency, Hz
    float  m_sweepPan = 0.0f;   ///< its place, -1 .. 1
    /// One bleep burst at a time.
    int     m_bleepsLeft = 0;
    double  m_bleepAge = 0.0;     ///< seconds since the last bleep
    double  m_bleepHz = 1000.0;   ///< the bleep's pitch, Hz
    double  m_bleepPhase = 0.0;   ///< the bleep's phase
    double  m_bleepEvery = 0.1;   ///< seconds between the bleeps of the burst
    float   m_bleepPan = 0.0f;    ///< the burst's place, -1 .. 1
    int64_t m_count = 0;          ///< samples rendered
    float   m_windNow = 0.0f;     ///< smoothed wind gain, so a knob's step does not click
    /** @brief One grain of the cloud. */
    struct Grain
    {
        bool   m_on = false;     ///< it sounds
        double m_phase = 0.0;    ///< the sine's phase, cycles
        double m_inc = 0.0;      ///< its step, cycles per sample
        double m_age = 0.0;      ///< seconds since it began
        double m_length = 0.1;   ///< its length, s
        float  m_amp = 0.0f;     ///< its amplitude
        float  m_gainL = 0.0f;   ///< its place: gain left
        float  m_gainR = 0.0f;   ///< ... gain right
    };
    static constexpr int Grains = 24;        ///< grains at most
    Grain                m_grains[Grains];   ///< the grains
    int                  m_grainsOn = 0;     ///< grains sounding
    Rng                  m_grainRng;         ///< the grains' own stream
};

}   // namespace eph
