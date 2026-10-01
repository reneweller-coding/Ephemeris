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
    float filtAttackS = 2.0f;   ///< the filter's own envelope: attack, s
    float filtDecayS = 3.0f;   ///< ... decay, s
    float filtSustain = 0.4f;   ///< ... sustain level
    float filtReleaseS = 4.0f;   ///< ... release, s
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
    /** @brief One key: its two oscillators, its envelopes, its filters, its modulation. */
    struct Key {
        bool on = false;   ///< it sounds
        bool held = false;   ///< it is held
        int pitch = 60;   ///< MIDI note
        int id = -1;   ///< the note id it plays, -1 none
        float velocity = 0.8f;   ///< its velocity, 0..1
        uint32_t order = 0;   ///< when it was pressed (the oldest is taken)
        Envelope env;   ///< the amplitude envelope
        double phase[2] = { 0.0, 0.0 };   ///< the two oscillators' phases
        double inc[2] = { 0.0, 0.0 };   ///< their phase steps per sample
        int level[2] = { -1, -1 };   ///< the mip level each reads (-1: not yet chosen)
        float drift[2] = { 0.0f, 0.0f };   ///< each oscillator's slow drift, cents
        float scanOffset = 0.0f;   ///< the key's own place in the scan's cycle
        float position = 0.3f;     ///< where in the table it reads (the scan's value of the moment)
        FilterLane filt[2];        ///< left and right (Filters.h)
        float g = 0.1f;            ///< the filter's tan(pi fc / fs) of the moment
        Rng rng;                   ///< its own drift, so the order of the keys does not matter
        Envelope fenv;             ///< the filter's own envelope (filtLink 0)
        Modulator mod;             ///< its modulation (Modulation.h)
        float mo[kModDests] = {};  ///< the matrix's last sums
        float fk = 0.0f;   ///< the resonance as the matrix moves it
        float mode = 0.0f;   ///< the filter's mode as the matrix moves it
        float lv = 1.0f;   ///< the level as the matrix moves it
        float panL = 1.0f;   ///< the pan's gain, left
        float panR = 1.0f;   ///< ... right
    };
    void retune(Key& k);   ///< the oscillators' increments and levels from the pitch, detune, drift and the matrix's pitch
    /** @brief The keys' filters side by side (26.09.2026): key k's left channel in lane 2k, its right in lane 2k + 1, run
     *         in SIMD registers as the voice bank's are (the comb keeps the keys' FilterLanes, whose lines it needs). */
    static constexpr int kLanes = 2 * kKeys;
    alignas(32) float fv_[4][kLanes] = {};   ///< the filters' node voltages, four per lane
    alignas(32) float fs_[4][kLanes] = {};   ///< the filters' states, four per lane
    void clearLanes(int key);   ///< the key's two lanes from rest (a fresh key, a new model)
    double beatAt(int64_t at) const { return beat0_ + static_cast<double>(at - clockAt_) * bps_; }   ///< setClock's beat
    double sr_ = 48000.0;   ///< sample rate
    PolySettings s_;   ///< the settings
    const CycleTable* table_ = nullptr;   ///< the wavetable it reads
    Key keys_[kKeys];   ///< the keys
    Envelope times_;          ///< the settings' envelope times, copied to every key
    Envelope ftimes_;         ///< the filter envelope's
    double beat0_ = 0.0;   ///< the clock: the beat at clockAt_
    double bps_ = 0.0;   ///< beats per sample
    int64_t clockAt_ = 0;   ///< the sample setClock() was called at
    uint32_t order_ = 0;   ///< counts the keys pressed
    double scanPhase_ = 0.0;   ///< the scan LFO's phase
    int64_t count_ = 0;   ///< samples rendered
    Rng rng_;   ///< the seed stream of the keys
    std::vector<float> line_;   ///< the ensemble's delay line
    size_t mask_ = 0;   ///< its size - 1
    size_t write_ = 0;   ///< where the next sample goes
    double chorusPhase_ = 0.0;   ///< the ensemble's LFO phase
    float ring_ = 0.0f;       ///< decaying peak of the output, for active()
};

} // namespace eph
