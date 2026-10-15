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
 * @author Rene Weller
 */
#pragma once
#include "eph/Clock.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace eph {

/** @brief The parts of the score, one MIDI track each (PLAN 7). Appended to, never reordered. */
enum class PartE : int { ROW1 = 0, ROW2, ROW3, ROW4, ROW5, ROW6, ROW7, ROW8,
                        LEAD, TAPE_KEYS, STRINGS, PAD, DRONE, ATMOS, DRUMS, COUNT };
constexpr int NumParts = static_cast<int>(PartE::COUNT);   ///< number of parts
extern const char* const PartNames[NumParts];            ///< "row1" .. "row8", "lead", "tape", ...
/**
 * @brief The part of row @p r (0-based).
 * @param[in] r  the row, 0-based
 * @return its part
 */
constexpr PartE rowPart(int r) { return static_cast<PartE>(static_cast<int>(PartE::ROW1) + r); }

/** @brief One note. */
struct NoteEvent {
    double m_beat = 0.0;        ///< onset in beats
    double m_length = 0.25;     ///< duration in beats
    PartE m_part = PartE::ROW1;   ///< which part plays it
    int m_pitch = 60;           ///< MIDI note number
    float m_velocity = 0.8f;    ///< 0..1
    bool m_accent = false;      ///< accented step (filter envelope and level up)
    bool m_slide = false;       ///< glide into the next note
    float m_bright = 0.0f;      ///< the modulation sequencer's step: octaves on the filter's cutoff for this note (25.09.2026)
    float m_decay = 0.0f;       ///< the second lane's step: the filter envelope's decay times 2^decay for this note
};

/** @brief How a gesture moves between its two values. */
enum class GestureShapeE : uint8_t {
    MINIMUM_JERK,   ///< the smooth S of a human reach: 10t^3 - 15t^4 + 6t^5 (Flash and Hogan 1985)
    LINEAR,        ///< constant speed
    EASE_IN,        ///< slow start, t^2 -- a knob that is grabbed, then turned
    EASE_OUT,       ///< fast start, 1 - (1 - t)^2 -- a throw that settles
    STEP,          ///< jumps to the end value at the start (a switch, a quick grab)
};

/** @brief One movement of one knob. */
struct Gesture {
    int m_param = -1;             ///< parameter id (Params.h) the hand moves
    double m_beat = 0.0;          ///< start in beats
    double m_length = 16.0;       ///< duration in beats (0 behaves like Step)
    float m_from = 0.0f;          ///< offset at the start, in the knob's normalised range (-1..1)
    float m_to = 0.0f;            ///< offset at the end
    GestureShapeE m_shape = GestureShapeE::MINIMUM_JERK;   ///< the path between the two
    uint8_t m_hand = 0;           ///< which of the player's two hands (PLAN 6.4: never more than two at once)
};

/**
 * @brief Position 0..1 along a shape at normalised time @p t (clamped to 0..1).
 * @param[in] shape  the shape
 * @param[in] t      the position in the gesture, 0..1
 * @return the shape's value, 0..1
 */
double gestureShape(GestureShapeE shape, double t);
/**
 * @brief Offset of one gesture at @p beat: @p from before it, its curve during it, @p to after it.
 * @param[in] g     the gesture
 * @param[in] beat  the position in beats
 * @return its offset there
 */
float gestureValue(const Gesture& g, double beat);

/** @brief What a rack event does to its row. */
enum class RackOpE : uint8_t {
    START,        ///< the row begins (value unused)
    STOP,         ///< the row stops
    TRANSPOSE,    ///< transposition in semitones becomes @p value
    SET_LENGTH,    ///< the row's length becomes @p value steps
    MUTATE,       ///< step @p value changes (the new content is in the notes that follow)
    KEY,          ///< the whole rack moves to a new key, @p value semitones from the piece's (row unused; Phase 4)
    CHORD,        ///< the row's steps move by @p value scale degrees (the bass wandering under the sequence; -1: all rows)
    SCALE,        ///< the rack plays in scale @p value (compose.scale order) from here: a parallel change of mode
    RATCHET,      ///< @p value steps of the row, drawn anew, split into two to four triggers (0: none)
    THIN,         ///< @p value more steps of the row fall silent: the sequence loses its pieces
    DIVISION,     ///< the row's division becomes @p value (RowDivision) on its next step: the pulse doubled or halved
    FILL,         ///< @p value of the row's thinned steps sound again (a row that came in with rests fills up)
    // The variations of the style guide's 4.3 (25.09.2026, the variation planner):
    GATE,         ///< @p value steps of the row switch: a sounding one falls silent or a silent one sounds (half keep sounding)
    OCTAVE_STEP,   ///< @p value sounding steps move an octave (up mostly; back where they were moved)
    DIRECTION,    ///< the row's direction becomes @p value (RowDirection) from its next step
    THEME,        ///< the row goes back to its pattern as drawn: what gates, octaves and mutations changed is undone
};

/** @brief One change to the rack. */
struct RackEvent {
    double m_beat = 0.0;          ///< when, in beats
    int m_row = 0;                ///< which row (0-based)
    RackOpE m_op = RackOpE::START;  ///< what happens
    int m_value = 0;              ///< the operation's argument
};

/** @brief A sequencer row's shape as composed, from a beat on: what a display needs to draw it (the orrery). */
struct RowShape {
    double m_from = 0.0;        ///< the beat it holds from (each piece of a concert brings its own)
    int m_row = 0;              ///< which row (0-based)
    int m_length = 16;          ///< steps per cycle
    double m_divBeats = 0.25;   ///< beats per step
    bool m_transposer = false;  ///< the transposer row
};

