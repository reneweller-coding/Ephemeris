/**
 * @file Study.h
 * @brief The study of Phase 1: one sequence that breathes (PLAN 12).
 *
 * Until the composer exists (Phase 4) this writes a short piece by hand-made rules, so that the rack,
 * the voices, the gestures and the echo can be heard together and judged: a Berlin bass on row 1, a
 * counter row of thirteen steps from about a tenth of the way in, a transposition cycle i - i - bVI -
 * bVII every eight bars after the opening pedal, and the hands: the filter opening over minutes, the
 * decay lengthened and taken back, an echo thrown, a dip, a peak with resonance, the close. Never more
 * than two hands at once (PLAN 6.4). Positions are fractions of the length, rounded to bars, so any
 * length from two minutes up keeps the same shape.
 */
#pragma once
#include "eph/Params.h"
#include "eph/Score.h"
#include <cstdint>

namespace eph {

/**
 * @brief Writes the study.
 * @param p       parameters (tempo, key, scale, rows, voices)
 * @param seed    seed of the rack and the voices
 * @param minutes length
 */
Score buildStudy(const ParamStore& p, uint64_t seed, double minutes);

} // namespace eph
