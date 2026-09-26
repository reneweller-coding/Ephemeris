/**
 * @file Poly.h
 * @brief The pad synth (25.09.2026): a polyphonic wavetable synth for the pads of the style guide's 7.1 -- the
 *        Oberheim and Juno pads of Stürtzer, Quaeschning and Redshift, and the Waldorf and PPG planes.
 *
 * **The voice.** Two oscillators per key, a few cents apart and spread across the stereo field, read one wavetable
 * (Wavetable.h) band-limited; a state-variable low pass of 12 dB per octave (the Oberheim SEM's kind) per side,
 * whose cutoff follows the key by half and the envelope by some octaves; a slow envelope (attack 1 to 4 s, release
 * 3 to 8 s in the guide's words). Eight keys; the oldest is taken when all sound.
 *
 * **What moves.** The one slow modulation a pad gets (0zk, "pulse-width for pad"): the position in the table swept
 * by a slow sine, each key a little apart in its phase -- on the PWM table that is the pulse width modulation of an
 * analog pad, on a PPG or Waldorf table the timbre walking through its frames. Each oscillator drifts in pitch by a
 * few cents, as analog ones do.
 *
 * **Envelopes and modulation** (26.09.2026). The amplitude has a full ADSR (attack, decay, sustain, release); the filter
 * follows it (as the pad had it) or has its own. Every key carries a modulation envelope, four LFOs and an eight-slot
 * matrix of its own (Modulation.h), evaluated on the control grid: the table's place, pitch, cutoff, resonance, the
 * filter's mode, the level and the key's place in the stereo field (the blend, the pulse width and the filter FM are
 * the voices' and do nothing here).
 *
 * **The ensemble.** A Juno-style chorus after the voices: one short delay line swept by a slow sine, read on the
 * left and, with the sweep inverted, on the right, and added to the dry sound -- width without losing the middle.
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/synth/Wavetable.h"
#include "eph/synth/Filters.h"
#include "eph/synth/Modulation.h"
#include <cstdint>
#include <vector>

namespace eph {

/** @brief Settings for the following samples. */
struct PolySettings {
    int table = 1;               ///< the wavetable (1: PWM, the analog pad)
    float position = 0.3f;       ///< 0..1 across the table's frames
    float scan = 0.35f;          ///< 0..1 how far the slow sine moves the position
    float scanHz = 0.06f;        ///< its rate
    float detuneCents = 8.0f;    ///< the two oscillators apart
    float spread = 0.6f;         ///< 0..1 the two oscillators apart in the stereo field
    float driftCents = 3.0f;     ///< the slow wandering of each oscillator's pitch
    float cutoffHz = 2200.0f;    ///< the low pass
    float resonance = 0.15f;     ///< 0..1
    float envOctaves = 1.0f;     ///< the envelope's push on the cutoff, octaves
    float attackS = 1.5f;        ///< swell
    float releaseS = 4.0f;       ///< fade after the key
    float chorus = 0.5f;         ///< 0..1 the ensemble's depth and mix
    int filter = 3;              ///< the filter model (Filters.h; 3: the Oberheim SEM)
    float filterMode = 0.0f;     ///< its mode (the SEM's morph, the Xpander's response ...)
    // 26.09.2026, the envelopes in full and the modulation (Modulation.h); only 4-byte members (compared bit for bit).
    float ampDecayS = 1.0f;      ///< the amplitude envelope's decay (its attack and release above)
    float ampSustain = 1.0f;     ///< ... its sustain level
    int filtLink = 1;            ///< 1: the filter follows the amplitude envelope; 0: its own below
    float filtAttackS = 2.0f, filtDecayS = 3.0f, filtSustain = 0.4f, filtReleaseS = 4.0f;   ///< the filter's own envelope
    float envVelocity = 0.0f;    ///< how far the velocity scales the filter envelope
    ModSettings mod;             ///< every key's modulation envelope, LFOs and matrix
};

/** @brief The pad synth; stereo out. */
class PolySynth {
public:
    static constexpr int kKeys = 8;   ///< polyphony
    void prepare(double sampleRate, uint64_t seed);   ///< sample rate; allocates the ensemble's line
    void set(const PolySettings& s);                  ///< takes the settings (every 32-sample cell)
    void noteOn(int pitch, float velocity, int id);   ///< presses a key
    void noteOff(int id);                             ///< releases a key
    void silence();                                   ///< every key off at once (a jump in the song)
    bool active() const;                              ///< whether anything sounds or rings in the ensemble
    void process(float* L, float* R, int n);          ///< renders @p n samples into @p L and @p R (overwritten)
    /** @brief The piece's beat at the next sample and beats per sample (the synced LFOs); call at every cell. */
    void setClock(double beat, double beatsPerSample) { beat0_ = beat; bps_ = beatsPerSample; clockAt_ = count_; }

private:
    struct Key {
        bool on = false, held = false;
        int pitch = 60, id = -1;
        float velocity = 0.8f;
        uint32_t order = 0;
        Envelope env;
        double phase[2] = { 0.0, 0.0 };
        double inc[2] = { 0.0, 0.0 };
        int level[2] = { -1, -1 };
        float drift[2] = { 0.0f, 0.0f };
        float scanOffset = 0.0f;   ///< the key's own place in the scan's cycle
        float position = 0.3f;     ///< where in the table it reads (the scan's value of the moment)
        FilterLane filt[2];        ///< left and right (Filters.h)
        float g = 0.1f;            ///< the filter's tan(pi fc / fs) of the moment
        Rng rng;                   ///< its own drift, so the order of the keys does not matter
        Envelope fenv;             ///< the filter's own envelope (filtLink 0)
        Modulator mod;             ///< its modulation (Modulation.h)
        float mo[kModDests] = {};  ///< the matrix's last sums
        float fk = 0.0f, mode = 0.0f, lv = 1.0f, panL = 1.0f, panR = 1.0f;   ///< resonance, mode, level, pan as the matrix moves them
    };
    void retune(Key& k);   ///< the oscillators' increments and levels from the pitch, detune, drift and the matrix's pitch
    double beatAt(int64_t at) const { return beat0_ + static_cast<double>(at - clockAt_) * bps_; }   ///< setClock's beat
    double sr_ = 48000.0;
    PolySettings s_;
    const CycleTable* table_ = nullptr;
    Key keys_[kKeys];
    Envelope times_;          ///< the settings' envelope times, copied to every key
    Envelope ftimes_;         ///< the filter envelope's
    double beat0_ = 0.0, bps_ = 0.0;   ///< the clock (setClock)
    int64_t clockAt_ = 0;
    uint32_t order_ = 0;
    double scanPhase_ = 0.0;
    int64_t count_ = 0;
    Rng rng_;
    std::vector<float> line_;
    size_t mask_ = 0, write_ = 0;
    double chorusPhase_ = 0.0;
    float ring_ = 0.0f;       ///< decaying peak of the output, for active()
};

} // namespace eph
