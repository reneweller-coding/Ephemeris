/**
 * @file Params.h
 * @brief The parameter system: descriptor tables per module, instantiated in blocks.
 *
 * A module declares its parameters once as a table of descriptors and may exist several times --
 * the eight rows of the sequencer rack are one module in eight instances. A parameter's id is the
 * base index of its module instance plus its index in the table; its text key is "<prefix>.<key>"
 * or "<prefix><instance>.<key>" ("compose.bpm", "row3.length").
 *
 * From the same tables come the host parameters, OSC addresses, the preset text form, the manual and
 * the `--list` output of eph_render. The composer never writes parameters; it reads a snapshot, and
 * what it plays on a knob it writes into the score as a gesture (Score.h).
 *
 * Values are stored as std::atomic<float> in their real range (Hz, ms, dB), so the audio thread can
 * read what another thread wrote without a lock.
 *
 * @note The store (ParamStore) is copied from Phosphene `Core/include/phos/Params.h` at 9a2f615
 *       (24.09.2026); the module tables are Ephemeris's own. Rule kept from Phosphene: a table is only
 *       ever appended to, never reordered, because the indices sit in presets, `.ephset` files and the
 *       plugin's state.
 */
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eph {

/** @brief How a parameter maps between its real value and the normalised 0..1 of a knob. */
enum class Curve : uint8_t {
    Linear,   ///< proportional
    Log,      ///< logarithmic; minimum must be > 0
    Int,      ///< integer steps, linear
    Choice,   ///< integer index into a list of names
    Toggle,   ///< 0 or 1
};

/** @brief Static description of one parameter. */
struct ParamDesc {
    const char* key;                    ///< identifier inside the module, snake_case
    const char* name;                   ///< display name (English)
    const char* unit;                   ///< unit for display, may be empty
    float minValue;                     ///< lowest real value
    float maxValue;                     ///< highest real value
    float defValue;                     ///< default real value
    Curve curve;                        ///< mapping to the knob
    const char* const* choices = nullptr;   ///< names for Curve::Choice (maxValue + 1 entries)
};

/** @brief The modules that own parameters. Appended to, never reordered. */
enum class Module : int { Compose = 0, Row, Master,
                          /** Phase 1: the modular voice of each row, and the tape echo. */
                          Voice, Echo,
                          Count };

constexpr int kRows = 8;   ///< instances of the row module (the rows of the rack) and of the voice module

/** @brief Parameters of the composer (read as a snapshot when a piece is planned). */
namespace compose {
enum : int { Bpm, Key, Scale, Style, PieceMinutes, Count };
}
/** @brief Parameters of one row of the sequencer rack (module Row, "row1" .. "row8"). */
namespace row {
enum : int { Active, Length, Division, Direction, Octave, Transpose, Mutation, Gate, Level, Pan,
             // Phase 1: the send into the tape echo.
             EchoSend, Count };
}
/** @brief Parameters of the master section. */
namespace master {
enum : int { Level, Count };
}
/**
 * @brief Parameters of one modular voice (module Voice, "voice1" .. "voice8", one per row; PLAN 5.2).
 *
 * Cutoff, resonance, filter decay and the envelope amount are the knobs a player's hand moves; the
 * gestures of the score (Score.h) are offsets on exactly these.
 */
namespace voice {
enum : int { Wave, Detune, PulseWidth, Drift, Drive, Cutoff, Resonance, EnvAmount, Decay, KeyTrack,
             Accent, AmpDecay, Glide, Count };
}
/** @brief Parameters of the tape echo (module Echo; PLAN 5.8). */
namespace echo {
enum : int { Time, Feedback, Tone, Wow, Flutter, Drive, PingPong, Return, Count };
}
/** @brief The echo times, in the order of echo.time. */
enum class EchoTime : int { Sixteenth = 0, Eighth, EighthD, Quarter, QuarterD, Half, Count };
/** @brief Length of an echo time in beats. */
double echoTimeBeats(EchoTime t);
extern const char* const kEchoTimeNames[];      ///< names of echo.time

/** @brief The five style profiles of PLAN 2.8, in the order of compose.style. */
enum class Style : int { Cosmic = 0, Doom, Melodic, Modern, Drift, Count };
/** @brief The step divisions a row can run on, in the order of row.division. */
enum class RowDivision : int { Quarter = 0, Eighth, EighthT, Sixteenth, SixteenthT, ThirtySecond, Count };
/** @brief Length of a row division in quarter-note beats. */
double rowDivisionBeats(RowDivision d);
/** @brief How a row walks its steps, in the order of row.direction. */
enum class RowDirection : int { Forward = 0, Backward, Pendulum, RandomWalk, Count };

extern const char* const kKeyNames[12];         ///< names of compose.key, C .. B
extern const char* const kScaleNames[];         ///< names of compose.scale
extern const char* const kStyleNames[];         ///< names of compose.style
extern const char* const kRowDivisionNames[];   ///< names of row.division
extern const char* const kRowDirectionNames[];  ///< names of row.direction

/**
 * @brief All parameter values of one engine, lock-free readable from the audio thread.
 *
 * Construction builds the registry from the module tables. Not copyable (atomics); use
 * copyValuesFrom() for snapshots.
 */
class ParamStore {
public:
    ParamStore();
    ParamStore(const ParamStore&) = delete;
    ParamStore& operator=(const ParamStore&) = delete;

    /** @brief Number of parameters. */
    int count() const { return static_cast<int>(entries_.size()); }
    /** @brief First id of a module instance; -1 if it does not exist. */
    int base(Module m, int instance = 0) const;
    /** @brief Id of parameter @p index of a module instance; -1 if it does not exist. */
    int id(Module m, int instance, int index) const { const int b = base(m, instance); return b < 0 ? -1 : b + index; }
    /** @brief Descriptor of @p id. */
    const ParamDesc& desc(int id) const { return *entries_[static_cast<size_t>(id)].desc; }
    /** @brief Full text key of @p id ("row3.length"). */
    const std::string& key(int id) const { return entries_[static_cast<size_t>(id)].key; }
    /** @brief Id for a text key, or -1. */
    int find(std::string_view key) const;

    /** @brief Current real value. */
    float get(int id) const { return values_[static_cast<size_t>(id)].load(std::memory_order_relaxed); }
    /** @brief Current value rounded to an integer (Int, Choice, Toggle). */
    int getInt(int id) const;
    /** @brief Current value as a switch. */
    bool getBool(int id) const { return get(id) >= 0.5f; }
    /** @brief Sets a real value, clamped to the range (and rounded for discrete curves). */
    void set(int id, float value);
    /** @brief Sets from a normalised 0..1 position. */
    void setNormalised(int id, float norm) { set(id, fromNormalised(id, norm)); }

    /** @brief Real value to normalised 0..1. */
    float toNormalised(int id, float value) const;
    /** @brief Normalised 0..1 to real value. */
    float fromNormalised(int id, float norm) const;

    /** @brief All parameters back to their defaults. */
    void resetDefaults();
    /** @brief Default of @p id: the descriptor's, or the instance's own where instances differ (the rows). */
    float defaultValue(int id) const { return defaults_[static_cast<size_t>(id)]; }
    /** @brief Copies every value from another store (for snapshots on another thread). */
    void copyValuesFrom(const ParamStore& other);
    /**
     * @brief Copies the current values of one module instance into @p out, indexed like its table.
     * @param m        module
     * @param instance instance index
     * @param out      at least as many floats as the module has parameters
     */
    void readModule(Module m, int instance, float* out) const;
    /** @brief Number of parameters of a module. */
    static int moduleCount(Module m);

    /**
     * @brief Applies "key=value" assignments separated by whitespace, newlines or ';'.
     *
     * Choice parameters accept their name ("compose.key=F#") or index. Lines starting with '#' are
     * comments.
     * @param text  the assignments
     * @param error receives a message for the first bad assignment, may be null
     * @return false if any assignment failed (the good ones are still applied)
     */
    bool parseText(std::string_view text, std::string* error = nullptr);
    /**
     * @brief The text form, one "key=value" per line.
     * @param onlyChanged leave out parameters at their default
     */
    std::string toText(bool onlyChanged) const;
    /** @brief A value formatted for display ("330 Hz", "F#"). */
    std::string format(int id) const;

private:
    struct Entry {
        const ParamDesc* desc;
        std::string key;
        Module module;
        int instance;
    };
    std::vector<Entry> entries_;
    std::unique_ptr<std::atomic<float>[]> values_;
    std::vector<float> defaults_;
    std::unordered_map<std::string, int> index_;
    static constexpr int kMaxInstances = 16;
    int bases_[static_cast<int>(Module::Count)][kMaxInstances] = {};
};

} // namespace eph
