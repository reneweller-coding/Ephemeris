/**
 * @file Drums.h
 * @brief A small analogue drum kit for the styles that have drums (PLAN 5.7: "Melodic", "Modern").
 *
 * PLAN 4 meant to copy Phosphene's percussion kit. It is not copied: that kit is twelve lanes of five
 * engines tied to Phosphene's parameter tables, its harmony and a psytrance rhythm module, and this music
 * wants a handful of sounds late in a piece. This kit is eight instruments, one voice each, the classic
 * analogue recipes:
 * - **kick**: a sine whose pitch falls from about 170 Hz to `kickHz` in some tens of milliseconds, a
 *   quarter- to half-second decay, a click, a little drive;
 * - **snare**: two sine modes (180 and 330 Hz) and band-passed noise;
 * - **hats**: six square waves at the TR-808's metal frequencies, band-passed high, short (closed) or
 *   long (open; the closed chokes it);
 * - **toms**: sines falling onto a pitch the composer gives (the root or the fifth of the moment);
 * - **rim** and **shaker**: a click with a short tone, and band-passed noise with a soft attack.
 * Notes arrive as General MIDI numbers (36 kick, 38 snare, 42/46 hats, 45/48 toms, 37 rim, 70 shaker),
 * so the MIDI export and the render agree.
 */
#pragma once
#include "eph/Dsp.h"
#include <cstdint>

namespace eph {

/** @brief Settings for the following samples. */
struct DrumSettings {
    float kickHz = 50.0f;      ///< where the kick's pitch ends
    float decay = 1.0f;        ///< scales every decay
    float tone = 0.5f;         ///< 0 dark .. 1 bright (hats, snare noise)
};

/** @brief The kit; stereo out. */
class DrumKit {
public:
    void prepare(double sampleRate, uint64_t seed);   ///< sample rate and the noise's seed
    void set(const DrumSettings& s);                  ///< call at every 32-sample cell
    /** @brief Strikes the instrument of General MIDI note @p note; toms take @p tomHz as their pitch. */
    void hit(int note, float velocity, float tomHz);
    bool active() const;                              ///< whether anything still rings
    /** @brief Renders @p n samples into @p L and @p R (overwritten). */
    void process(float* L, float* R, int n);

private:
    enum Inst { Kick, Snare, Closed, Open, TomLow, TomHigh, Rim, Shaker, kInsts };
    struct Voice {
        double age = 1e9;          ///< seconds since the hit
        float velocity = 0.0f;
        double phase = 0.0, phase2 = 0.0;
        float hz = 100.0f;
        Svf bp;
    };
    float noise() { return rng_.bipolar(); }
    double sr_ = 48000.0;
    DrumSettings s_;
    Voice v_[kInsts];
    double metal_[6] = {};         ///< the hats' six square phases
    Svf hatBp_, hatHp_;
    Rng rng_;
};

} // namespace eph
