/**
 * @file Rack.h
 * @brief The sequencer rack: rows of steps that run against each other (PLAN 5.1).
 *
 * The rack is a model of the step sequencers the style is played on: up to eight rows, each with its
 * own length (1 to 32 steps), its own division, its own direction, a transposition input and a
 * mutation. It runs on the composer's side, not in the audio thread: the composer plays it forward and
 * the notes it produces go into the score, so the MIDI export, the offline render and the plugin hear
 * the same notes and a row can be locked or rerolled on its own.
 *
 * **Polymetry.** A row's step k falls on beat start + k * division. Rows of different lengths are not
 * reset against each other: a sixteen against a thirteen comes back to its first alignment after 208
 * steps, and the composer knows those *conjunctions* (conjunctionSteps()) and prefers them for changes
 * of form (PLAN 6.2).
 *
 * **Mutation.** At the end of every cycle, with the row's mutation chance, exactly one step changes --
 * the one bit a Turing machine's shift register flips (T. Whitwell, Music Thing Modular, 2012), and the
 * rule Noctuary's near layer plays by (`Near.h`, runSequence). What changes is drawn in shares: the
 * degree most, then the octave, the gate, the accent. Step 0 always sounds, so the row keeps its
 * downbeat however long it mutates.
 *
 * **Transposition** from the score's rack events takes effect on the first step at or after the
 * event's beat, as a sequencer's transposer does: never in the middle of a note.
 *
 * **The transposer row** (row.mode = Transposer, Phase 2) plays no notes. Its steps are semitone offsets
 * from the tonic, drawn from the style's progression (Harmony.h), usually on a slow division of one,
 * two or four bars; at each of its steps every note row is shifted by that offset from its next step
 * on. A fast row riding on a slow one is the epicycle of PLAN 5.1. At equal beats the transposer steps
 * first, so the downbeat of a bar already sounds in the new root.
 *
 * **The chord** (RackOp::Chord, 25.09.2026, after the style guide's 3.5 D): a row's steps move by some
 * degrees of the scale -- diatonically, so the bass row under an unchanged sequence plays the sixth
 * degree's root, fifth and octave while the counter rows keep their notes. The sequence is heard anew
 * over the new bass: the Berlin-School way of changing chords without touching the machine.
 *
 * **A new mode** (RackOp::Scale): the rows play in another scale from the next step on, on the same
 * centre -- Aeolian brightening to Dorian at the peak (the style guide's 3.2).
 *
 * **Figures** (25.09.2026, the style guide's 4.2): a row is drawn from an archetype (Figure), eight steps
 * that the longer rows repeat with the odd octave changed; every eight steps have one to three rests (the
 * bass none to two) and accents on their first and fifth step (or 3+3+2); a style's share of steps are
 * probability gates (Modern the most, Cosmic and Doom the fewest). Ratchets (RackOp::Ratchet) split a
 * few steps into quick triggers, a thinning (RackOp::Thin) silences steps one by one, a new division
 * (RackOp::Division) doubles the pulse -- all drawn from each row's own dice, so the patterns and the
 * mutations stay what they were. A style's share of steps are random steps: each time round they play a
 * note drawn anew through a quantiser -- root, fifth, octave, seventh, fourth or third of the mode, the
 * style guide's interval stock (4.2, 4.6); Drift and Modern have the most, Cosmic and Doom the fewest.
 *
 * **A new key** (RackOp::Key, Phase 4) moves the whole rack -- rows, and the root the lead and the chords
 * follow -- by some semitones from the piece's key; the transposer then moves around the new key.
 * @author Rene Weller
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/Params.h"
#include "eph/Score.h"
#include <cstdint>
#include <vector>

namespace eph
{

constexpr int MaxSteps = 32;   ///< longest row

/** @brief One step of a row. */
struct Step
{
    int   m_degree = 0;        ///< scale degree above the row's root (may leave the octave)
    int   m_octave = 0;        ///< octave offset of this step
    bool  m_gate = true;       ///< plays at all
    float m_velocity = 0.8f;   ///< 0..1
    bool  m_accent = false;    ///< accented
    bool  m_slide = false;     ///< glides into the next step (legato)
    int   m_ratchet = 1;       ///< triggers the step is split into (RackOp::Ratchet)
    float m_chance = 1.0f;     ///< chance that the step sounds each time it comes round (a probability gate)
    bool  m_random = false;    ///< a new note each time it comes round, through a quantiser on the mode (4.6)
};

/**
 * @brief The figures a row is drawn from (the style guide's 4.2): the old free rules (Classic), and the
 *        archetypes -- octave pendulum (D2 D3 D2 D3 .. A2 D3), fifth anchor (D A D A' D A C A), the 3+1 pulse
 *        (D D D - D D D -), stairs up and down, the mode's colour (D F A B D' A F D in Dorian), the Phrygian
 *        push (D Eb D A D Eb D C), the arpeggio spiral (i, then VI), the canon (the row before, three steps
 *        later or a fifth up).
 */