/** @brief A factory preset the composer chose for a synth, from a beat on (Presets.h; compose.pick_sounds): what a page shows. */
struct SoundPick {
    double m_beat = 0.0;   ///< from where it holds (each piece of a concert brings its own)
    int m_module = 0;      ///< the synth's Module, as an int
    int m_instance = 0;    ///< which instance (the row, for the voices)
    int m_preset = -1;     ///< its index in factoryPresets(module)
};

/**
 * @brief A knob the composer sets, from a beat on (26.09.2026): the sound of a synth as a program change -- the preset it
 *        chose for the piece (compose.pick_sounds) -- and the mix it sets for the piece (the faders, pans, sends, low
 *        cuts, distances), set on the knob itself where the piece begins. Unlike a gesture it is no offset: the page
 *        shows it, and a hand that turns the knob turns it from there.
 */
struct KnobSet {
    double m_beat = 0.0;    ///< from where it holds (each piece of a concert brings its own)
    int m_param = -1;       ///< the parameter
    float m_value = 0.0f;   ///< its value
    int m_module = 0;       ///< the synth it belongs to (Module, as an int): a program change is per synth
    int m_instance = 0;     ///< ... and its instance (the row, for the voices)
    int m_kind = 0;         ///< 0 a sound (a preset's knob), 1 a setting of the mix (a fader, a pan, a send ...)
};

/**
 * @brief Where a piece's loudness is set (26.09.2026, Leveler.h): its start, where its loudest part begins, what that part
 *        should measure, and the correction found for it -- the master gain from the piece's start on.
 */
struct LevelMark {
    double m_beat = 0.0;          ///< the piece's start
    double m_peakBeat = 0.0;      ///< where its loudest part begins (its peak)
    float m_targetLufs = -12.0f;  ///< what that part should measure (the style's)
    float m_trimDb = 0.0f;        ///< the correction (levelScore); 0 until measured
};

/** @brief A named position: a phase of a piece, the start of a piece. */
struct Marker {
    double m_beat = 0.0;   ///< position in beats
    std::string m_text;    ///< name, e.g. "Atmo", "Build", "Piece 2"
};

/**
 * @brief The root offset in force at @p beat in a list of (beat, semitones) changes (0 before the first).
 * @param[in] shifts  the (beat, semitones) changes, in order
 * @param[in] beat    the position in beats
 * @return the root offset in semitones
 */
int rootShiftAt(const std::vector<std::pair<double, int>>& shifts, double beat);

/** @brief A score of one piece or a whole concert. */
struct Score {
    TempoMap m_tempo;                    ///< the tempo map; every other time is in beats
    uint64_t m_seed = 1;                 ///< the seed the score was composed from; the voices' drift derives from it
    int m_keyRoot = 9;                   ///< pitch class of the key (A = 9), for the MIDI key signature
    double m_lengthBeats = 0.0;          ///< where the score ends
    std::vector<NoteEvent> m_notes;      ///< notes, sorted by beat after sort()
    std::vector<Gesture> m_gestures;     ///< gestures, sorted by beat after sort()
    std::vector<RackEvent> m_rack;       ///< rack events, sorted by beat after sort()
    std::vector<Marker> m_markers;       ///< markers, sorted by beat after sort()
    std::vector<RowShape> m_rowShapes;   ///< the rows as composed (Composer), in beat order; nothing plays from it
    std::vector<SoundPick> m_sounds;     ///< the composer's presets, in beat order: what the pages show
    std::vector<KnobSet> m_knobs;        ///< the knobs the composer sets to its presets, in beat order (Engine::load, updateCell)
    std::vector<LevelMark> m_levels;     ///< every piece's loudness mark, in beat order (Leveler.h)
    /** The transposer's roots over time as (beat, semitones), written by whoever ran the rack; the
     *  atmosphere's bleeps and anything else that must be in the rows' root read it. */
    std::vector<std::pair<double, int>> m_rootShifts;
    /** The scale (compose.scale order) over time as (beat, scale), written by whoever ran the rack (Rack::scales):
     *  a parallel change of mode (RackOp::Scale) shows here. Empty: compose.scale throughout. */
    std::vector<std::pair<double, int>> m_scaleShifts;

    /** @brief Sorts every list by beat (stable, so equal beats keep the order they were written in). */
    void sort();
    /**
     * @brief Empties the score and resets the tempo to @p bpm.
     * @param[in] bpm  the new tempo in beats per minute
     */
    void clear(double bpm);
    /**
     * @brief The gesture offset of @p param at @p beat, in the normalised range.
     *
     * Of the gestures on one knob, the latest that has started counts (a new movement starts from
     * where the knob is); before the first one the offset is 0. Linear in the number of gestures --
     * for the offline render and the tests; the audio thread keeps a cursor instead.
     * @param[in] param  the parameter
     * @param[in] beat   the position in beats
     * @return the automation offset in the normalised range
     */
    float gestureOffset(int param, double beat) const;
    /**
     * @brief The transposer's root offset at @p beat (rootShifts).
     * @param[in] beat  the position in beats
     * @return the root offset in semitones
     */
    int rootAt(double beat) const { return rootShiftAt(m_rootShifts, beat); }
    /**
     * @brief The scale at @p beat (scaleShifts), @p fallback where the score has none.
     * @param[in] beat      the position in beats
     * @param[in] fallback  the scale where the score has none
     * @return the scale
     */
    int scaleAt(double beat, int fallback) const { return m_scaleShifts.empty() ? fallback : rootShiftAt(m_scaleShifts, beat); }
};

} // namespace eph
