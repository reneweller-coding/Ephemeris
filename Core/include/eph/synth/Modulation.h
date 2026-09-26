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
 * one side to the other.
 *
 * **Determinism.** Everything runs on the synth's absolute sample count and the piece's beat, never on the host's
 * blocks: the envelope per sample, the LFOs evaluated at the synth's control steps. A free LFO's phase advances by the
 * samples between two evaluations; a synced one reads its phase off the beat (Clock.h, four beats a bar), so a
 * one-bar sweep opens on every downbeat of the piece unless it restarts with each note (retrig). The random shapes
 * draw from a stream of their own, so modulation never shifts the voice's drift.
 *
 * **Fade.** An LFO can come in over a few seconds after each note, as a player reaches for the wheel.
 */
#pragma once
#include "eph/Dsp.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace eph {

constexpr int kLfos = 4;        ///< LFOs per voice
constexpr int kModSlots = 8;    ///< slots of the modulation matrix

/** @brief The LFOs' shapes (kLfoShapeNames). */
enum class LfoShape : int { Sine, Triangle, SawUp, SawDown, Square, SampleHold, SmoothRandom, Count };
/** @brief The matrix's sources (kModSourceNames). */
enum class ModSource : int { Off, Lfo1, Lfo2, Lfo3, Lfo4, ModEnv, FilterEnv, Velocity, ModLane, Count };
/** @brief The matrix's destinations (kModDestNames). */
enum class ModDest : int { Off, Pitch, PulseWidth, Wave, TablePos, Cutoff, Resonance, FilterMode, FilterFm, Level, Pan, Count };
constexpr int kModSources = static_cast<int>(ModSource::Count);   ///< sources including Off
constexpr int kModDests = static_cast<int>(ModDest::Count);       ///< destinations including Off

/** @brief Cycles per beat of an LFO's sync division (kLfoSyncNames: free, 4, 2, 1 bars, 1/2 .. 1/16, 1/4T, 1/8T); 0: free. */
inline double lfoCyclesPerBeat(int sync)
{
    static const double k[10] = { 0.0, 1.0 / 16.0, 1.0 / 8.0, 1.0 / 4.0, 1.0 / 2.0, 1.0, 2.0, 4.0, 1.5, 3.0 };
    return k[std::clamp(sync, 0, 9)];
}

/** @brief One LFO's settings. Only 4-byte members: the engine compares settings bit for bit. */
struct LfoSettings {
    float rateHz = 1.0f;   ///< the free rate
    int shape = 0;         ///< LfoShape
    int sync = 0;          ///< tempo division (lfoCyclesPerBeat), 0 free
    int retrig = 0;        ///< 1: every note restarts it
    float fadeS = 0.0f;    ///< it comes in over this long after each note
};
/** @brief One slot of the matrix. */
struct ModSlot {
    int src = 0;           ///< ModSource
    int dst = 0;           ///< ModDest
    float amount = 0.0f;   ///< -1..1 of the destination's span
};
/** @brief A voice's modulation settings: the modulation envelope, the LFOs, the matrix. */
struct ModSettings {
    float attackMs = 10.0f, decayMs = 500.0f, sustain = 0.0f, releaseMs = 300.0f;   ///< the modulation envelope
    LfoSettings lfo[kLfos];
    ModSlot slot[kModSlots];
};

/** @brief The smaller matrix's destinations (the tape keys', the strings': kShortModDestNames) as ModDest. */
inline int shortModDest(int i)
{
    static const int k[5] = { 0, static_cast<int>(ModDest::Pitch), static_cast<int>(ModDest::Cutoff), static_cast<int>(ModDest::Level),
                              static_cast<int>(ModDest::Pan) };
    return k[std::clamp(i, 0, 4)];
}