enum class FigureE : int
{
    CLASSIC,
    OCTAVE_PENDULUM,
    FIFTH_ANCHOR,
    THREE_PLUS_ONE,
    STAIRS_UP,
    STAIRS_DOWN,
    COLOUR,
    PHRYGIAN_PUSH,
    SPIRAL,
    CANON,
    COUNT
};
/**
 * @brief The name of a figure ("Classic", "Octave Pendulum", ...).
 * @param[in] f  the figure
 * @return its name
 */
const char *figureName( FigureE f );
/**
 * @brief The degree that makes a mode heard (Aeolian b6, Dorian 6, Phrygian b2, Lydian #4 ...; the style guide's 3.2).
 * @param[in] scale  the scale (compose.scale order)
 * @return its characteristic degree
 */
int characterDegree( int scale );

/** @brief How a row's steps are drawn when it is generated. */
enum class RowRoleE : int
{
    BASS,         ///< the Berlin bass: root-heavy, octave jumps, the odd fifth or seventh, accents on the downbeats
    COUNTER,      ///< an arpeggio of chord tones an octave up, for odd lengths against the bass
    WALK,         ///< stepwise through the scale with the occasional leap
    TRANSPOSER,   ///< the roots of the style's progression (Harmony.h); `degree` holds semitones
};

/** @brief Number of scales (compose.scale): Aeolian, Dorian, Phrygian, Harmonic Minor, Minor Pentatonic,
 *         Mixolydian, Lydian, Locrian (the last three since 25.09.2026, after the style guide's 3.2). */
constexpr int Scales = 8;
/**
 * @brief Semitones of scale degree @p degree in scale @p scale (compose.scale order); any integer degree.
 * @param[in] scale   the scale (compose.scale order)
 * @param[in] degree  the degree, any integer
 * @return its semitones above the root
 */
int scaleSemitones( int scale, int degree );
/**
 * @brief Number of degrees in an octave of scale @p scale.
 * @param[in] scale  the scale (compose.scale order)
 * @return the number of degrees in an octave
 */
int scaleSize( int scale );
/**
 * @brief Steps after which rows of lengths @p a and @p b (same division) are both on step 0 again.
 * @param[in] a  the first row's length in steps
 * @param[in] b  the second row's length in steps
 * @return the steps until both are on step 0 again
 */
int conjunctionSteps( int a, int b );

/** @brief The rack: rows, their patterns and their running state. */
class Rack
{
public:
    /**
     * @brief Takes the row settings from the parameters and seeds every row.
     * @param[in] p     parameters (row module, compose.key and compose.scale)
     * @param[in] seed  the piece's seed; each row gets its own stream
     */
    void setup( const ParamStore &p, uint64_t seed );
    /**
     * @brief Draws a new pattern for row @p row from the rules of @p role (uses the row's stream).
     * @param[in] row   the row
     * @param[in] role  its role
     */
    void generate( int row, RowRoleE role );
    /**
     * @brief The pattern of a row, for inspection and for locking.
     * @param[in] row  the row
     * @return its pattern
     */
    const Step *steps( int row ) const { return m_rows[row].m_steps; }
    /**
     * @brief Current length of a row in steps.
     * @param[in] row  the row
     * @return its length in steps
     */
    int length( int row ) const { return m_rows[row].m_length; }
    /**
     * @brief The figure a row was last drawn from.
     * @param[in] row  the row
     * @return the figure it was last drawn from
     */
    FigureE figure( int row ) const { return m_rows[row].m_figure; }
    /**
     * @brief Mutations a row has made since setup().
     * @param[in] row  the row
     * @return how many mutations it has made
     */
    int mutations( int row ) const { return m_rows[row].m_mutations; }
    /**
     * @brief The transposer's current offset in semitones.
     * @return the offset in semitones
     */
    int shift() const { return m_shift; }
    /**
     * @brief Every change of the transposer's offset so far, as (beat, semitones), for the lead to follow.
     * @return the changes as (beat, semitones)
     */
    const std::vector<std::pair<double, int>> &shifts() const { return m_shiftLog; }
    /**
     * @brief Every change of the scale so far, as (beat, scale), starting with compose.scale at beat 0.
     * @return the changes as (beat, scale)
     */
    const std::vector<std::pair<double, int>> &scales() const { return m_scaleLog; }
    /** @brief The key's pitch class and the scale (compose.scale order) the rack plays in. */
    /**
     * the key's pitch class
     * @return the key's pitch class
     */
    int keyRoot() const { return m_keyRoot; }
    /**
     * the scale, compose.scale order
     * @return the scale, in compose.scale's order
     */
    int scale() const { return m_scale; }
    /**
     * @brief Division of a row in beats.
     * @param[in] row  the row
     * @return its division in beats
     */
    double divisionBeats( int row ) const { return m_rows[row].m_divBeats; }

