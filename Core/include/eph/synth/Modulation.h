/**
 * @file Modulation.h
 * @brief A voice's own modulation (26.09.2026): a third envelope, four LFOs and an eight-slot modulation matrix, as
 *        every synth of the program carries them (the voices, the lead and the drone; the pad synth, the tape keys and
 *        the strings with fewer of them).
 *
 * **Sources.** The four LFOs (bipolar, -1..1), the modulation envelope and the synth's filter envelope (0..1), the
 * note's velocity (0..1) and the row's modulation lane (the note's step on it, -1..1). **Destinations.** Pitch, pulse
 * width, the saw-to-pulse blend, the place in a wavetable, cutoff, resonance, the filter's mode, its FM depth, the
 * level and the pan. A slot's amount is -1..1 of the destination's span: 12 semitones for the pitch (the amount
 * squared, so a vibrato of a few cents sits on the first tenth of the knob), 0.45 of the pulse width, the whole of the
 * blend, the table, the resonance, the mode and the FM, five octaves of cutoff, the level doubled or shut, the pan from
 * one side to the other; and VCO 2's pitch alone, two octaves (Vco.h: the sync sweep, the Prophet's Poly-Mod).
 *
 * **Determinism.** Everything runs on the synth's absolute sample count and the piece's beat, never on the host's
 * blocks: the envelope per sample, the LFOs evaluated at the synth's control steps. A free LFO's phase advances by the
 * samples between two evaluations; a synced one reads its phase off the beat (Clock.h, four beats a bar), so a
 * one-bar sweep opens on every downbeat of the piece unless it restarts with each note (retrig). The random shapes
 * draw from a stream of their own, so modulation never shifts the voice's drift.
 *
 * **Fade.** An LFO can come in over a few seconds after each note, as a player reaches for the wheel.
 * @author Rene Weller
 */
#pragma once
#include "eph/Dsp.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace eph {

constexpr int Lfos = 4;        ///< LFOs per voice
constexpr int ModSlots = 8;    ///< slots of the modulation matrix

/** @brief The LFOs' shapes (LfoShapeNames). */
enum class LfoShapeE : int { SINE, TRIANGLE, SAW_UP, SAW_DOWN, SQUARE, SAMPLE_HOLD, SMOOTH_RANDOM, COUNT };
/** @brief The matrix's sources (ModSourceNames). */
enum class ModSourceE : int { OFF, LFO1, LFO2, LFO3, LFO4, MOD_ENV, FILTER_ENV, VELOCITY, MOD_LANE, COUNT };
/** @brief The matrix's destinations (ModDestNames). */
enum class ModDestE : int { OFF, PITCH, PULSE_WIDTH, WAVE, TABLE_POS, CUTOFF, RESONANCE, FILTER_MODE, FILTER_FM, LEVEL, PAN, OSC2_PITCH,
                           COUNT };
constexpr int ModSources = static_cast<int>(ModSourceE::COUNT);   ///< sources including Off
constexpr int ModDests = static_cast<int>(ModDestE::COUNT);       ///< destinations including Off

/**
 * @brief Cycles per beat of an LFO's sync division (LfoSyncNames: free, 4, 2, 1 bars, 1/2 .. 1/16, 1/4T, 1/8T); 0:
 *        free.
 * @param[in] sync  the sync division
 * @return the cycles per beat, 0 for free
 */
inline double lfoCyclesPerBeat(int sync)
{
    static const double k[10] = { 0.0, 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0, 2.0, 4.0, 1.5, 3.0 };
    return k[std::clamp(sync, 0, 9)];
}

