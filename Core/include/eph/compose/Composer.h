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
 * **Curation** (SetFile.h): every stream can be drawn again on its own; the rest stays bit for bit.
 *
 * The drums come in from the second build to the end of the peak in the styles that have them (Drums.h).
 */
#pragma once
#include "eph/Params.h"
#include "eph/Score.h"
#include "eph/SetFile.h"
#include <cstdint>

namespace eph {

struct StyleProfile;

/**
 * @brief Writes one piece.
 * @param p        parameters (compose.style, key, scale, bpm or the profile's tempo, the voices)
 * @param seed     the piece's seed
 * @param minutes  its length
 * @param keyShift semitones from compose.key (a concert moves from piece to piece)
 * @param curation rerolls, or null
 * @param unit     prefix of this piece's units in @p curation ("" alone, "piece2." in a concert)
 * @param profile  a profile instead of compose.style's (a concert's morph, morphProfile), or null
 */
Score composePiece(const ParamStore& p, uint64_t seed, double minutes, int keyShift = 0,
                   const Curation* curation = nullptr, const std::string& unit = std::string(),
                   const StyleProfile* profile = nullptr);

/**
 * @brief Writes a concert of pieces, @p minutes long in all. With compose.morph_to it wanders from compose.style
 *        to that style: each piece is composed with the profile at the share of the concert already played
 *        (morphProfile), and takes the nearer style's drum patterns. With compose.concert_arc the pieces follow an
 *        arc of tension over the whole concert (concertArc, arcProfile): quiet at the ends, densest at 60 %.
 *        With compose.album the concert is an album (the style guide's 6.5): an interlude of three to five minutes
 *        between the long pieces, the piece in the middle the darkest (Phrygian, darker voices), the last one
 *        ethereal (Dorian, a longer hall, a row fewer at the peak, no drums, a quieter lead). Without it the
 *        concert draws exactly what it drew before.
 */
Score composeConcert(const ParamStore& p, uint64_t seed, double minutes, const Curation* curation = nullptr);

/**
 * @brief Writes an interlude (the style guide's 6.5, the album's short ambient pieces between the long ones): no
 *        rows, only the drone, slow chords on the string machine or the tape keys (a chord every 16 to 32 bars),
 *        the atmosphere with its granular cloud, a longer hall, and the open fifth at the end.
 * @param p        parameters (key, scale, the voices)
 * @param seed     the interlude's seed
 * @param minutes  its length
 * @param keyShift semitones from compose.key
 * @param profile  the profile of the moment in the concert
 */
Score composeInterlude(const ParamStore& p, uint64_t seed, double minutes, int keyShift, const StyleProfile& profile);

/** @brief The names of a piece's units, in stream order: form, tempo, rows, rack, layers, lead, pads, hands. */
extern const char* const kUnitNames[8];

/** @brief Appends @p src to @p dst at @p dst's end; its roots are moved by @p rootOffset semitones. */
void appendScore(Score& dst, const Score& src, int rootOffset);

} // namespace eph
