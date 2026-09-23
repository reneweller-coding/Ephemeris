/**
 * @file Composer.h
 * @brief The composer (PLAN 6): a whole piece, and a concert of pieces, from a seed and a style.
 *
 * **A piece** is written in this order, each step on its own stream derived from the seed, so that
 * changing one does not move the others (the ground for locking and rerolling, PLAN 6.10):
 * 1. the form (Form.h): sections, lengths, energy, the tempo and key of every sequence phase;
 * 2. the rack: a bass row, counter rows of the profile's lengths, a transposer row; every phase draws
 *    new patterns; rows come in one by one through the builds (the instrumentation matrix of PLAN 6.3),
 *    all play at the peak, the counters leave in the breakdown, everything stops in the bridge and the
 *    coda;
 * 3. what follows the rack's roots: the drone throughout, the tape keys' chords from the second build,
 *    the string machine at the peak and in the breakdown, the lead in its section and at the peak, all
 *    with the profile's chances;
 * 4. the atmosphere: wind and sweeps in the intro, the bridges and the coda, bleeps in the builds;
 * 5. the settings of the piece as steps at its start: tape set, hall, loudness;
 * 6. the hands on the knobs of what plays, following the form's energy.
 *
 * **A concert** is pieces one after another until the length is reached: each piece's length from the
 * profile, its key a fourth, a fifth, the relative or a tone away from the one before, each on its own
 * seed branch. The coda of one piece and the intro of the next make the bridge between them; there is
 * no beat matching to do (PLAN 6.9).
 *
 * Not yet: the drums (the profile's chance is drawn but nothing plays), the composer thread and ring,
 * locking and rerolling beyond what the seed branches allow.
 */
#pragma once
#include "eph/Params.h"
#include "eph/Score.h"
#include <cstdint>

namespace eph {

/**
 * @brief Writes one piece.
 * @param p        parameters (compose.style, key, scale, bpm or the profile's tempo, the voices)
 * @param seed     the piece's seed
 * @param minutes  its length
 * @param keyShift semitones from compose.key (a concert moves from piece to piece)
 */
Score composePiece(const ParamStore& p, uint64_t seed, double minutes, int keyShift = 0);

/** @brief Writes a concert of pieces, @p minutes long in all. */
Score composeConcert(const ParamStore& p, uint64_t seed, double minutes);

/** @brief Appends @p src to @p dst at @p dst's end; its roots are moved by @p rootOffset semitones. */
void appendScore(Score& dst, const Score& src, int rootOffset);

} // namespace eph