/** @brief One LFO's settings. Only 4-byte members: the engine compares settings bit for bit. */
struct LfoSettings {
    float m_rateHz = 1.0f;   ///< the free rate
    int m_shape = 0;         ///< LfoShape
    int m_sync = 0;          ///< tempo division (lfoCyclesPerBeat), 0 free
    int m_retrig = 0;        ///< 1: every note restarts it
    float m_fadeS = 0.0f;    ///< it comes in over this long after each note
};
/** @brief One slot of the matrix. */
struct ModSlot {
    int m_src = 0;           ///< ModSource
    int m_dst = 0;           ///< ModDest
    float m_amount = 0.0f;   ///< -1..1 of the destination's span
};
/** @brief A voice's modulation settings: the modulation envelope, the LFOs, the matrix. */
struct ModSettings {
    float m_attackMs = 10.0f;   ///< the modulation envelope's attack, ms
    float m_decayMs = 500.0f;   ///< its decay, ms
    float m_sustain = 0.0f;   ///< its sustain, 0..1
    float m_releaseMs = 300.0f;   ///< its release, ms
    LfoSettings m_lfo[Lfos];   ///< the four LFOs
    ModSlot m_slot[ModSlots];   ///< the matrix
};

/**
 * @brief The smaller matrix's destinations (the tape keys', the strings': ShortModDestNames) as ModDest.
 * @param[in] i  the destination in the smaller matrix
 * @return the destination as ModDest
 */
inline int shortModDest(int i)
{
    static const int k[5] = { 0, static_cast<int>(ModDestE::PITCH), static_cast<int>(ModDestE::CUTOFF), static_cast<int>(ModDestE::LEVEL),
                              static_cast<int>(ModDestE::PAN) };
    return k[std::clamp(i, 0, 4)];
}

/**
 * @brief The span of each destination for an amount of 1 (see the file comment); the pitch's amount is squared.
 * @param[in] d  the destination
 * @return its span for an amount of 1
 */
inline float modDestSpan(ModDestE d)
{
    switch (d) {
    case ModDestE::PITCH: return 12.0f;
    case ModDestE::OSC2_PITCH: return 24.0f;
    case ModDestE::PULSE_WIDTH: return 0.45f;
    case ModDestE::CUTOFF: return 5.0f;
    case ModDestE::OFF: return 0.0f;
    default: return 1.0f;
    }
}

/**
 * @brief One voice's modulation: the envelope, the LFOs, the matrix (see the file comment).
 *
 * Use: set() when the settings change; noteOn() / noteOff() with the voice's notes; tick() every sample (the
 * envelope); evaluate() at the voice's control steps, which returns the sums per destination.
 */
