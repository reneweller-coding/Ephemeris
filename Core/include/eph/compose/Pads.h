/**
 * @file Pads.h
 * @brief Held chords over the rows (PLAN 6.5): what the tape keys and, later, the string machine play.
 *
 * The chord of a moment is the triad on the transposer's root in the scale (root, third, fifth), with
 * now and then the seventh or the ninth. It is voiced inside a register with the least movement from
 * the chord before -- each voice goes to the nearest octave of its new tone -- the rule a player's hand
 * follows on a keyboard without thinking.
 *
 * A tape keyboard cannot hold a chord longer than its tapes: the writer re-strikes before the tape
 * ends (`restrikeSeconds`, less than kTapeSeconds) and at every change of root, and staggers the keys of
 * a chord by a few milliseconds, as ten fingers do.
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/Score.h"
#include <utility>
#include <vector>

namespace eph {

/** @brief Where and how chords are written. */
struct PadPlan {
    Part part = Part::TapeKeys;     ///< which part plays them
    int keyRoot = 9;                ///< pitch class of the key
    int scale = 0;                  ///< compose.scale order
    int low = 50;                   ///< lowest note of the register (MIDI)
    int high = 74;                  ///< highest note
    double restrikeSeconds = 7.0;   ///< longest a chord is held before it is struck again
    float colour = 0.3f;            ///< chance of a seventh or a ninth
    std::vector<std::pair<double, int>> shifts;   ///< the transposer's roots over time
};

/**
 * @brief The drone: the root in the second octave (E2 .. E3), held through each of the score's roots
 *        (Score::rootShifts) between @p from and @p to.
 */
void writeDrone(Score& score, int keyRoot, double from, double to);

/** @brief Writes chords between two beats; returns the number of chords struck. */
int writeChords(Score& score, const PadPlan& plan, double from, double to, Rng& rng);

} // namespace eph
