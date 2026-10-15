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
    float m_wave = 0.0f;         ///< 0 saw .. 1 pulse
    float m_detuneCents = 7.0f;  ///< VCO 2 against VCO 1
    float m_pulseWidth = 0.5f;   ///< duty cycle
    float m_driftCents = 3.0f;   ///< stationary spread of the drift per VCO
    float m_driveDb = 6.0f;      ///< mixer drive
    float m_cutoffHz = 600.0f;   ///< ladder cutoff before envelope and key tracking
    float m_resonance = 0.35f;   ///< 0..1, self-oscillation near 1
    float m_envOctaves = 2.5f;   ///< filter envelope depth
    float m_decayMs = 180.0f;    ///< filter envelope decay
    float m_keyTrack = 0.5f;     ///< 0..1 of an octave per octave
    float m_accent = 0.5f;       ///< how much an accent adds
    float m_releaseMs = 60.0f;   ///< amplitude release
    float m_glideMs = 60.0f;     ///< portamento time on a slide
    float m_vibratoCents = 0.0f; ///< vibrato depth; it comes in over 0.4 s of a held note, as a hand reaches the wheel
    float m_vibratoHz = 5.2f;    ///< vibrato rate
    int m_table = 0;             ///< a wavetable instead of the analog oscillators: 0 none, else Wavetable.h's index + 1
    float m_tablePos = 0.0f;     ///< where in the table, 0..1
    float m_tableMod = 0.4f;     ///< how far the note's modulation step moves that place
    int m_filter = 0;            ///< the filter model (FilterModel, Filters.h)
    float m_filterMode = 0.0f;   ///< the SEM's morph, the Xpander's response, the Polivoks' band pass, the comb's sign
    float m_filterFm = 0.0f;     ///< oscillator 1 on the cutoff at audio rate, 0..1 (three octaves at full swing)
    // 26.09.2026: the envelopes in full and the modulation (Modulation.h). Only 4-byte members: the engine compares
    // settings bit for bit.
    float m_ampAttackMs = 2.0f;      ///< the amplitude envelope's attack
    float m_ampDecayMs = 50.0f;      ///< ... its decay to the sustain (its release: releaseMs)
    float m_ampSustain = 1.0f;       ///< ... its sustain level
    float m_filtAttackMs = 1.5f;     ///< the filter envelope's attack (its decay: decayMs)
    float m_filtSustain = 0.0f;      ///< ... its sustain level
    float m_filtReleaseMs = 180.0f;  ///< ... its release, when not linked to the decay
    int m_filtLink = 1;              ///< 1: the filter's release takes the decay's time
    float m_envVelocity = 0.0f;      ///< how far the velocity scales the filter envelope, 0..1
    ModSettings m_mod;               ///< the modulation envelope, the LFOs, the matrix
    // 26.09.2026, the classic VCOs (Vco.h).
    int m_vco = 0;                   ///< the model (VcoModel); 0: the voice's own oscillators
    int m_sync = 0;                  ///< 1: VCO 2 hard-synced to VCO 1
    float m_osc2Semis = 0.0f;        ///< VCO 2's interval, semitones
    float m_crossMod = 0.0f;         ///< VCO 1 on VCO 2's frequency at audio rate, 0..1 (three octaves at full swing)
};

/**
 * @brief An Ornstein-Uhlenbeck process: dx = -x/tau dt + sigma sqrt(2/tau) dW.
 *
 * The update is the exact discretisation x <- a x + sigma sqrt(1 - a^2) n with a = exp(-dt/tau), so
 * the stationary standard deviation is sigma for any step size.
 */
struct OuProcess {
    double m_x = 0.0;   ///< current value
    /** @brief One step of @p dt seconds with time constant @p tau and spread @p sigma, noise from @p rng. */
    double step(double dt, double tau, double sigma, Rng& rng)
    {
        const double a = std::exp(-dt / tau);
        m_x = a * m_x + sigma * std::sqrt(1.0 - a * a) * rng.gaussian();
        return m_x;
    }
};

