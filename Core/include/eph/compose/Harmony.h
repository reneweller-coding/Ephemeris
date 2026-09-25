/**
 * @file Harmony.h
 * @brief The harmony of a piece (PLAN 2.5, 6.5; since 25.09.2026 after the "Harmonie- und Stilguide Moderne
 *        Berlin School", its sections 3.x): a chord track over the centre, the transposer's moves, the chords.
 *
 * This music is drone harmony in church modes, not functional harmony: a centre is held for minutes by the
 * bass and the drone, chords are surfaces above it, and they change rarely -- most elegantly by moving only
 * the bass note under an unchanged sequence (the guide's 3.5 D). The harmony has three layers:
 *
 * **The chord track** (drawChordTrack): degrees of the mode over time, in one of four classes (3.5) --
 * *Static* (one chord for the whole phase), *Pendulum* (two chords alternating, i-VII, i-VI, i-iv, i-IV in
 * Dorian, i-bII in Phrygian ...), *Loop* (three or four chords round, i-VI-VII, i-VII-VI, i-iv-VI ...) and
 * *Walk* (the guide's Markov table 3.6, one draw per block). The classes' weights and the harmonic rhythm
 * (how many bars a chord lasts) are the style's:
 *
 * | Style   | Static | Pendulum | Loop | Walk | bars per chord | stays (3.6) |
 * |---------|--------|----------|------|------|----------------|-------------|
 * | Cosmic  | 60 %   | 22 %     | 10 % |  8 % | 8 .. 16        | 75 %        |
 * | Doom    | 25 %   | 43 %     | 27 % |  5 % | 4 .. 16        | 50 %        |
 * | Melodic | 15 %   | 37 %     | 30 % | 18 % | 2 .. 8         | 45 %        |
 * | Modern  | 37 %   | 35 %     | 15 % | 13 % | 4 .. 16        | 55 %        |
 * | Drift   | 35 %   | 45 %     | 20 % |  0 % | 8 .. 32        | 55 %        |
 *
 * Calibrated on 25.09.2026 against the reference recordings (Tools/analyze_harmony.py; the bass's pitch class per
 * two seconds): half the guide's first values, half the measurement -- Cosmic sits 94 % of the time on i, Melodic
 * swings in pendulums (III, VI, VII after the tonic) with chords of about three bars, Modern, Doom and Drift move
 * more than the guide assumed. The modes were not changed: the bass's fifth harmonic reads as a major third, so a
 * chroma this simple cannot tell the minor modes apart.
 *
 * Only degrees whose fifth is perfect are drawn (no diminished chord; the tonic of Locrian excepted), and
 * nothing chromatic: bII exists in Phrygian, where it is a degree of the mode. The composer plays the track
 * on the bass row alone (RackOp::Chord): the counter rows keep their notes and are heard anew over the
 * wandering bass, the drone stays on the centre.
 *
 * **The transposer** (drawProgression): the sequencer transposition of 3.7 -- the whole sequence up a fifth,
 * up a fourth or down a minor third and back, in the chain tonic, move, tonic, other move, tonic; four to
 * sixteen bars a stage. Cosmic moves by +7 and +5, Doom by +5 and -3, Melodic by all three, Modern by +7
 * and +5, Drift by +5 alone.
 *
 * **The chords** (chordTones): the vocabulary of 3.4 -- open fifth, the mode's triad (minor on i, major on
 * VI, VII, III), sus2 and sus4, add9, stacked fourths, a triad over the triad a fifth up (m9), the seventh
 * (a major seventh over VI) -- for the string machine; the tape keys' choirs play plain triads mostly. No
 * dominant with a leading tone, no suspension that is not a perfect fourth or a whole tone.
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/Params.h"
#include <utility>
#include <vector>

namespace eph {

/**
 * @brief Draws the roots of a transposer row: the chain of sequencer transpositions.
 * @param style        the style profile
 * @param scale        the mode: a move is left out where the centre would fall outside the moved scale
 *                     (a fifth up in Lydian, a fourth up in Locrian, a minor third down in Mixolydian), so the
 *                     drone held on the centre always belongs to the rows' scale
 * @param steps        how many steps (the transposer row's length)
 * @param barsPerStep  bars per step (the row's division), so a stage lasts four to sixteen bars
 * @param rng          the row's stream
 * @return semitone offsets from the tonic, first and last 0
 */
std::vector<int> drawProgression(Style style, int scale, int steps, int barsPerStep, Rng& rng);

/** @brief The roots a style moves between, for tests and for mutating a transposer row. */
const std::vector<int>& progressionRoots(Style style);

/** @brief The classes of chord progression (the guide's 3.5). */
enum class ChordClass : int { Static, Pendulum, Loop, Walk };

/**
 * @brief Draws a chord track between two beats: (beat, degree of the mode), starting on the tonic at @p from.
 *        Changes fall on whole bars from @p from on; the caller returns to the tonic at @p to.
 * @param style  the style (classes' weights, harmonic rhythm)
 * @param scale  the mode (compose.scale order)
 * @param from   first beat (a bar line)
 * @param to     end (no change at or after it)
 * @param rng    the harmony's stream
 * @param drawn  if not null, receives the class drawn
 */
std::vector<std::pair<double, int>> drawChordTrack(Style style, int scale, double from, double to, Rng& rng,
                                                   ChordClass* drawn = nullptr);

/** @brief Whether the chord on @p degree of @p scale has a perfect fifth (the tonic of Locrian counts as usable). */
bool usableDegree(int scale, int degree);

/**
 * @brief The degree to move a bass row by for @p degree: the same, or an octave of degrees lower, whichever
 *        keeps the bass within a tritone of the centre (VI and VII go down, iv and III up).
 */
int bassDegree(int scale, int degree);

/** @brief The chord kinds of the vocabulary (the guide's 3.4). */
enum class ChordKind : int { Fifth, Triad, Sus2, Sus4, Add9, Quartal, Stack, Seventh };

/** @brief Draws a kind for a chord on @p degree: plain triads for a choir (@p choir), the whole vocabulary otherwise. */
ChordKind drawChordKind(int scale, int degree, bool choir, Rng& rng);

/**
 * @brief The tones of a chord in semitones above the centre (may pass the octave; the open fifth doubles its
 *        root an octave up). A kind the degree cannot carry -- a sus4 on a tritone, a fifth that is not perfect --
 *        falls back to the triad.
 */
std::vector<int> chordTones(int scale, int degree, ChordKind kind);

} // namespace eph
