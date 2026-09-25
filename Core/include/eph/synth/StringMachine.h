/**
 * @file StringMachine.h
 * @brief The string machine (PLAN 5.5): divide-down saws and the ensemble that makes them strings.
 *
 * **Divide-down.** A string ensemble of the seventies had no oscillator per key: a top-octave generator
 * made the twelve highest notes and flip-flops divided them down, so every C on the keyboard is locked
 * in phase to every other C. Here each pitch class has one phase counter at the top octave (C8 up),
 * kept modulo 2^8, and a note's phase is that counter divided by the power of two of its octave -- so
 * the octaves of a note are phase-locked, and a chord has the static, organ-like sound the ensemble then
 * animates. Each note is a PolyBLEP saw at 8' plus one an octave up at 4', mixed by `feet`.
 *
 * **The ensemble.** What the ear knows as the sound of these machines is the chorus after them: three
 * bucket-brigade delays around 6 ms, each swept by the sum of a slow (0.6 Hz) and a fast (6 Hz) LFO,
 * the three a third of a cycle apart, mixed to two channels -- the widely described design of the ARP
 * Solina's ensemble (details to be checked, PLAN 5.5). The BBDs' losses are a low pass at 7 kHz in each
 * line; a full BBD model after Holters and Parker (DAFx 2018) is for later.
 *
 * Polyphonic (12 keys), slow attack and release after the machines' crescendo and sustain sliders.
 */
#pragma once
#include "eph/Dsp.h"
#include <cstdint>
#include <vector>

namespace eph {

/** @brief Settings for the following samples. */
struct StringSettings {
    float attackS = 0.35f;     ///< crescendo
    float releaseS = 1.2f;     ///< sustain after the key
    float feet = 0.4f;         ///< 0: 8' only .. 1: 4' as loud as 8'
    float toneHz = 5000.0f;    ///< low pass after the dividers
    float ensemble = 0.8f;     ///< 0..1 depth of the ensemble
};

/** @brief The string machine; stereo out (the ensemble makes the width). */
class StringMachine {
public:
    static constexpr int kKeys = 12;   ///< polyphony
    /** @brief Sample rate; allocates the ensemble's lines. */
    void prepare(double sampleRate, uint64_t seed);
    /** @brief Takes the settings; call at every 32-sample cell. */
    void set(const StringSettings& s);
    void noteOn(int pitch, float velocity, int id);   ///< presses a key (the oldest is taken when all sound)
    void noteOff(int id);                             ///< releases a key
    void silence();                                   ///< every key off at once (a jump in the song)
    bool active() const;                              ///< whether anything sounds or rings in the ensemble
    /** @brief Renders @p n samples into @p L and @p R (overwritten). */
    void process(float* L, float* R, int n);

private:
    struct Key {
        bool on = false, held = false;
        int pitch = 60, id = -1;
        float velocity = 0.8f, env = 0.0f;
        uint32_t order = 0;
        int pc = 0;                  ///< pitch class: the top-octave generator the key divides
        double inv8 = 1.0, inv4 = 1.0;   ///< 1 / the 8' and 4' dividers (powers of two, so exact)
    };
    double sr_ = 48000.0;
    StringSettings s_;
    Key keys_[kKeys];
    double counter_[12] = {};   ///< top-octave phase counters per pitch class, modulo 256
    uint32_t order_ = 0;
    Svf tone_;
    std::vector<float> line_;
    size_t mask_ = 0, write_ = 0;
    double slow_ = 0.0, fast_ = 0.0;
    Svf bbd_[3];
    float ring_ = 0.0f;         ///< recent output level, so the ensemble can ring out
};

} // namespace eph