/** @brief The modular voices, their audio in lanes (see the file comment). */
class ModVoiceBank {
public:
    /**
     * @brief Sets the output sample rate (the audio path runs at twice it) and each voice's own seed.
     * @param sampleRate output rate
     * @param seeds      one seed per voice, BankVoices of them
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
    bool active(int v) const { return m_ctl[v].m_amp.isActive(); }
    /** @brief Whether a key is held on voice @p v (a note is on and not yet released). */
    bool held(int v) const { return m_ctl[v].m_held >= 0; }
    /**
     * @brief Renders @p n samples (at most BankSpan) of every voice with @p run set; afterwards
     *        output(v) holds them. The whole bank runs when any voice does, a decision taken from
     *        @p run alone, so it is the same on every vector path and for every host block size.
     */
    void process(const bool* run, int n);
    /** @brief Voice @p v's last rendered samples (valid for the voices that ran). */
    const float* output(int v) const { return m_out[v]; }
    /**
     * @brief The piece's clock for the synced LFOs: @p beat at the next sample to render, @p beatsPerSample from there.
     *        Call at every cell, before its notes.
     */
    void setClock(double beat, double beatsPerSample) { m_beat0 = beat; m_bps = beatsPerSample; m_clockAt = m_count; }
    /** @brief Voice @p v's pan offset from its modulation matrix (-1..1; the engine adds it to the strip's pan). */
    float panMod(int v) const
    {
        const Control& c = m_ctl[v];
        return c.m_mod.active() && c.m_mod.targets(ModDestE::PAN) ? c.m_mo[static_cast<int>(ModDestE::PAN)] : 0.0f;
    }

private:
    /** @brief What runs per voice on the scalar side. */
    struct Control {
        VoiceSettings m_s;             ///< current settings
        Envelope m_filt;   ///< the filter envelope
        Envelope m_amp;   ///< the amplitude envelope
        OuProcess m_drift1;   ///< VCO 1's wandering, cents
        OuProcess m_drift2;   ///< VCO 2's
        Rng m_rng;                     ///< the voice's own stream
        double m_pitch = 45.0;         ///< sounding pitch (glides towards target)
        double m_target = 45.0;        ///< pitch of the held note
        double m_glideCoef = 1.0;      ///< glide per sample
        double m_noteCents = 0.0;      ///< small offset drawn per note
        int m_held = -1;               ///< id of the held note, -1 if none
        float m_velocity = 0.8f;       ///< of the current note
        float m_accentAmt = 0.0f;      ///< accent of the current note
        float m_noteOct = 0.0f;       ///< the note's cutoff offset (the modulation sequencer), octaves
        float m_decayMul = 1.0f;      ///< the note's filter decay factor (the second lane), 2^decay
        float m_noteBright = 0.0f;    ///< the note's modulation step, which also moves its place in a wavetable
        const CycleTable* m_table = nullptr;   ///< the wavetable, if the voice reads one
        double m_wph1 = 0.0;   ///< table oscillator 1's phase
        double m_wph2 = 0.0;   ///< table oscillator 2's phase
        int m_wlev1 = -1;   ///< table oscillator 1's band-limited level
        int m_wlev2 = -1;   ///< table oscillator 2's
        double m_vibPhase = 0.0;       ///< vibrato phase in cycles
        double m_vibLevel = 0.0;       ///< 0..1, rises while a note is held
        double m_vibCoef = 0.0;        ///< vibrato fade-in per sample
        float m_dt1 = 0.001f;   ///< VCO 1's phase step
        float m_inv1 = 1000.0f;   ///< ... and its inverse
        float m_dt2 = 0.001f;   ///< VCO 2's phase step
        float m_inv2 = 1000.0f;   ///< ... and its inverse
        float m_g = 0.1f;   ///< the filter's tan(pi fc / fs) of the moment
        bool m_fresh = true;           ///< a control step is due at the next sample whatever the raster (a new note)
        Modulator m_mod;               ///< the modulation envelope, the LFOs, the matrix (Modulation.h)
        float m_mo[ModDests] = {};    ///< the matrix's last sums per destination
        /** @brief What the lanes get per sample: blend, pulse width, feedback, makeup, mode, FM depth; the level
         *         factor and the place in the table (the knobs', moved by the matrix). */
        float m_wv = 0.0f;   ///< what the lanes get: the blend
        float m_pwv = 0.5f;   ///< ... the pulse width
        float m_kv = 0.0f;   ///< ... the feedback
        float m_mkv = 1.0f;   ///< ... the makeup
        float m_modev = 0.0f;   ///< ... the filter's mode
        float m_ffmv = 0.0f;   ///< ... the filter FM depth
        float m_levelv = 1.0f;   ///< ... the level factor
        float m_tposv = 0.0f;   ///< ... the place in the table
        bool m_model = false;          ///< a VCO model, sync or cross mod: the oscillators of Vco.h play
        VcoOsc m_osc1;   ///< the first VCO model
        VcoOsc m_osc2;   ///< the second
        OuProcess m_jitter1;   ///< the first model's fast pitch jitter, cents
        OuProcess m_jitter2;   ///< the second's
        Rng m_jrng;                    ///< its own stream (the drift's stays as it was)
    };
    /** @brief Voice @p v's scalar side for sample @p i of the span, written into the lanes. */
    void control(int v, int i);
    /** @brief Voice @p v's per-sample values (Control::wv ..) from its knobs and its matrix's last sums. */
    void shape(int v);
    /** @brief The filter envelope's times from the knobs and the note's decay factor. */
    static void filterTimes(Control& c);
    /** @brief The piece's beat at sample @p at (setClock). */
    double beatAt(int64_t at) const { return m_beat0 + static_cast<double>(at - m_clockAt) * m_bps; }

    double m_sr = 48000.0;   ///< the sample rate, Hz
    double m_stepBase = 0.0;      ///< log2 of the phase step at 2x of MIDI note 0
    int64_t m_count = 0;          ///< samples rendered, for the drift and control rasters
    Control m_ctl[BankLanes];   ///< every voice's scalar side
    VoiceLanes m_lanes;   ///< the voices in lanes (the kernel)
    alignas(32) float m_mixed[BankSpan * BankLanes] = {};   ///< the kernel's output, per sample and lane
    float m_out[BankLanes][BankSpan] = {};                  ///< the same per voice
    float m_tpos[BankLanes][BankSpan] = {};                 ///< the place in the wavetable per voice and sample
    double m_beat0 = 0.0;   ///< the clock: the beat at m_clockAt
    double m_bps = 0.0;   ///< beats per sample
    int64_t m_clockAt = 0;                                    ///< ... anchored at this sample
};

} // namespace eph
