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
 * @author Rene Weller
 */
#pragma once
#include "eph/Dsp.h"
#include <cstdint>

namespace eph {

/** @brief Settings for the following samples. */
struct DrumSettings {
    float m_kickHz = 50.0f;      ///< where the kick's pitch ends
    float m_decay = 1.0f;        ///< scales every decay
    float m_tone = 0.5f;         ///< 0 dark .. 1 bright (hats, snare noise)
};

/** @brief The kit; stereo out. */
class DrumKit {
public:
    /**
     * sample rate and the noise's seed
     * @param[in] sampleRate  the sample rate in Hz
     * @param[in] seed        the noise's seed
     */
    void prepare(double sampleRate, uint64_t seed);
    /**
     * call at every 32-sample cell
     * @param[in] s  the settings
     */
    void set(const DrumSettings& s);
    /**
     * @brief Strikes the instrument of General MIDI note @p note; toms take @p tomHz as their pitch.
     * @param[in] note      the General MIDI note
     * @param[in] velocity  the velocity, 0..1
     * @param[in] tomHz     the toms' pitch in Hz
     */
    void hit(int note, float velocity, float tomHz);
    /**
     * whether anything still rings
     * @return true while anything rings
     */
    bool active() const;
    /**
     * @brief Renders @p n samples into @p L and @p R (overwritten).
     * @param[out] L  receives the left channel (overwritten)
     * @param[out] R  receives the right channel (overwritten)
     * @param[in]  n  how many samples
     */
    void process(float* L, float* R, int n);

private:
    /** @brief The kit's instruments. */
    enum InstE { KICK, SNARE, CLOSED, OPEN, TOM_LOW, TOM_HIGH, RIM, SHAKER, INSTS };
    /** @brief One instrument sounding. */
    struct Voice {
        double m_age = 1e9;          ///< seconds since the hit
        float m_velocity = 0.0f;   ///< its velocity, 0..1
        double m_phase = 0.0;   ///< its tone's phase
        double m_phase2 = 0.0;   ///< its second tone's phase
        float m_hz = 100.0f;   ///< its pitch, Hz
        Svf m_bp;   ///< its noise's band pass
    };
    /**
     * @brief White noise, -1 .. 1.
     * @return a white noise sample, -1 .. 1
     */
    float noise() { return m_rng.bipolar(); }
    double m_sr = 48000.0;   ///< the sample rate, Hz
    DrumSettings m_s;   ///< the settings
    Voice m_v[INSTS];   ///< the instruments
    double m_metal[6] = {};         ///< the hats' six square phases
    Svf m_hatBp;   ///< the hats' band pass
    Svf m_hatHp;   ///< the hats' high pass
    Rng m_rng;   ///< the noise's stream
};

} // namespace eph