class Modulator {
public:
    /**
     * @brief The sample rate and the seed of the random shapes' stream.
     * @param[in] sampleRate  the sample rate in Hz
     * @param[in] seed        the seed of the random shapes
     */
    void prepare(double sampleRate, uint64_t seed)
    {
        m_sr = sampleRate > 0.0 ? sampleRate : 48000.0;
        m_env.setSampleRate(m_sr);
        m_env.kill();
        for (int l = 0; l < Lfos; ++l) {
            m_lfo[l] = LfoState{};
            m_lfo[l].m_rng.seed(mixSeed(seed, 0x4C464F00ull + static_cast<uint64_t>(l)));
        }
        m_lastAt = 0;
        m_noteAt = std::numeric_limits<int64_t>::min() / 2;
        set(m_);
    }
    /**
     * @brief Takes new settings; the LFOs run on.
     * @param[in] m  the settings
     */
    void set(const ModSettings& m)
    {
        m_ = m;
        m_env.setTimes(m.m_attackMs * 0.001f, m.m_decayMs * 0.001f, std::clamp(m.m_sustain, 0.0f, 1.0f), m.m_releaseMs * 0.001f);
        m_lfoUsed = 0u;
        m_dstMask = 0u;
        m_envUsed = false;
        m_live = 0;
        for (const ModSlot& s : m.m_slot) {
            const int src = std::clamp(s.m_src, 0, ModSources - 1), dst = std::clamp(s.m_dst, 0, ModDests - 1);
            if (src == 0 || dst == 0 || s.m_amount == 0.0f) continue;
            const ModDestE d = static_cast<ModDestE>(dst);
            const float a = std::clamp(s.m_amount, -1.0f, 1.0f);
            Live& l = m_liveSlots[m_live++];
            l.m_src = src;
            l.m_dst = dst;
            l.m_amount = (d == ModDestE::PITCH ? a * std::fabs(a) : a) * modDestSpan(d);
            m_dstMask |= 1u << dst;
            if (src >= static_cast<int>(ModSourceE::LFO1) && src <= static_cast<int>(ModSourceE::LFO4))
                m_lfoUsed |= 1u << (src - static_cast<int>(ModSourceE::LFO1));
            m_envUsed = m_envUsed || src == static_cast<int>(ModSourceE::MOD_ENV);
        }
        for (int l = 0; l < Lfos; ++l) {
            m_lfo[l].m_inc = std::clamp(static_cast<double>(m.m_lfo[l].m_rateHz), 0.0, 100.0) / m_sr;
            m_lfo[l].m_cpb = lfoCyclesPerBeat(m.m_lfo[l].m_sync);
        }
    }
    /**
     * @brief Whether any slot is live.
     * @return true when any slot is live
     */
    bool active() const { return m_live > 0; }
    /**
     * @brief Whether a live slot reaches @p d.
     * @param[in] d  the destination
     * @return true when a live slot reaches it
     */
    bool targets(ModDestE d) const { return ((m_dstMask >> static_cast<int>(d)) & 1u) != 0u; }
    /**
     * @brief A new note at sample @p at, beat @p beat: the envelope's attack, the retriggered LFOs from their start,
     *        the fades from 0.
     * @param[in] at    the note's sample
     * @param[in] beat  its beat
     */
    void noteOn(int64_t at, double beat)
    {
        m_env.noteOn();
        m_noteAt = at;
        advance(at);
        for (int l = 0; l < Lfos; ++l) {
            if (m_.m_lfo[l].m_retrig == 0) continue;
            LfoState& s = m_lfo[l];
            // The next whole cycle begins here (a new cycle also draws a new random value).
            if (s.m_cpb > 0.0) s.m_offset = beat * s.m_cpb - (std::floor(beat * s.m_cpb - s.m_offset) + 1.0);
            else s.m_cyc = std::floor(s.m_cyc) + 1.0;
        }
    }
    /** @brief The note's end: the envelope's release. */
    void noteOff() { m_env.noteOff(); }
    /** @brief Silences the envelope. */
    void kill() { m_env.kill(); }
    /** @brief One sample of the envelope (only when a slot reads it). */
    void tick() { if (m_envUsed) m_env.process(); }
    /**
     * @brief The sums per destination at sample @p at, beat @p beat.
     * @param[in]     at    the sample
     * @param[in]     beat  the beat
     * @param[in]     ext   the voice's own sources, indexed by ModSource (FilterEnv, Velocity, ModLane are read)
     * @param[in,out] out   ModDests sums, overwritten
     */
    void evaluate(int64_t at, double beat, const float* ext, float* out)
    {
        float src[ModSources] = {};
        for (int i = 0; i < ModSources; ++i) src[i] = ext[i];
        src[0] = 0.0f;
        src[static_cast<int>(ModSourceE::MOD_ENV)] = m_env.level();
        advance(at);
        for (int l = 0; l < Lfos; ++l)
            if (((m_lfoUsed >> l) & 1u) != 0u) src[static_cast<int>(ModSourceE::LFO1) + l] = value(l, at, beat);
        for (int d = 0; d < ModDests; ++d) out[d] = 0.0f;
        for (int i = 0; i < m_live; ++i) out[m_liveSlots[i].m_dst] += src[m_liveSlots[i].m_src] * m_liveSlots[i].m_amount;
    }
    /**
     * @brief The envelope's level (for tests).
     * @return the envelope's level
     */
    float envLevel() const { return m_env.level(); }
    /**
     * @brief @p sum of destination @p d where a slot reaches it, else nothing (for the synths' per-destination reads).
     * @param[in] sums  the sums per destination
     * @param[in] d     the destination
     * @return its sum where a slot reaches it, else 0
     */
    float offset(const float* sums, ModDestE d) const { return active() && targets(d) ? sums[static_cast<int>(d)] : 0.0f; }

private:
    /** @brief One LFO's running state. */
    struct LfoState {
        double m_cyc = 0.0;       ///< a free LFO's cycles
        double m_offset = 0.0;    ///< a synced one's shift against the beat (retrig)
        double m_inc = 0.0;       ///< cycles per sample, free
        double m_cpb = 0.0;       ///< cycles per beat, synced (0: free)
        int64_t m_cycle = std::numeric_limits<int64_t>::min();   ///< the whole cycle last read (the random shapes draw on a new one)
        float m_from = 0.0f;   ///< the random shapes' last value
        float m_to = 0.0f;   ///< ... and the next
        Rng m_rng;   ///< the random shapes' stream
    };
    /** @brief A slot that reaches something, with its amount already scaled by the span. */
    struct Live {
        int m_src = 0;           ///< its source (ModSource)
        int m_dst = 0;           ///< its destination (ModDest)
        float m_amount = 0.0f;   ///< its amount, scaled by the destination's span
    };