    /**
     * @brief Plays the rack from its current position up to @p endBeat and writes the notes.
     *
     * Start, Stop, Transpose and SetLength events of @p score within the span are applied at step
     * boundaries; the mutations the rows make are appended to the score as Mutate events. Call
     * repeatedly with increasing @p endBeat -- the composer's lookahead -- or once for a whole piece.
     * @param[in,out] score    the score, written to
     * @param[in]     endBeat  the beat to play up to
     */
    void run( Score &score, double endBeat );

private:
    /** @brief One row of the rack: its pattern, its settings, where it is, its lanes and its streams. */
    struct Row
    {
        Step          m_steps[MaxSteps];                      ///< the pattern
        int           m_length = 16;                          ///< its length, steps
        double        m_divBeats = 0.25;                      ///< beats a step
        RowDirectionE m_direction = RowDirectionE::FORWARD;   ///< how it runs through the pattern
        int           m_octave = 0;                           ///< its octave offset
        int           m_transpose = 0;                        ///< its transposition, semitones
        int           m_chord = 0;                            ///< degrees the steps move by (RackOp::Chord)
        float         m_mutation = 0.0f;                      ///< how much it mutates (row.mutation)
        float         m_gate = 0.5f;                          ///< a step's length, a share of the step
        bool          m_running = false;                      ///< it plays
        double        m_startBeat = 0.0;                      ///< beat of step 0 of the current run
        int64_t       m_step = 0;                             ///< steps played since startBeat
        int           m_pos = 0;                              ///< index of the next step to play
        int           m_dir = 1;                              ///< pendulum direction
        int           m_mutations = 0;                        ///< mutations since setup()
        bool          m_transposer = false;                   ///< row.mode = Transposer
        double        m_nextMutation = 64.0;                  ///< the next 16-bar mark a mutation may fall on
        bool m_thinned[MaxSteps] = {};   ///< steps a thinning silenced (RackOp::Thin), which a Fill may bring back
        // The modulation sequencer (25.09.2026, after Stuertzer's Stepic "8 Modulationssequencer mit
        // unterschiedlichen Laengen"): a lane of cutoff offsets with a length of its own, stepping with the row.
        float m_mod[MaxSteps] = {};   ///< the modulation lane: a cutoff offset per step
        int   m_modLength = 1;        ///< the lane's own length
        int   m_modPos = 0;           ///< where in it the row is
        /// The second lane (25.09.2026, the style guide's 4.3 "Hüllkurven-Modulation"): the filter envelope's decay
        /// per step, in octaves of its time, a length of its own again.
        float   m_decay[MaxSteps] = {};
        int     m_decayLength = 1;             ///< the second lane's own length
        int     m_decayPos = 0;                ///< where in it the row is
        Step    m_theme[MaxSteps];             ///< the pattern as drawn (RackOp::Theme brings it back)
        Rng     m_lanes;                       ///< the second lane's stream, apart from the patterns and the dice
        FigureE m_figure = FigureE::CLASSIC;   ///< the figure it was last drawn from
        Rng     m_rng;                         ///< the patterns' stream
        Rng     m_dice;   ///< probability gates, ratchets and thinning: apart from the patterns' stream
    };
    /**
     * @brief The beat of row @p r's next step.
     * @param[in] r  the row
     * @return the beat of its next step
     */
    double nextStepBeat( const Row &r ) const { return r.m_startBeat + static_cast<double>( r.m_step ) * r.m_divBeats; }
    /**
     * @brief Writes the note of row @p r's next step (row @p index) into @p score; what it does goes to @p log.
     * @param[in]     index  the row's index
     * @param[in,out] r      the row
     * @param[in,out] score  the score, written to
     * @param[in,out] log    what happens, appended to
     */
    void playStep( int index, Row &r, Score &score, std::vector<RackEvent> &log );
    /**
     * @brief Moves row @p r on a step at @p beat: its direction, its lanes, a mutation where one is due.
     * @param[in,out] r      the row
     * @param[in]     index  the row's index
     * @param[in]     beat   the beat of the step
     * @param[in,out] log    what happens, appended to
     */
    void advance( Row &r, int index, double beat, std::vector<RackEvent> &log );
    /**
     * @brief Mutates row @p r at @p beat (a step changed within its rules) and logs it.
     * @param[in,out] r      the row
     * @param[in]     index  the row's index
     * @param[in]     beat   the beat of the mutation
     * @param[in,out] log    what happens, appended to
     */
    void mutate( Row &r, int index, double beat, std::vector<RackEvent> &log );
    /**
     * @brief The MIDI note of row @p r's root: the key, the row's octave and transposition, the transposer's shift.
     * @param[in] r  the row
     * @return the MIDI note of its root
     */
    int rootNote( const Row &r ) const;

    Row    m_rows[Rows];               ///< the rows
    int    m_keyRoot = 9;              ///< the key's pitch class
    int    m_scale = 0;                ///< the scale (compose.scale order)
    StyleE m_style = StyleE::COSMIC;   ///< the style the rows are drawn in
    int    m_shift = 0;                ///< the transposer's offset plus the key's, applied to every note row
    int    m_base = 0;                 ///< the key's offset from the piece's key (RackOp::Key)
    int    m_degree = 0;               ///< the transposer's own offset
    std::vector<std::pair<double, int>> m_shiftLog;   ///< every change of the transposer's offset, as (beat, semitones)
    std::vector<std::pair<double, int>> m_scaleLog;   ///< every change of the scale, as (beat, scale)
    double                              m_position = 0.0;   ///< beat up to which the rack has run
};

}   // namespace eph
