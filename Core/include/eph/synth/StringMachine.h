/**
 * @file StringMachine.h
 * @brief The string machine (PLAN 5.5): divide-down registers, a registration that morphs and moves, and the
 *        ensemble and phaser that make them strings -- after the example of Waldorf's Streichfett.
 *
 * **Divide-down.** A string ensemble of the seventies had no oscillator per key: a top-octave generator
 * made the twelve highest notes and flip-flops divided them down, so every C on the keyboard is locked
 * in phase to every other C. Here each pitch class has one phase counter at the top octave (C8 up),
 * kept modulo 2^8, and a note's phase is that counter divided by the power of two of its octave -- so
 * the octaves of a note are phase-locked, and a chord has the static, organ-like sound the ensemble then
 * animates.
 *
 * **Registers and registration** (25.09.2026, the user: "das Mischen der verschiedenen Stimmen sowie das
 * langsame Animieren der Mischung"). Every key sounds five divide-down registers: sawtooths at 16', 8' and 4'
 * and squares at 8' and 4' (the hollow, reedy half of those machines). A registration is a mix of them and a
 * tone: Violins, Violas, Cellos, Basses, Full, Hollow, Brass, Organ. `registration` morphs continuously through
 * the eight, as the registration knob of a Streichfett does, with the loudness kept; `animate` moves it with a
 * slow sine (`animateHz`) across up to three and a half registrations either way, so the mix of the voices
 * wanders by itself; `feet` tilts the footages (0: the low ones, 1: the high ones).
 *
 * **The ensemble.** What the ear knows as the sound of these machines is the chorus after them: bucket-brigade
 * delays of a few milliseconds, swept by slow and fast LFOs, mixed to two channels. Three types: Solina (three
 * lines around 6 ms, a 0.6 Hz and a 6 Hz sweep, a third of a cycle apart -- the widely described design of the
 * ARP Solina's ensemble), Chorus (two lines, a slower single sweep) and Wide (three longer, deeper lines, a
 * very slow sweep, spread wider). Each line loses its highs as a BBD does (a low pass at 7 kHz).
 *
 * **The phaser**, after the ensemble: four first-order all-passes per side swept between 200 Hz and 2.5 kHz by
 * a 0.3 Hz sine, the right side a quarter of a cycle behind, with feedback -- the swirl of a string machine
 * through a phase shifter.
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
    float attackS = 0.35f;       ///< crescendo
    float releaseS = 1.2f;       ///< sustain after the key
    float feet = 0.4f;           ///< balance of the footages: 0 the low ones .. 1 the high ones
    float toneHz = 5000.0f;      ///< low pass after the registers (times the registration's tone)
    float ensemble = 0.8f;       ///< 0..1 depth of the ensemble
    float registration = 0.0f;   ///< 0..7 through Violins, Violas, Cellos, Basses, Full, Hollow, Brass, Organ
    float animate = 0.3f;        ///< 0..1 how far the slow sine moves the registration
    float animateHz = 0.05f;     ///< its rate
    int ensembleType = 0;        ///< 0 Solina, 1 Chorus, 2 Wide
    float phaser = 0.0f;         ///< 0..1 amount of the phaser
};

/** @brief The string machine; stereo out (the ensemble makes the width). */
class StringMachine {
public:
    static constexpr int kKeys = 12;          ///< polyphony
    static constexpr int kRegisters = 5;      ///< saw 16', saw 8', saw 4', square 8', square 4'
    static constexpr int kRegistrations = 8;  ///< the registrations the knob morphs through
    /** @brief Sample rate; allocates the ensemble's lines. */
    void prepare(double sampleRate, uint64_t seed);
    void set(const StringSettings& s);                 ///< takes the settings (every 32-sample cell)
    void noteOn(int pitch, float velocity, int id);   ///< presses a key (the oldest is taken when all sound)
    void noteOff(int id);                             ///< releases a key
    void silence();                                   ///< every key off at once (a jump in the song)
    bool active() const;                              ///< whether anything sounds or rings in the ensemble
    /** @brief Renders @p n samples into @p L and @p R (overwritten). */
    void process(float* L, float* R, int n);
    /** @brief The name of registration @p i (0..7). */
    static const char* registrationName(int i);

private:
    struct Key {
        bool on = false, held = false;
        int pitch = 60, id = -1;
        float velocity = 0.8f, env = 0.0f;
        uint32_t order = 0;
        int pc = 0;                          ///< pitch class: the top-octave generator the key divides
        double inv16 = 1.0, inv8 = 1.0, inv4 = 1.0;   ///< 1 / the 16', 8' and 4' dividers (powers of two, so exact)
    };
    /** @brief The registration at @p position (0..7): register weights, tone factor and loudness. */
    void registrationAt(float position);
    double sr_ = 48000.0;
    StringSettings s_;
    Key keys_[kKeys];
    double counter_[12] = {};   ///< top-octave phase counters per pitch class, modulo 256
    uint32_t order_ = 0;
    Svf tone_;
    std::vector<float> line_;
    size_t mask_ = 0, write_ = 0;
    double slow_ = 0.0, fast_ = 0.0;   ///< the ensemble's LFO phases
    Svf bbd_[3];
    float ring_ = 0.0f;               ///< decaying peak of the output, for active()
    // The registration, renewed every 32 samples.
    float weight_[kRegisters] = { 0.0f, 1.0f, 0.4f, 0.0f, 0.0f };
    float regGain_ = 1.0f;
    double animPhase_ = 0.0;
    int64_t count_ = 0;
    // The phaser: four all-passes per side.
    float apX_[2][4] = {}, apY_[2][4] = {};
    float apCoef_[2] = { 0.0f, 0.0f }, phFb_[2] = { 0.0f, 0.0f };
    double phPhase_ = 0.0;
};

} // namespace eph
