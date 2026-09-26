/**
 * @file Leveler.h
 * @brief Every piece as loud as its style means (26.09.2026): the loudest part of each piece measured, and a correction
 *        of the master gain that brings it to its style's target.
 *
 * Pieces of one style came out several dB apart (a Modern piece at -13.1 LUFS, the next at -17.4): what plays -- how
 * many rows, which presets, whether the drums come in -- is drawn anew each time. The composer marks where each piece
 * is at its loudest (its peak; else its section of the most energy) and what that part should measure
 * (StyleProfile::peakLufs); levelScore renders that part -- a few seconds before it for the rooms to fill and the notes
 * that sound on across the jump to be found again, then the part itself -- measures it (BS.1770, gated), and sets the
 * correction (LevelMark::trimDb, at most 4 dB either way). The master bus compresses and limits after the gain, so the
 * part is measured a second time with the correction and the rest put right. The player's master level is left out of
 * the measurement and comes on top.
 *
 * The correction is part of the score: live and offline play it alike, and a score saved or exported carries it. A
 * concert's pieces each get their own; the engine glides from one to the next over eight bars. The measuring renders
 * 48 seconds, some seconds of work: eph_render does it before it renders, the plugin and the Quest while the piece
 * already plays -- the correction follows and glides in (Engine::setLevelTrims).
 */
#pragma once
#include "eph/Params.h"
#include "eph/Score.h"
#include <functional>
#include <vector>

namespace eph {

/** @brief What levelScore found for one piece. */
struct LevelReading {
    double beat = 0.0;       ///< the piece's start
    float measured = 0.0f;   ///< its loudest part as composed, LUFS
    float target = 0.0f;     ///< what it should measure
    float trim = 0.0f;       ///< the correction set, dB
    float after = 0.0f;      ///< the part with the correction, LUFS
};

/**
 * @brief Measures every piece of @p score (its LevelMarks) with the knobs of @p params and sets their corrections.
 * @param seconds how much of each loudest part is measured
 * @param stop    asked between blocks: true breaks off, and @p score is left as it was
 * @return what was found, a reading per piece (nothing if broken off)
 */
std::vector<LevelReading> levelScore(Score& score, const ParamStore& params, double seconds = 20.0,
                                     const std::function<bool()>& stop = {});

} // namespace eph
