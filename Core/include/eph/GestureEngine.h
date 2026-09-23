/**
 * @file GestureEngine.h
 * @brief The player's hands (PLAN 6.4): what moves the knobs, when, how far and how fast.
 *
 * Two hands, each a process in time: it takes a knob, moves it along a curve to a new place, lets go,
 * rests, takes the next. The rules that make it sound like a person:
 * - **Never more than two hands**, and never both on the same knob.
 * - **Continuity**: a movement starts where the knob was left, so no value ever jumps (Score.h keeps
 *   the knob where the last gesture ended).
 * - **Human shape**: most movements follow the minimum-jerk profile of a reach (Flash and Hogan 1985);
 *   some start slowly (a knob grabbed, then turned), a few are quick twists that settle.
 * - **Durations** are log-normal around a median, with the odd long sweep over a minute or more.
 *   Calibration: the first reference measurement (PLAN, Stand 24.09.2026) found brightness moving about
 *   five times a minute in every profile, median 9 s, 0.5 to 0.7 octave -- in arrangements with more
 *   going on than the hands. The user heard the first study, which moved 2.2 times a minute, as slow.
 *   The defaults below give two hands together about five movements a minute (throws and their catches
 *   counted), a median of 8 s each and a median rest of 14 s per hand -- the references' rate, before
 *   entries and transpositions add their own. (A first setting of 4 s rest gave 8.9 a minute: the two
 *   hands had been forgotten in the sum.)
 * - **The energy** of the moment (0..1) sets where a knob tends to go: a filter opens as the piece
 *   builds and closes as it ends; around that centre the target scatters.
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/Score.h"
#include <functional>
#include <vector>

namespace eph {

/** @brief One knob the hands may move, with its range in offsets of the normalised knob. */
struct HandKnob {
    int param = -1;           ///< parameter id
    float low = -0.4f;        ///< lowest offset a hand takes it to
    float high = 0.4f;        ///< highest offset
    float weight = 1.0f;      ///< how often it is chosen
    float atRest = 0.0f;      ///< its centre at energy 0
    float atPeak = 0.0f;      ///< its centre at energy 1
    float scatter = 0.12f;    ///< spread of targets around the centre
    bool throws = false;      ///< moved in quick throws and caught again (an echo's feedback)
    double from = 0.0;        ///< beat before which nobody touches it (a row that has not started yet)
};

/** @brief The timing of the hands. */
struct HandStyle {
    double medianSeconds = 8.0;     ///< median duration of a movement
    double spread = 0.7;            ///< log-normal sigma of the durations
    double longChance = 0.12;       ///< chance of a long sweep (4 to 10 times the median)
    double quickChance = 0.10;      ///< chance of a quick twist (1 to 2 s)
    double restSeconds = 14.0;      ///< median rest between two movements of one hand (two hands: ~4 a minute together)
};

/**
 * @brief Plays two hands over a piece and writes their gestures into the score.
 * @param score   the score (gestures are appended; tempo map read)
 * @param knobs   the knobs, with their ranges and weights
 * @param style   timing
 * @param energy  the energy at a beat, 0..1
 * @param start   first beat
 * @param end     last beat; no movement runs past it
 * @param rng     the stream of the hands
 */
void playHands(Score& score, const std::vector<HandKnob>& knobs, const HandStyle& style,
               const std::function<float(double)>& energy, double start, double end, Rng& rng);

} // namespace eph
