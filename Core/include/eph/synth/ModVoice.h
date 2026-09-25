/**
 * @file ModVoice.h
 * @brief The modular voice of a sequencer row (PLAN 5.2): two drifting VCOs, an overdriven mixer,
 *        the ladder, two envelopes, glide.
 *
 * **Signal path**, at twice the sample rate and decimated through a half-band filter at the end (as
 * Phosphene's bass): VCO 1 and VCO 2 (PolyBLEP saw blended into a pulse, VCO 2 detuned) -> mixer with
 * drive into an antiderivative-antialiased tanh (the Minimoog mixer was overdriven on purpose) -> the
 * four-pole ZDF ladder (Ladder.h) -> VCA -> a DC blocker at 8 Hz. The blocker is the AC coupling of a
 * modular's output: a pulse of 30 % duty has a mean of 0.4, and the ladder, being a low pass, passes it
 * (measured on the first study: 0.029 of full scale left, 0.014 right before the blocker).
 *
 * **Free-running oscillators.** Unlike the psytrance bass, the oscillators are not reset on a note:
 * a modular's VCOs run on, so every note of a sequence starts at another phase and the line breathes
 * a little in its attacks, which is part of the sound.
 *
 * **Drift.** Each VCO's pitch wanders as an Ornstein-Uhlenbeck process (Uhlenbeck and Ornstein 1930):
 * mean-reverting noise with a time constant of a quarter of a minute and a stationary spread of
 * `voice.drift` cents. It is updated every 32 samples on the voice's own stream, so it is deterministic
 * and does not depend on the host's block size. The magnitudes are a first guess (PLAN 5.2) and are to
 * be measured.
 *
 * **Filter.** Cutoff in Hz from the knob, raised by the filter envelope (`env_amount` octaves, more on
 * an accent) and by key tracking. The knobs the player's hand moves -- cutoff, resonance, envelope
 * amount, decay -- come in already offset by the gestures (Engine.cpp).
 */
#pragma once
#include "eph/Adaa.h"
#include "eph/Dsp.h"
#include "eph/Halfband.h"
#include "eph/synth/Ladder.h"
#include "eph/synth/Oscillator.h"
#include <cstdint>

namespace eph {

/** @brief The settings of a voice for one 32-sample cell, in real units (gestures applied). */
struct VoiceSettings {
    float wave = 0.0f;         ///< 0 saw .. 1 pulse
    float detuneCents = 7.0f;  ///< VCO 2 against VCO 1
    float pulseWidth = 0.5f;   ///< duty cycle
    float driftCents = 3.0f;   ///< stationary spread of the drift per VCO
    float driveDb = 6.0f;      ///< mixer drive
    float cutoffHz = 600.0f;   ///< ladder cutoff before envelope and key tracking
    float resonance = 0.35f;   ///< 0..1, self-oscillation near 1
    float envOctaves = 2.5f;   ///< filter envelope depth
    float decayMs = 180.0f;    ///< filter envelope decay
    float keyTrack = 0.5f;     ///< 0..1 of an octave per octave
    float accent = 0.5f;       ///< how much an accent adds
    float releaseMs = 60.0f;   ///< amplitude release
    float glideMs = 60.0f;     ///< portamento time on a slide
    float vibratoCents = 0.0f; ///< vibrato depth; it comes in over 0.4 s of a held note, as a hand reaches the wheel
    float vibratoHz = 5.2f;    ///< vibrato rate
};

/**
 * @brief An Ornstein-Uhlenbeck process: dx = -x/tau dt + sigma sqrt(2/tau) dW.
 *
 * The update is the exact discretisation x <- a x + sigma sqrt(1 - a^2) n with a = exp(-dt/tau), so
 * the stationary standard deviation is sigma for any step size.
 */
struct OuProcess {
    double x = 0.0;   ///< current value
    /** @brief One step of @p dt seconds with time constant @p tau and spread @p sigma, noise from @p rng. */
    double step(double dt, double tau, double sigma, Rng& rng)
    {
        const double a = std::exp(-dt / tau);
        x = a * x + sigma * std::sqrt(1.0 - a * a) * rng.gaussian();
        return x;
    }
};

/** @brief One monophonic modular voice. */
class ModVoice {
public:
    /** @brief Sets the output sample rate (the voice runs at twice it) and the voice's own seed. */
    void prepare(double sampleRate, uint64_t seed);
    /** @brief Silences and clears every state except the drift. */
    void reset();
    /**
     * @brief Starts a note.
     * @param pitch    MIDI note
     * @param velocity 0..1
     * @param accent   accented step
     * @param legato   glide from the sounding pitch without retriggering the envelopes
     * @param id       pairs the note with its noteOff()
     */
    void noteOn(int pitch, float velocity, bool accent, bool legato, int id);
    /**
     * @brief Releases the note @p id if it is the one held. A mono voice ignores the off of a note a
     *        later one has already taken over -- by id, not by pitch, because two slides on the same
     *        pitch overlap.
     */
    void noteOff(int id);
    /** @brief Whether the voice makes sound or is about to. */
    bool active() const { return amp_.isActive(); }
    /** @brief Whether a key is held (a note is on and not yet released). */
    bool held() const { return held_ >= 0; }
    /** @brief Takes the settings for the following samples; call at every 32-sample cell. */
    void set(const VoiceSettings& s);
    /** @brief Renders @p n samples into @p out (overwritten). */
    void process(float* out, int n);

private:
    double sr_ = 48000.0, sr2_ = 96000.0;
    VoiceSettings s_;
    VaOscillator osc1_, osc2_;
    LadderT<float> ladder_;
    HalfbandDown<float> down_;
    TanhAdaa sat_;
    DcBlocker dc_;
    Envelope filt_, amp_;
    OuProcess drift1_, drift2_;
    Rng rng_;
    int64_t sampleCount_ = 0;   ///< samples rendered, for the 32-sample drift raster
    double pitch_ = 45.0;       ///< sounding pitch (glides towards target_)
    double target_ = 45.0;
    double glideCoef_ = 1.0;
    double noteCents_ = 0.0;    ///< small offset drawn per note
    int held_ = -1;             ///< id of the held note, -1 if none
    float velocity_ = 0.8f;
    float accentAmt_ = 0.0f;
    float driveGain_ = 2.0f, driveNorm_ = 0.5f;
    double vibPhase_ = 0.0;     ///< vibrato phase in cycles
    double vibLevel_ = 0.0;     ///< 0..1, rises while a note is held
    double vibCoef_ = 0.0;
};

} // namespace eph
