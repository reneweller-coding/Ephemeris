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
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/Params.h"
#include "eph/Score.h"
#include <cstdint>
#include <vector>

namespace eph {

constexpr int kMaxSteps = 32;   ///< longest row

/** @brief One step of a row. */
struct Step {
    int degree = 0;          ///< scale degree above the row's root (may leave the octave)
    int octave = 0;          ///< octave offset of this step
    bool gate = true;        ///< plays at all
    float velocity = 0.8f;   ///< 0..1
    bool accent = false;     ///< accented
    bool slide = false;      ///< glides into the next step (legato)
};

/** @brief How a row's steps are drawn when it is generated. */
enum class RowRole : int {
    Bass,      ///< the Berlin bass: root-heavy, octave jumps, the odd fifth or seventh, accents on the downbeats
    Counter,   ///< an arpeggio of chord tones an octave up, for odd lengths against the bass
    Walk,      ///< stepwise through the scale with the occasional leap
    Transposer,///< the roots of the style's progression (Harmony.h); `degree` holds semitones
};

/** @brief Semitones of scale degree @p degree in scale @p scale (compose.scale order); any integer degree. */
int scaleSemitones(int scale, int degree);
/** @brief Number of degrees in an octave of scale @p scale. */
int scaleSize(int scale);
/** @brief Steps after which rows of lengths @p a and @p b (same division) are both on step 0 again. */
int conjunctionSteps(int a, int b);

/** @brief The rack: rows, their patterns and their running state. */
class Rack {
public:
    /**
     * @brief Takes the row settings from the parameters and seeds every row.
     * @param p     parameters (row module, compose.key and compose.scale)
     * @param seed  the piece's seed; each row gets its own stream
     */
    void setup(const ParamStore& p, uint64_t seed);
    /** @brief Draws a new pattern for row @p row from the rules of @p role (uses the row's stream). */
    void generate(int row, RowRole role);
    /** @brief The pattern of a row, for inspection and for locking. */
    const Step* steps(int row) const { return rows_[row].steps; }
    /** @brief Current length of a row in steps. */
    int length(int row) const { return rows_[row].length; }
    /** @brief Mutations a row has made since setup(). */
    int mutations(int row) const { return rows_[row].mutations; }
    /** @brief The transposer's current offset in semitones. */
    int shift() const { return shift_; }
    /** @brief Every change of the transposer's offset so far, as (beat, semitones), for the lead to follow. */
    const std::vector<std::pair<double, int>>& shifts() const { return shiftLog_; }
    /** @brief The key's pitch class and the scale (compose.scale order) the rack plays in. */
    int keyRoot() const { return keyRoot_; }
    int scale() const { return scale_; }
    /** @brief Division of a row in beats. */
    double divisionBeats(int row) const { return rows_[row].divBeats; }

    /**
     * @brief Plays the rack from its current position up to @p endBeat and writes the notes.
     *
     * Start, Stop, Transpose and SetLength events of @p score within the span are applied at step
     * boundaries; the mutations the rows make are appended to the score as Mutate events. Call
     * repeatedly with increasing @p endBeat -- the composer's lookahead -- or once for a whole piece.
     */
    void run(Score& score, double endBeat);

private:
    struct Row {
        Step steps[kMaxSteps];
        int length = 16;
        double divBeats = 0.25;
        RowDirection direction = RowDirection::Forward;
        int octave = 0;
        int transpose = 0;
        float mutation = 0.0f;
        float gate = 0.5f;
        bool running = false;
        double startBeat = 0.0;   ///< beat of step 0 of the current run
        int64_t step = 0;         ///< steps played since startBeat
        int pos = 0;              ///< index of the next step to play
        int dir = 1;              ///< pendulum direction
        int mutations = 0;
        bool transposer = false;  ///< row.mode = Transposer
        Rng rng;
    };
    double nextStepBeat(const Row& r) const { return r.startBeat + static_cast<double>(r.step) * r.divBeats; }
    void playStep(int index, Row& r, Score& score, std::vector<RackEvent>& log);
    void advance(Row& r, int index, double beat, std::vector<RackEvent>& log);
    void mutate(Row& r, int index, double beat, std::vector<RackEvent>& log);
    int rootNote(const Row& r) const;

    Row rows_[kRows];
    int keyRoot_ = 9;
    int scale_ = 0;
    Style style_ = Style::Cosmic;
    int shift_ = 0;           ///< the transposer's offset, applied to every note row
    std::vector<std::pair<double, int>> shiftLog_;
    double position_ = 0.0;   ///< beat up to which the rack has run
};

} // namespace eph
