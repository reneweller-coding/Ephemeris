/**
 * @file Pads.h
 * @brief Held chords over the rows (PLAN 6.5): what the tape keys and, later, the string machine play.
 *
 * The chord of a moment stands on the chord track's degree (Harmony.h) of the scale on the transposer's
 * root, and is one of the style guide's vocabulary (3.4): open fifth, triad, sus2 and sus4, add9, stacked
 * fourths, m9, the seventh -- or, for the tape keys' choirs, a plain triad mostly. It is voiced inside a
 * register with the least movement from the chord before -- each voice goes to the nearest octave of its
 * new tone, so common tones stay (3.9) -- the rule a player's hand follows on a keyboard without thinking.
 *
 * A tape keyboard cannot hold a chord longer than its tapes: the writer re-strikes before the tape
 * ends (`restrikeSeconds`, less than TapeSeconds) and at every change of root, and staggers the keys of
 * a chord by a few milliseconds, as ten fingers do.
 * @author Rene Weller
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/Score.h"
#include <utility>
#include <vector>

namespace eph
{

/** @brief Where and how chords are written. */
struct PadPlan
{
    PartE  m_part = PartE::TAPE_KEYS;   ///< which part plays them
    int    m_keyRoot = 9;               ///< pitch class of the key
    int    m_scale = 0;                 ///< compose.scale order
    int    m_low = 50;                  ///< lowest note of the register (MIDI)
    int    m_high = 74;                 ///< highest note
    double m_restrikeSeconds = 7.0;     ///< longest a chord is held before it is struck again
    bool   m_choir = true;              ///< a choir's plain triads (the tape keys) rather than the whole vocabulary
    bool   m_openFifth = false;         ///< only the open fifth on the tonic: how a piece ends (3.3)
    std::vector<std::pair<double, int>> m_shifts;   ///< the transposer's roots over time
    std::vector<std::pair<double, int>> m_chords;   ///< the chord track's degrees over time (empty: the tonic)
    std::vector<std::pair<double, int>> m_scales;   ///< the scale over time (Score::scaleShifts; empty: @p scale)
};

/**
 * @brief The drone: the root in the second octave (E2 .. E3), held through each of @p roots (null: the score's,
 *        Score::rootShifts) between @p from and @p to. The composer hands in the keys alone, so the drone stays
 *        on the centre while the transposer moves the sequence (the style guide's 3.3 and 3.7).
 * @param[in,out] score    the score written to
 * @param[in]     keyRoot  the key's root note
 * @param[in]     from     the first beat
 * @param[in]     to       the end in beats
 * @param[in]     roots    the (beat, semitones) root changes, or null for the score's
 */
void writeDrone( Score &score, int keyRoot, double from, double to,
                 const std::vector<std::pair<double, int>> *roots = nullptr );

/**
 * @brief Writes chords between two beats; returns the number of chords struck.
 * @param[in,out] score  the score written to
 * @param[in]     plan   the pads' plan
 * @param[in]     from   the first beat
 * @param[in]     to     the end in beats
 * @param[in,out] rng    the random stream
 * @return the number of chords struck
 */
int writeChords( Score &score, const PadPlan &plan, double from, double to, Rng &rng );

}   // namespace eph