/** @brief The span of each destination for an amount of 1 (see the file comment); the pitch's amount is squared. */
inline float modDestSpan(ModDest d)
{
    switch (d) {
    case ModDest::Pitch: return 12.0f;
    case ModDest::PulseWidth: return 0.45f;
    case ModDest::Cutoff: return 5.0f;
    case ModDest::Off: return 0.0f;
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
    /** @brief The sample rate and the seed of the random shapes' stream. */
    void prepare(double sampleRate, uint64_t seed)
    {
        sr_ = sampleRate > 0.0 ? sampleRate : 48000.0;
        env_.setSampleRate(sr_);
        env_.kill();
        for (int l = 0; l < kLfos; ++l) {
            lfo_[l] = LfoState{};
            lfo_[l].rng.seed(mixSeed(seed, 0x4C464F00ull + static_cast<uint64_t>(l)));
        }
        lastAt_ = 0;
        noteAt_ = std::numeric_limits<int64_t>::min() / 2;
        set(m_);
    }
    /** @brief Takes new settings; the LFOs run on. */
    void set(const ModSettings& m)
    {
        m_ = m;
        env_.setTimes(m.attackMs * 0.001f, m.decayMs * 0.001f, std::clamp(m.sustain, 0.0f, 1.0f), m.releaseMs * 0.001f);
        lfoUsed_ = 0u;
        dstMask_ = 0u;
        envUsed_ = false;
        live_ = 0;
        for (const ModSlot& s : m.slot) {
            const int src = std::clamp(s.src, 0, kModSources - 1), dst = std::clamp(s.dst, 0, kModDests - 1);
            if (src == 0 || dst == 0 || s.amount == 0.0f) continue;
            const ModDest d = static_cast<ModDest>(dst);
            const float a = std::clamp(s.amount, -1.0f, 1.0f);
            Live& l = liveSlots_[live_++];
            l.src = src;
            l.dst = dst;
            l.amount = (d == ModDest::Pitch ? a * std::fabs(a) : a) * modDestSpan(d);
            dstMask_ |= 1u << dst;
            if (src >= static_cast<int>(ModSource::Lfo1) && src <= static_cast<int>(ModSource::Lfo4))
                lfoUsed_ |= 1u << (src - static_cast<int>(ModSource::Lfo1));
            envUsed_ = envUsed_ || src == static_cast<int>(ModSource::ModEnv);
        }
        for (int l = 0; l < kLfos; ++l) {
            lfo_[l].inc = std::clamp(static_cast<double>(m.lfo[l].rateHz), 0.0, 100.0) / sr_;
            lfo_[l].cpb = lfoCyclesPerBeat(m.lfo[l].sync);
        }
    }
    /** @brief Whether any slot is live. */
    bool active() const { return live_ > 0; }
    /** @brief Whether a live slot reaches @p d. */
    bool targets(ModDest d) const { return ((dstMask_ >> static_cast<int>(d)) & 1u) != 0u; }
    /** @brief A new note at sample @p at, beat @p beat: the envelope's attack, the retriggered LFOs from their start, the fades from 0. */
    void noteOn(int64_t at, double beat)
    {
        env_.noteOn();
        noteAt_ = at;
        advance(at);
        for (int l = 0; l < kLfos; ++l) {
            if (m_.lfo[l].retrig == 0) continue;
            LfoState& s = lfo_[l];
            // The next whole cycle begins here (a new cycle also draws a new random value).
            if (s.cpb > 0.0) s.offset = beat * s.cpb - (std::floor(beat * s.cpb - s.offset) + 1.0);
            else s.cyc = std::floor(s.cyc) + 1.0;
        }
    }
    /** @brief The note's end: the envelope's release. */
    void noteOff() { env_.noteOff(); }
    /** @brief Silences the envelope. */
    void kill() { env_.kill(); }
    /** @brief One sample of the envelope (only when a slot reads it). */
    void tick() { if (envUsed_) env_.process(); }
    /**
     * @brief The sums per destination at sample @p at, beat @p beat.
     * @param ext the voice's own sources, indexed by ModSource (FilterEnv, Velocity, ModLane are read)
     * @param out kModDests sums, overwritten
     */
    void evaluate(int64_t at, double beat, const float* ext, float* out)
    {
        float src[kModSources] = {};
        for (int i = 0; i < kModSources; ++i) src[i] = ext[i];
        src[0] = 0.0f;
        src[static_cast<int>(ModSource::ModEnv)] = env_.level();
        advance(at);
        for (int l = 0; l < kLfos; ++l)
            if (((lfoUsed_ >> l) & 1u) != 0u) src[static_cast<int>(ModSource::Lfo1) + l] = value(l, at, beat);
        for (int d = 0; d < kModDests; ++d) out[d] = 0.0f;
        for (int i = 0; i < live_; ++i) out[liveSlots_[i].dst] += src[liveSlots_[i].src] * liveSlots_[i].amount;
    }
    /** @brief The envelope's level (for tests). */
    float envLevel() const { return env_.level(); }
    /** @brief @p sum of destination @p d where a slot reaches it, else nothing (for the synths' per-destination reads). */
    float offset(const float* sums, ModDest d) const { return active() && targets(d) ? sums[static_cast<int>(d)] : 0.0f; }

private:
    struct LfoState {
        double cyc = 0.0;       ///< a free LFO's cycles
        double offset = 0.0;    ///< a synced one's shift against the beat (retrig)
        double inc = 0.0;       ///< cycles per sample, free
        double cpb = 0.0;       ///< cycles per beat, synced (0: free)
        int64_t cycle = std::numeric_limits<int64_t>::min();   ///< the whole cycle last read (the random shapes draw on a new one)
        float from = 0.0f, to = 0.0f;   ///< the random shapes' last two values
        Rng rng;
    };
    struct Live { int src = 0, dst = 0; float amount = 0.0f; };

    /** @brief The free LFOs run on to sample @p at (read or not). */
    void advance(int64_t at)
    {
        const double n = static_cast<double>(at - lastAt_);
        for (LfoState& s : lfo_) if (s.cpb <= 0.0) s.cyc += n * s.inc;
        lastAt_ = at;
    }
    float value(int l, int64_t at, double beat)
    {
        LfoState& s = lfo_[l];
        const double c = s.cpb > 0.0 ? beat * s.cpb - s.offset : s.cyc;
        const double whole = std::floor(c);
        const float p = static_cast<float>(c - whole);
        const int64_t w = static_cast<int64_t>(whole);
        if (w != s.cycle) {
            s.cycle = w;
            s.from = s.to;
            s.to = s.rng.bipolar();
        }
        float v = 0.0f;
        switch (static_cast<LfoShape>(std::clamp(m_.lfo[l].shape, 0, static_cast<int>(LfoShape::Count) - 1))) {
        case LfoShape::Sine: v = sin01(p); break;
        case LfoShape::Triangle: v = p < 0.25f ? 4.0f * p : (p < 0.75f ? 2.0f - 4.0f * p : 4.0f * p - 4.0f); break;
        case LfoShape::SawUp: v = 2.0f * p - 1.0f; break;
        case LfoShape::SawDown: v = 1.0f - 2.0f * p; break;
        case LfoShape::Square: v = p < 0.5f ? 1.0f : -1.0f; break;
        case LfoShape::SampleHold: v = s.to; break;
        case LfoShape::SmoothRandom: v = s.from + (s.to - s.from) * p * p * (3.0f - 2.0f * p); break;
        default: break;
        }
        const float fade = m_.lfo[l].fadeS;
        if (fade > 0.0f) {
            const double t = static_cast<double>(at - noteAt_) / (static_cast<double>(fade) * sr_);
            if (t < 1.0) v *= static_cast<float>(std::max(0.0, t));
        }
        return v;
    }

    double sr_ = 48000.0;
    ModSettings m_;
    Envelope env_;
    LfoState lfo_[kLfos];
    Live liveSlots_[kModSlots];
    int live_ = 0;
    unsigned lfoUsed_ = 0u, dstMask_ = 0u;
    bool envUsed_ = false;
    int64_t lastAt_ = 0;
    int64_t noteAt_ = 0;
};

} // namespace eph
