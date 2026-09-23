/**
 * @file Sketch.h
 * @brief The sketch of Phase 2: a ten-minute piece whose hands, harmony and lead are generated
 *        (PLAN 12, Phase 2: "zehn Minuten mit drei Reihen und Transposition").
 *
 * Where the study of Phase 1 (Study.h) had every gesture written by hand, the sketch has the building
 * blocks of Phase 2 play it: a bass row from the start, a counter row of thirteen from about an eighth,
 * a walking row of twelve eighths an octave up from about a third to the close; a transposer row of
 * eight steps of four bars from a fifth of the way, drawing its roots from the style's progression and
 * coming home when it stops; a lead in two passages, the second denser; and two hands on seven knobs
 * following an energy arc that rises to about two thirds of the piece and falls to the end.
 *
 * It is still not the composer (Phase 4): the form is fixed, only its contents are drawn. It exists to
 * hear the parts of Phase 2 together, and as the reference piece of the tests.
 */
#pragma once
#include "eph/Params.h"
#include "eph/Score.h"
#include <cstdint>

namespace eph {

/** @brief The energy arc of the sketch at a fraction @p f of its length: 0.15 .. 1 at 0.65 .. 0.1. */
float sketchEnergy(double f);

/**
 * @brief Writes the sketch.
 * @param p       parameters (tempo, key, scale, style, voices); the rows' settings are the sketch's own
 * @param seed    seed of everything drawn
 * @param minutes length (at least four)
 */
Score buildSketch(const ParamStore& p, uint64_t seed, double minutes);

} // namespace eph
