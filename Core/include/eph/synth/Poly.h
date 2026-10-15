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
 * @author Rene Weller
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/synth/Wavetable.h"
#include "eph/synth/Filters.h"
#include "eph/synth/Modulation.h"
#include <cstdint>
#include <vector>

namespace eph
{

/** @brief Settings for the following samples. */
struct PolySettings
{
    int   m_table = 1;            ///< the wavetable (1: PWM, the analog pad)
    float m_position = 0.3f;      ///< 0..1 across the table's frames
    float m_scan = 0.35f;         ///< 0..1 how far the slow sine moves the position
    float m_scanHz = 0.06f;       ///< its rate
    float m_detuneCents = 8.0f;   ///< the two oscillators apart
    float m_spread = 0.6f;        ///< 0..1 the two oscillators apart in the stereo field
    float m_driftCents = 3.0f;    ///< the slow wandering of each oscillator's pitch
    float m_cutoffHz = 2200.0f;   ///< the low pass
    float m_resonance = 0.15f;    ///< 0..1
    float m_envOctaves = 1.0f;    ///< the envelope's push on the cutoff, octaves
    float m_attackS = 1.5f;       ///< swell
    float m_releaseS = 4.0f;      ///< fade after the key
    float m_chorus = 0.5f;        ///< 0..1 the ensemble's depth and mix
    int   m_filter = 3;           ///< the filter model (Filters.h; 3: the Oberheim SEM)
    float m_filterMode = 0.0f;    ///< its mode (the SEM's morph, the Xpander's response ...)
    // 26.09.2026, the envelopes in full and the modulation (Modulation.h); only 4-byte members (compared bit for bit).
    float       m_ampDecayS = 1.0f;      ///< the amplitude envelope's decay (its attack and release above)
    float       m_ampSustain = 1.0f;     ///< ... its sustain level
    int         m_filtLink = 1;          ///< 1: the filter follows the amplitude envelope; 0: its own below
    float       m_filtAttackS = 2.0f;    ///< the filter's own envelope: attack, s
    float       m_filtDecayS = 3.0f;     ///< ... decay, s
    float       m_filtSustain = 0.4f;    ///< ... sustain level
    float       m_filtReleaseS = 4.0f;   ///< ... release, s
    float       m_envVelocity = 0.0f;    ///< how far the velocity scales the filter envelope
    ModSettings m_mod;                   ///< every key's modulation envelope, LFOs and matrix
};

/** @brief The pad synth; stereo out. */
class PolySynth
{
public:
    static constexpr int Keys = 8;   ///< polyphony
    /**
     * sample rate; allocates the ensemble's line
     * @param[in] sampleRate  the sample rate in Hz
     * @param[in] seed        the seed
     */
    void prepare( double sampleRate, uint64_t seed );
    /**
     * takes the settings (every 32-sample cell)
     * @param[in] s  the settings
     */
    void set( const PolySettings &s );
    /**
     * presses a key
     * @param[in] pitch     the MIDI note
     * @param[in] velocity  the velocity, 0..1
     * @param[in] id        the note's id
     */
    void noteOn( int pitch, float velocity, int id );
    /**
     * releases a key
     * @param[in] id  the note's id
     */
    void noteOff( int id );
    void silence();   ///< every key off at once (a jump in the song)
    /**
     * whether anything sounds or rings in the ensemble
     * @return true while anything sounds or rings
     */
    bool active() const;
    /**
     * renders @p n samples into @p L and @p R (overwritten)
     * @param[out] L  receives the left channel (overwritten)
     * @param[out] R  receives the right channel (overwritten)
     * @param[in]  n  how many samples
     */
    void process( float *L, float *R, int n );
    /**
     * @brief The piece's beat at the next sample and beats per sample (the synced LFOs); call at every cell.
     * @param[in] beat            the beat at the next sample
     * @param[in] beatsPerSample  beats per sample from there
     */
    void setClock( double beat, double beatsPerSample )
    {
        m_beat0 = beat;
        m_bps = beatsPerSample;
        m_clockAt = m_count;
    }

private:
    /** @brief One key: its two oscillators, its envelopes, its filters, its modulation. */
    struct Key
    {
        bool       m_on = false;                  ///< it sounds
        bool       m_held = false;                ///< it is held
        int        m_pitch = 60;                  ///< MIDI note
        int        m_id = -1;                     ///< the note id it plays, -1 none
        float      m_velocity = 0.8f;             ///< its velocity, 0..1
        uint32_t   m_order = 0;                   ///< when it was pressed (the oldest is taken)
        Envelope   m_env;                         ///< the amplitude envelope
        double     m_phase[2] = { 0.0, 0.0 };     ///< the two oscillators' phases
        double     m_inc[2] = { 0.0, 0.0 };       ///< their phase steps per sample
        int        m_level[2] = { -1, -1 };       ///< the mip level each reads (-1: not yet chosen)
        float      m_drift[2] = { 0.0f, 0.0f };   ///< each oscillator's slow drift, cents
        float      m_scanOffset = 0.0f;           ///< the key's own place in the scan's cycle
        float      m_position = 0.3f;             ///< where in the table it reads (the scan's value of the moment)
        FilterLane m_filt[2];                     ///< left and right (Filters.h)
        float      m_g = 0.1f;                    ///< the filter's tan(pi fc / fs) of the moment
        Rng        m_rng;                         ///< its own drift, so the order of the keys does not matter
        Envelope   m_fenv;                        ///< the filter's own envelope (filtLink 0)
        Modulator  m_mod;                         ///< its modulation (Modulation.h)
        float      m_mo[ModDests] = {};           ///< the matrix's last sums
        float      m_fk = 0.0f;                   ///< the resonance as the matrix moves it
        float      m_mode = 0.0f;                 ///< the filter's mode as the matrix moves it
        float      m_lv = 1.0f;                   ///< the level as the matrix moves it
        float      m_panL = 1.0f;                 ///< the pan's gain, left
        float      m_panR = 1.0f;                 ///< ... right
    };
    /**
     * the oscillators' increments and levels from the pitch, detune, drift and the matrix's pitch
     * @param[in,out] k  the key
     */
    void retune( Key &k );
    /** @brief The keys' filters side by side (26.09.2026): key k's left channel in lane 2k, its right in lane 2k + 1, run
     *         in SIMD registers as the voice bank's are (the comb keeps the keys' FilterLanes, whose lines it needs). */
    static constexpr int Lanes = 2 * Keys;
    alignas( 32 ) float m_fv[4][Lanes] = {};   ///< the filters' node voltages, four per lane
    alignas( 32 ) float m_fs[4][Lanes] = {};   ///< the filters' states, four per lane
    /**
     * the key's two lanes from rest (a fresh key, a new model)
     * @param[in] key  the key
     */
    void clearLanes( int key );
    /**
     * setClock's beat
     * @param[in] at  the sample
     * @return the beat there
     */
    double beatAt( int64_t at ) const { return m_beat0 + static_cast<double>( at - m_clockAt ) * m_bps; }
    double             m_sr = 48000.0;        ///< sample rate
    PolySettings       m_s;                   ///< the settings
    const CycleTable  *m_table = nullptr;     ///< the wavetable it reads
    Key                m_keys[Keys];          ///< the keys
    Envelope           m_times;               ///< the settings' envelope times, copied to every key
    Envelope           m_ftimes;              ///< the filter envelope's
    double             m_beat0 = 0.0;         ///< the clock: the beat at m_clockAt
    double             m_bps = 0.0;           ///< beats per sample
    int64_t            m_clockAt = 0;         ///< the sample setClock() was called at
    uint32_t           m_order = 0;           ///< counts the keys pressed
    double             m_scanPhase = 0.0;     ///< the scan LFO's phase
    int64_t            m_count = 0;           ///< samples rendered
    Rng                m_rng;                 ///< the seed stream of the keys
    std::vector<float> m_line;                ///< the ensemble's delay line
    size_t             m_mask = 0;            ///< its size - 1
    size_t             m_write = 0;           ///< where the next sample goes
    double             m_chorusPhase = 0.0;   ///< the ensemble's LFO phase
    float              m_ring = 0.0f;         ///< decaying peak of the output, for active()
};

}   // namespace eph
