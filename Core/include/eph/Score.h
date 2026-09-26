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
    float bright = 0.0f;      ///< the modulation sequencer's step: octaves on the filter's cutoff for this note (25.09.2026)
    float decay = 0.0f;       ///< the second lane's step: the filter envelope's decay times 2^decay for this note
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
    Chord,        ///< the row's steps move by @p value scale degrees (the bass wandering under the sequence; -1: all rows)
    Scale,        ///< the rack plays in scale @p value (compose.scale order) from here: a parallel change of mode
    Ratchet,      ///< @p value steps of the row, drawn anew, split into two to four triggers (0: none)
    Thin,         ///< @p value more steps of the row fall silent: the sequence loses its pieces
    Division,     ///< the row's division becomes @p value (RowDivision) on its next step: the pulse doubled or halved
    Fill,         ///< @p value of the row's thinned steps sound again (a row that came in with rests fills up)
    // The variations of the style guide's 4.3 (25.09.2026, the variation planner):
    Gate,         ///< @p value steps of the row switch: a sounding one falls silent or a silent one sounds (half keep sounding)
    OctaveStep,   ///< @p value sounding steps move an octave (up mostly; back where they were moved)
    Direction,    ///< the row's direction becomes @p value (RowDirection) from its next step
    Theme,        ///< the row goes back to its pattern as drawn: what gates, octaves and mutations changed is undone
};

/** @brief One change to the rack. */
struct RackEvent {
    double beat = 0.0;          ///< when, in beats
    int row = 0;                ///< which row (0-based)
    RackOp op = RackOp::Start;  ///< what happens
    int value = 0;              ///< the operation's argument
};

/** @brief A sequencer row's shape as composed, from a beat on: what a display needs to draw it (the orrery). */
struct RowShape {
    double from = 0.0;        ///< the beat it holds from (each piece of a concert brings its own)
    int row = 0;              ///< which row (0-based)
    int length = 16;          ///< steps per cycle
    double divBeats = 0.25;   ///< beats per step
    bool transposer = false;  ///< the transposer row
};

/** @brief A factory preset the composer chose for a synth, from a beat on (Presets.h; compose.pick_sounds): what a page shows. */
struct SoundPick {
    double beat = 0.0;   ///< from where it holds (each piece of a concert brings its own)
    int module = 0;      ///< the synth's Module, as an int
    int instance = 0;    ///< which instance (the row, for the voices)
    int preset = -1;     ///< its index in factoryPresets(module)
};

/**
 * @brief A knob the composer sets, from a beat on (26.09.2026): the sound of a synth as a program change -- the preset it
 *        chose for the piece (compose.pick_sounds), set on the knob itself where the piece begins. Unlike a gesture it
 *        is no offset: the page shows it, and a hand that turns the knob turns it from there.
 */
struct KnobSet {
    double beat = 0.0;    ///< from where it holds (each piece of a concert brings its own)
    int param = -1;       ///< the parameter
    float value = 0.0f;   ///< its value
    int module = 0;       ///< the synth it belongs to (Module, as an int): a program change is per synth
    int instance = 0;     ///< ... and its instance (the row, for the voices)
};

/** @brief A named position: a phase of a piece, the start of a piece. */
struct Marker {
    double beat = 0.0;   ///< position in beats
    std::string text;    ///< name, e.g. "Atmo", "Build", "Piece 2"
};

/** @brief The root offset in force at @p beat in a list of (beat, semitones) changes (0 before the first). */
int rootShiftAt(const std::vector<std::pair<double, int>>& shifts, double beat);

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
    std::vector<RowShape> rowShapes;   ///< the rows as composed (Composer), in beat order; nothing plays from it
    std::vector<SoundPick> sounds;     ///< the composer's presets, in beat order: what the pages show
    std::vector<KnobSet> knobs;        ///< the knobs the composer sets to its presets, in beat order (Engine::load, updateCell)
    /** The transposer's roots over time as (beat, semitones), written by whoever ran the rack; the
     *  atmosphere's bleeps and anything else that must be in the rows' root read it. */
    std::vector<std::pair<double, int>> rootShifts;
    /** The scale (compose.scale order) over time as (beat, scale), written by whoever ran the rack (Rack::scales):
     *  a parallel change of mode (RackOp::Scale) shows here. Empty: compose.scale throughout. */
    std::vector<std::pair<double, int>> scaleShifts;

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
    /** @brief The transposer's root offset at @p beat (rootShifts). */
    int rootAt(double beat) const { return rootShiftAt(rootShifts, beat); }
    /** @brief The scale at @p beat (scaleShifts), @p fallback where the score has none. */
    int scaleAt(double beat, int fallback) const { return scaleShifts.empty() ? fallback : rootShiftAt(scaleShifts, beat); }
};

} // namespace eph
