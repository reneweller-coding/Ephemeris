/**
 * @file Harmony.h
 * @brief The harmony of a piece: which roots the transposer moves between (PLAN 2.5, 6.5).
 *
 * In this music the harmony is a transposition of the whole sequence -- by the player's hand on the
 * sequencer's keyboard, or by a slow row that transposes the fast ones (the epicycle of PLAN 5.1). Over
 * a pedal on the tonic, the roots move between a handful of degrees, and which ones and how often is
 * what separates the styles:
 *
 * | Style   | Moves (semitones from the tonic)             | Stays on the tonic |
 * |---------|----------------------------------------------|--------------------|
 * | Cosmic  | bVI -4, bVII -2, bIII +3, iv +5              | often              |
 * | Doom    | bII +1, bVI -4, tritone +6, vii -1           | often              |
 * | Melodic | bVI -4, bVII -2, bIII +3, iv +5, v +7        | less               |
 * | Modern  | bVI -4, bVII -2, bIII +3, iv +5, ii +2       | less               |
 * | Drift   | bVII -2, iv +5                               | mostly             |
 *
 * The weights are a first setting from the literature and the listening of the references (PLAN 2.5),
 * to be calibrated against chroma measurements of the reference recordings in Phase 4.
 *
 * A progression is drawn as a first-order Markov chain over these roots that never repeats a move
 * immediately (a root goes back to the tonic or on to another root), starts on the tonic and ends on
 * it -- the pedal the piece comes home to.
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/Params.h"
#include <vector>

namespace eph {

/**
 * @brief Draws the roots of a transposer row.
 * @param style  the style profile
 * @param steps  how many steps (the transposer row's length)
 * @param rng    the row's stream
 * @return semitone offsets from the tonic, first and last 0
 */
std::vector<int> drawProgression(Style style, int steps, Rng& rng);

/** @brief The roots a style moves between, for tests and for mutating a transposer row. */
const std::vector<int>& progressionRoots(Style style);

} // namespace eph