    /**
     * @brief The free LFOs run on to sample @p at (read or not).
     * @param[in] at  the sample to run on to
     */
    void advance(int64_t at)
    {
        const double n = static_cast<double>(at - m_lastAt);
        for (LfoState& s : m_lfo) if (s.m_cpb <= 0.0) s.m_cyc += n * s.m_inc;
        m_lastAt = at;
    }
    /**
     * @brief LFO @p l's value at sample @p at, beat @p beat.
     * @param[in] l     the LFO
     * @param[in] at    the sample
     * @param[in] beat  the beat
     * @return its value
     */
    float value(int l, int64_t at, double beat)
    {
        LfoState& s = m_lfo[l];
        const double c = s.m_cpb > 0.0 ? beat * s.m_cpb - s.m_offset : s.m_cyc;
        const double whole = std::floor(c);
        const float p = static_cast<float>(c - whole);
        const int64_t w = static_cast<int64_t>(whole);
        if (w != s.m_cycle) {
            s.m_cycle = w;
            s.m_from = s.m_to;
            s.m_to = s.m_rng.bipolar();
        }
        float v = 0.0f;
        switch (static_cast<LfoShapeE>(std::clamp(m_.m_lfo[l].m_shape, 0, static_cast<int>(LfoShapeE::COUNT) - 1))) {
        case LfoShapeE::SINE: v = sin01(p); break;
        case LfoShapeE::TRIANGLE: v = p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f); break;
        case LfoShapeE::SAW_UP: v = 2.0f * p - 1.0f; break;
        case LfoShapeE::SAW_DOWN: v = 1.0f - 2.0f * p; break;
        case LfoShapeE::SQUARE: v = p < 0.5f ? 1.0f : -1.0f; break;
        case LfoShapeE::SAMPLE_HOLD: v = s.m_to; break;
        case LfoShapeE::SMOOTH_RANDOM: v = s.m_from + (s.m_to - s.m_from) * p * p * (3.0f - 2.0f * p); break;
        default: break;
        }
        const float fade = m_.m_lfo[l].m_fadeS;
        if (fade > 0.0f) {
            const double t = static_cast<double>(at - m_noteAt) / (static_cast<double>(fade) * m_sr);
            if (t < 1.0) v *= static_cast<float>(std::max(0.0, t));
        }
        return v;
    }

    double m_sr = 48000.0;   ///< sample rate
    ModSettings m_;   ///< the settings
    Envelope m_env;   ///< the modulation envelope
    LfoState m_lfo[Lfos];   ///< the LFOs
    Live m_liveSlots[ModSlots];   ///< the slots that reach something
    int m_live = 0;   ///< how many
    unsigned m_lfoUsed = 0u;   ///< which LFOs a slot reads (bit l)
    unsigned m_dstMask = 0u;   ///< which destinations a slot reaches (bit d)
    bool m_envUsed = false;   ///< a slot reads the envelope
    int64_t m_lastAt = 0;   ///< the sample the free LFOs ran to
    int64_t m_noteAt = 0;   ///< the last note's sample (the fades)
};

} // namespace eph
