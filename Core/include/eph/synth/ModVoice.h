/**
 * @file ModVoice.h
 * @brief The modular voices of the sequencer rows, the lead and the drone (PLAN 5.2): two drifting
 *        VCOs, an overdriven mixer, the ladder, two envelopes, glide -- as one bank whose audio runs in
 *        SIMD lanes.
 *
 * **Signal path**, at twice the sample rate and decimated through a half-band filter (as Phosphene's
 * bass): VCO 1 and VCO 2 (PolyBLEP saw blended into a pulse, VCO 2 detuned) -> mixer with drive into
 * an antiderivative-antialiased sigmoid (the Minimoog mixer was overdriven on purpose) -> the four-pole
 * ZDF ladder (Ladder.h) -> a DC blocker at 8 Hz -> VCA. The blocker is the AC coupling of a modular's
 * output: a pulse of 30 % duty has a mean of 0.4, and the ladder, being a low pass, passes it
 * (measured on the first study: 0.029 of full scale left, 0.014 right before the blocker).
 *
 * **The bank.** The ten voices run side by side, one per lane (VoiceKernel.h): two AVX2 registers,
 * three NEON registers, run through each stage together. The bank is rendered when any voice runs; a
 * silent voice keeps its oscillators and filters going with its VCA at zero, which costs nothing on a
 * register that is computed anyway. What cannot run in lanes runs per voice here: glide, vibrato, the
 * envelopes, the drift, and the control step below.
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
 *
 * **Envelopes and modulation** (26.09.2026). The amplitude and the filter have full ADSRs; the filter's release is
 * linked to its decay by default (the Minimoog's habit, and the sound the voices had): with no sustain the decay then
 * simply runs on through the note's end, so a sequencer's short gates leave the hand's decay alone. A third envelope,
 * four LFOs and eight matrix slots (Modulation.h) reach pitch, pulse width, the blend, the table, cutoff, resonance,
 * the filter's mode and FM, the level and the pan; they are evaluated at the control steps below, and what they move
 * reaches the lanes per sample (VoiceKernel.h), so the modulation is the same for every host block size.
 *
 * **Classic VCOs** (26.09.2026, Vco.h). A voice with a VCO model, hard sync or cross mod plays the oscillators of
 * Vco.h instead of its own: on the scalar side, at the lanes' twice the rate and on their steps, handed to the
 * kernel as a wavetable's are. The model also scales the drift and adds its jitter.
 *
 * **Control rate.** Glide, vibrato and the envelopes run every sample; the oscillators' frequencies
 * and the ladder's cutoff follow them every 4 samples (12 kHz at 48 kHz) on the bank's absolute
 * sample raster, which saves two exponentials, an exp2 and a tan on three of four samples. The
 * ladder's zero-delay form takes the steps of its coefficient without clicks; the fastest thing it
 * follows, the 1.5 ms attack of the filter envelope, still gets 18 steps.
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/synth/Modulation.h"
#include "eph/synth/Vco.h"
#include "eph/synth/VoiceKernel.h"
#include "eph/synth/Wavetable.h"
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
    int table = 0;             ///< a wavetable instead of the analog oscillators: 0 none, else Wavetable.h's index + 1
    float tablePos = 0.0f;     ///< where in the table, 0..1
    float tableMod = 0.4f;     ///< how far the note's modulation step moves that place
    int filter = 0;            ///< the filter model (FilterModel, Filters.h)
    float filterMode = 0.0f;   ///< the SEM's morph, the Xpander's response, the Polivoks' band pass, the comb's sign
    float filterFm = 0.0f;     ///< oscillator 1 on the cutoff at audio rate, 0..1 (three octaves at full swing)
    // 26.09.2026: the envelopes in full and the modulation (Modulation.h). Only 4-byte members: the engine compares
    // settings bit for bit.
    float ampAttackMs = 2.0f;      ///< the amplitude envelope's attack
    float ampDecayMs = 50.0f;      ///< ... its decay to the sustain (its release: releaseMs)
    float ampSustain = 1.0f;       ///< ... its sustain level
    float filtAttackMs = 1.5f;     ///< the filter envelope's attack (its decay: decayMs)
    float filtSustain = 0.0f;      ///< ... its sustain level
    float filtReleaseMs = 180.0f;  ///< ... its release, when not linked to the decay
    int filtLink = 1;              ///< 1: the filter's release takes the decay's time
    float envVelocity = 0.0f;      ///< how far the velocity scales the filter envelope, 0..1
    ModSettings mod;               ///< the modulation envelope, the LFOs, the matrix
    // 26.09.2026, the classic VCOs (Vco.h).
    int vco = 0;                   ///< the model (VcoModel); 0: the voice's own oscillators
    int sync = 0;                  ///< 1: VCO 2 hard-synced to VCO 1
    float osc2Semis = 0.0f;        ///< VCO 2's interval, semitones
    float crossMod = 0.0f;         ///< VCO 1 on VCO 2's frequency at audio rate, 0..1 (three octaves at full swing)
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

/** @brief The modular voices, their audio in lanes (see the file comment). */
class ModVoiceBank {
public:
    /**
     * @brief Sets the output sample rate (the audio path runs at twice it) and each voice's own seed.
     * @param sampleRate output rate
     * @param seeds      one seed per voice, kBankVoices of them
     */
    void prepare(double sampleRate, const uint64_t* seeds);
    /** @brief Silences every voice and clears every state except the drift and the oscillators' phases. */
    void reset();
    /** @brief Takes voice @p v's settings for the following samples; call at every 32-sample cell. */
    void set(int v, const VoiceSettings& s);
    /**
     * @brief Starts a note on voice @p v.
     * @param v        the voice
     * @param pitch    MIDI note
     * @param velocity 0..1
     * @param accent   accented step
     * @param legato   glide from the sounding pitch without retriggering the envelopes
     * @param id       pairs the note with its noteOff()
     */
    void noteOn(int v, int pitch, float velocity, bool accent, bool legato, int id, float bright = 0.0f, float decay = 0.0f);
    /**
     * @brief Releases the note @p id on voice @p v if it is the one held. A mono voice ignores the off
     *        of a note a later one has already taken over -- by id, not by pitch, because two slides on
     *        the same pitch overlap.
     */
    void noteOff(int v, int id);
    /** @brief Whether voice @p v makes sound or is about to. */
    bool active(int v) const { return ctl_[v].amp.isActive(); }
    /** @brief Whether a key is held on voice @p v (a note is on and not yet released). */
    bool held(int v) const { return ctl_[v].held >= 0; }
    /**
     * @brief Renders @p n samples (at most kBankSpan) of every voice with @p run set; afterwards
     *        output(v) holds them. The whole bank runs when any voice does, a decision taken from
     *        @p run alone, so it is the same on every vector path and for every host block size.
     */
    void process(const bool* run, int n);
    /** @brief Voice @p v's last rendered samples (valid for the voices that ran). */
    const float* output(int v) const { return out_[v]; }
    /**
     * @brief The piece's clock for the synced LFOs: @p beat at the next sample to render, @p beatsPerSample from there.
     *        Call at every cell, before its notes.
     */
    void setClock(double beat, double beatsPerSample) { beat0_ = beat; bps_ = beatsPerSample; clockAt_ = count_; }
    /** @brief Voice @p v's pan offset from its modulation matrix (-1..1; the engine adds it to the strip's pan). */
    float panMod(int v) const
    {
        const Control& c = ctl_[v];
        return c.mod.active() && c.mod.targets(ModDest::Pan) ? c.mo[static_cast<int>(ModDest::Pan)] : 0.0f;
    }

private:
    /** @brief What runs per voice on the scalar side. */
    struct Control {
        VoiceSettings s;             ///< current settings
        Envelope filt;   ///< the filter envelope
        Envelope amp;   ///< the amplitude envelope
        OuProcess drift1;   ///< VCO 1's wandering, cents
        OuProcess drift2;   ///< VCO 2's
        Rng rng;                     ///< the voice's own stream
        double pitch = 45.0;         ///< sounding pitch (glides towards target)
        double target = 45.0;        ///< pitch of the held note
        double glideCoef = 1.0;      ///< glide per sample
        double noteCents = 0.0;      ///< small offset drawn per note
        int held = -1;               ///< id of the held note, -1 if none
        float velocity = 0.8f;       ///< of the current note
        float accentAmt = 0.0f;      ///< accent of the current note
        float noteOct = 0.0f;       ///< the note's cutoff offset (the modulation sequencer), octaves
        float decayMul = 1.0f;      ///< the note's filter decay factor (the second lane), 2^decay
        float noteBright = 0.0f;    ///< the note's modulation step, which also moves its place in a wavetable
        const CycleTable* table = nullptr;   ///< the wavetable, if the voice reads one
        double wph1 = 0.0;   ///< table oscillator 1's phase
        double wph2 = 0.0;   ///< table oscillator 2's phase
        int wlev1 = -1;   ///< table oscillator 1's band-limited level
        int wlev2 = -1;   ///< table oscillator 2's
        double vibPhase = 0.0;       ///< vibrato phase in cycles
        double vibLevel = 0.0;       ///< 0..1, rises while a note is held
        double vibCoef = 0.0;        ///< vibrato fade-in per sample
        float dt1 = 0.001f;   ///< VCO 1's phase step
        float inv1 = 1000.0f;   ///< ... and its inverse
        float dt2 = 0.001f;   ///< VCO 2's phase step
        float inv2 = 1000.0f;   ///< ... and its inverse
        float g = 0.1f;   ///< the filter's tan(pi fc / fs) of the moment
        bool fresh = true;           ///< a control step is due at the next sample whatever the raster (a new note)
        Modulator mod;               ///< the modulation envelope, the LFOs, the matrix (Modulation.h)
        float mo[kModDests] = {};    ///< the matrix's last sums per destination
        /** @brief What the lanes get per sample: blend, pulse width, feedback, makeup, mode, FM depth; the level
         *         factor and the place in the table (the knobs', moved by the matrix). */
        float wv = 0.0f;   ///< what the lanes get: the blend
        float pwv = 0.5f;   ///< ... the pulse width
        float kv = 0.0f;   ///< ... the feedback
        float mkv = 1.0f;   ///< ... the makeup
        float modev = 0.0f;   ///< ... the filter's mode
        float ffmv = 0.0f;   ///< ... the filter FM depth
        float levelv = 1.0f;   ///< ... the level factor
        float tposv = 0.0f;   ///< ... the place in the table
        bool model = false;          ///< a VCO model, sync or cross mod: the oscillators of Vco.h play
        VcoOsc osc1;   ///< the first VCO model
        VcoOsc osc2;   ///< the second
        OuProcess jitter1;   ///< the first model's fast pitch jitter, cents
        OuProcess jitter2;   ///< the second's
        Rng jrng;                    ///< its own stream (the drift's stays as it was)
    };
    /** @brief Voice @p v's scalar side for sample @p i of the span, written into the lanes. */
    void control(int v, int i);
    /** @brief Voice @p v's per-sample values (Control::wv ..) from its knobs and its matrix's last sums. */
    void shape(int v);
    /** @brief The filter envelope's times from the knobs and the note's decay factor. */
    static void filterTimes(Control& c);
    /** @brief The piece's beat at sample @p at (setClock). */
    double beatAt(int64_t at) const { return beat0_ + static_cast<double>(at - clockAt_) * bps_; }

    double sr_ = 48000.0;   ///< the sample rate, Hz
    double stepBase_ = 0.0;      ///< log2 of the phase step at 2x of MIDI note 0
    int64_t count_ = 0;          ///< samples rendered, for the drift and control rasters
    Control ctl_[kBankLanes];   ///< every voice's scalar side
    VoiceLanes lanes_;   ///< the voices in lanes (the kernel)
    alignas(32) float mixed_[kBankSpan * kBankLanes] = {};   ///< the kernel's output, per sample and lane
    float out_[kBankLanes][kBankSpan] = {};                  ///< the same per voice
    float tpos_[kBankLanes][kBankSpan] = {};                 ///< the place in the wavetable per voice and sample
    double beat0_ = 0.0;   ///< the clock: the beat at clockAt_
    double bps_ = 0.0;   ///< beats per sample
    int64_t clockAt_ = 0;                                    ///< ... anchored at this sample
};

} // namespace eph
