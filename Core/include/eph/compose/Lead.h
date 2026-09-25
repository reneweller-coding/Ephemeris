/**
 * @file Lead.h
 * @brief Lead solos over the rows (PLAN 6.7).
 *
 * A lead in this music sings above the sequences: long notes with vibrato, a glide into a note from a
 * whole tone below, short runs between them, phrases of two or four bars with rests between, a motif
 * that comes back. Until the constraint Markov chains of Phosphene's `Melody` are ported (Phase 4),
 * the phrases are drawn by rules:
 * - **Pitch set**: the minor pentatonic of the current root, plus the scale's other tones as passing
 *   notes off the beat. The root follows the transposer (Rack::shifts), so the lead is always in the
 *   root the rows are in.
 * - **Contour**: an arc -- up in the first half of a phrase, down in the second -- in steps of the
 *   pitch set with the odd leap; the register is kept between `low` and `high`.
 * - **Strong beats** prefer chord tones (root, third, fifth); a phrase ends on one, held long.
 * - **Rhythm** from cells of half notes, dotted quarters, quarters and eighth runs, denser with more
 *   intensity.
 * - **Repetition**: with some chance a phrase repeats the rhythm and intervals of the one before, moved
 *   to the new root -- the motif a listener recognises.
 * Monophonic by construction: a note ends before the next begins, except the glide from below, which
 * overlaps so the voice slides instead of retriggering.
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/Score.h"
#include <utility>
#include <vector>

namespace eph {

/** @brief Where and how a lead plays. */
struct LeadPlan {
    int keyRoot = 9;                ///< pitch class of the key
    int scale = 0;                  ///< compose.scale order
    int low = 64;                   ///< lowest note of the register (MIDI)
    int high = 86;                  ///< highest note
    float intensity = 0.5f;         ///< 0..1: density of the phrases, fewer rests
    std::vector<std::pair<double, int>> shifts;   ///< the transposer's roots over time (Rack::shifts)
};

/**
 * @brief Writes lead phrases between two beats.
 * @param score the score (notes appended, part Lead)
 * @param plan  register, key, roots and intensity
 * @param from  first beat (rounded up to a bar)
 * @param to    last beat; no note reaches past it
 * @param rng   the lead's stream
 */
void writeLead(Score& score, const LeadPlan& plan, double from, double to, Rng& rng);

/** @brief Whether @p pitch belongs to the scale on root @p rootPc (for the tests). */
bool inScale(int pitch, int rootPc, int scale);

} // namespace eph
