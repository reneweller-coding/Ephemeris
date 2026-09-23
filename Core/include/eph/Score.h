/**
 * @file Score.h
 * @brief The score: notes, gestures, rack events and markers of a piece or a concert, on a tempo map.
 *
 * The composer writes the score ahead of the audio thread (PLAN 3); the audio render, the MIDI export
 * and the `.ephset` file all read the same score. Three kinds of content:
 *
 * - **Notes** of every part, in beats.
 * - **Gestures**: what a player's hand does to a knob -- a filter opened over two minutes, an echo
 *   thrown at the end of a phrase (PLAN 6.4). A gesture is a *curve*, not a list of points: start,
 *   length, from, to, shape. It may be longer than the composer's lookahead, and the audio thread
 *   evaluates it wherever it is. Values are **offsets in the knob's normalised range** (-1..1), added
 *   to where the user left the knob: the user keeps the range, the hand moves within it. After a
 *   gesture ends its end value stays, as a knob stays where the hand let go of it, until the next
 *   gesture on the same knob starts.
 * - **Rack events**: transposition, a row starting or stopping, a mutation, a new length (PLAN 5.1),
 *   so that locking and rerolling can work on a row.
 */
#pragma once
#include "eph/Clock.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace eph {

/** @brief The parts of the score, one MIDI track each (PLAN 7). Appended to, never reordered. */
enum class Part : int { Row1 = 0, Row2, Row3, Row4, Row5, Row6, Row7, Row8,
                        Lead, TapeKeys, Strings, Pad, Drone, Atmos, Drums, Count };
constexpr int kNumParts = static_cast<int>(Part::Count);   ///< number of parts
extern const char* const kPartNames[kNumParts];            ///< "row1" .. "row8", "lead", "tape", ...
/** @brief The part of row @p r (0-based). */
constexpr Part rowPart(int r) { return static_cast<Part>(static_cast<int>(Part::Row1) + r); }

/** @brief One note. */
struct NoteEvent {
    double beat = 0.0;        ///< onset in beats
    double length = 0.25;     ///< duration in beats
    Part part = Part::Row1;   ///< which part plays it
    int pitch = 60;           ///< MIDI note number
    float velocity = 0.8f;    ///< 0..1
    bool accent = false;      ///< accented step (filter envelope and level up)
    bool slide = false;       ///< glide into the next note
};

/** @brief How a gesture moves between its two values. */
enum class GestureShape : uint8_t {
    MinimumJerk,   ///< the smooth S of a human reach: 10t^3 - 15t^4 + 6t^5 (Flash and Hogan 1985)
    Linear,        ///< constant speed
    EaseIn,        ///< slow start, t^2 -- a knob that is grabbed, then turned
    EaseOut,       ///< fast start, 1 - (1 - t)^2 -- a throw that settles
    Step,          ///< jumps to the end value at the start (a switch, a quick grab)
};

/** @brief One movement of one knob. */
struct Gesture {
    int param = -1;             ///< parameter id (Params.h) the hand moves
    double beat = 0.0;          ///< start in beats
    double length = 16.0;       ///< duration in beats (0 behaves like Step)
    float from = 0.0f;          ///< offset at the start, in the knob's normalised range (-1..1)
    float to = 0.0f;            ///< offset at the end
    GestureShape shape = GestureShape::MinimumJerk;   ///< the path between the two
    uint8_t hand = 0;           ///< which of the player's two hands (PLAN 6.4: never more than two at once)
};

/** @brief Position 0..1 along a shape at normalised time @p t (clamped to 0..1). */
double gestureShape(GestureShape shape, double t);
/** @brief Offset of one gesture at @p beat: @p from before it, its curve during it, @p to after it. */
float gestureValue(const Gesture& g, double beat);

/** @brief What a rack event does to its row. */
enum class RackOp : uint8_t {
    Start,        ///< the row begins (value unused)
    Stop,         ///< the row stops
    Transpose,    ///< transposition in semitones becomes @p value
    SetLength,    ///< the row's length becomes @p value steps
    Mutate,       ///< step @p value changes (the new content is in the notes that follow)
    Key,          ///< the whole rack moves to a new key, @p value semitones from the piece's (row unused; Phase 4)
};

/** @brief One change to the rack. */
struct RackEvent {
    double beat = 0.0;          ///< when, in beats
    int row = 0;                ///< which row (0-based)
    RackOp op = RackOp::Start;  ///< what happens
    int value = 0;              ///< the operation's argument
};

/** @brief A named position: a phase of a piece, the start of a piece. */
struct Marker {
    double beat = 0.0;   ///< position in beats
    std::string text;    ///< name, e.g. "Atmo", "Build", "Piece 2"
};

/** @brief A score of one piece or a whole concert. */
struct Score {
    TempoMap tempo;                    ///< the tempo map; every other time is in beats
    uint64_t seed = 1;                 ///< the seed the score was composed from; the voices' drift derives from it
    int keyRoot = 9;                   ///< pitch class of the key (A = 9), for the MIDI key signature
    double lengthBeats = 0.0;          ///< where the score ends
    std::vector<NoteEvent> notes;      ///< notes, sorted by beat after sort()
    std::vector<Gesture> gestures;     ///< gestures, sorted by beat after sort()
    std::vector<RackEvent> rack;       ///< rack events, sorted by beat after sort()
    std::vector<Marker> markers;       ///< markers, sorted by beat after sort()
    /** The transposer's roots over time as (beat, semitones), written by whoever ran the rack; the
     *  atmosphere's bleeps and anything else that must be in the rows' root read it. */
    std::vector<std::pair<double, int>> rootShifts;

    /** @brief Sorts every list by beat (stable, so equal beats keep the order they were written in). */
    void sort();
    /** @brief Empties the score and resets the tempo to @p bpm. */
    void clear(double bpm);
    /**
     * @brief The gesture offset of @p param at @p beat, in the normalised range.
     *
     * Of the gestures on one knob, the latest that has started counts (a new movement starts from
     * where the knob is); before the first one the offset is 0. Linear in the number of gestures --
     * for the offline render and the tests; the audio thread keeps a cursor instead.
     */
    float gestureOffset(int param, double beat) const;
};

} // namespace eph
