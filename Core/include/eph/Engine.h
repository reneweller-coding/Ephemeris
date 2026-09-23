/**
 * @file Engine.h
 * @brief The engine: plays a score through the voices, the space and the master.
 *
 * Phases 1 and 2 (PLAN 12): the rows of the rack and the lead on their modular voices, the gestures of the score on the
 * voices' and the echo's knobs, a mixer with pan and echo send, the tape echo, the master level.
 *
 * **Time** is the sample counter; beats are derived from it through the tempo map (Clock.h), never
 * accumulated, so a three-hour concert does not drift.
 *
 * **Determinism across block sizes.** Parameters and gestures are read on an absolute raster of 32
 * samples, and spans are split exactly at note events, so a host's block size cannot change a single
 * sample (the self test renders with blocks of 1, 37 and 512 and compares bits). The rule and the
 * raster are Phosphene's.
 *
 * The composer thread and its lock-free event ring come with the composer (Phase 4); until then the
 * score is handed over whole with load(), not from the audio thread.
 */
#pragma once
#include "eph/Atmos.h"
#include "eph/Dynamics.h"
#include "eph/ModVoice.h"
#include "eph/Params.h"
#include "eph/Reverb.h"
#include "eph/Score.h"
#include "eph/Spring.h"
#include "eph/StringMachine.h"
#include "eph/TapeEcho.h"
#include "eph/TapeKeys.h"
#include <cstdint>
#include <vector>

namespace eph {

/** @brief Plays a score. Not thread-safe except for parameter writes (ParamStore is atomic). */
class Engine {
public:
    /** @brief The parameters; any thread may write them. */
    ParamStore& params() { return params_; }
    /** @brief The parameters, read-only. */
    const ParamStore& params() const { return params_; }

    /** @brief Sets the sample rate and the largest block; call before load(). */
    void prepare(double sampleRate, int maxBlock);
    /** @brief Replaces the score (not from the audio thread) and returns to its start. */
    void load(const Score& score);

    /**
     * @brief Renders @p n samples into @p L and @p R (overwritten) and advances the position.
     * @return false once the position has passed the end of the score (the echo still rings)
     */
    bool process(float* L, float* R, int n);

    /** @brief The score being played. */
    const Score& score() const { return score_; }
    /** @brief Current position in seconds. */
    double seconds() const { return static_cast<double>(sample_) / sampleRate_; }
    /** @brief Current position in beats. */
    double beat() const { return score_.tempo.beatAt(seconds()); }
    /** @brief Length of the score in seconds. */
    double lengthSeconds() const { return score_.tempo.secondsAt(score_.lengthBeats); }
    /** @brief Sample rate set by prepare(). */
    double sampleRate() const { return sampleRate_; }
    /**
     * @brief The value of parameter @p id as the engine plays it: the knob plus the score's gesture
     *        offset at the current cell, in real units.
     */
    float played(int id) const;

private:
    /** @brief A note event on the sample grid. */
    struct Ev {
        int64_t sample;
        uint8_t on;       ///< 0 off, 1 on (offs first at equal samples)
        uint8_t row;
        bool accent;
        bool legato;
        int pitch;
        float velocity;
        int id;           ///< pairs an off with its on
    };
    /** @brief The gestures on one parameter, in time order, with a cursor. */
    struct Track {
        int param;
        std::vector<Gesture> gestures;
        size_t cursor = 0;   ///< index of the latest gesture that has started, or gestures.size()
        float offset = 0.0f;
    };
    void updateCell();
    void renderSpan(float* L, float* R, int n);
    void setLeadLike(Module m, int voiceIndex);

    ParamStore params_;
    Score score_;
    double sampleRate_ = 48000.0;
    int maxBlock_ = 512;
    int64_t sample_ = 0;
    std::vector<Ev> events_;
    size_t evCursor_ = 0;
    std::vector<Track> tracks_;
    std::vector<int> trackOf_;   ///< parameter id -> index into tracks_, or -1

    static constexpr int kVoices = kRows + 2;   ///< the rows' voices, then the lead and the drone
    static constexpr int kLeadVoice = kRows;
    static constexpr int kDroneVoice = kRows + 1;
    static constexpr int kTapeVoice = kRows + 2;   ///< not a ModVoice: the tape keyboard's events
    static constexpr int kStringsVoice = kRows + 3;   ///< not a ModVoice: the string machine's events
    ModVoice voices_[kVoices];
    /** Whether a voice runs in the current cell: decided at the cell's start and on a note-on, never
     *  when it falls silent mid-span -- that would tie its oscillator phase to the host's block size. */
    bool running_[kVoices] = {};
    TapeEcho echo_;
    TapeKeys tape_;
    BusCompressor comp_;
    TruePeakLimiter limiter_;
    Atmos atmos_;
    StringMachine strings_;
    Spring spring_;
    float springReturn_ = 0.0f;
    bool stringsRunning_ = false;
    float strL_ = 0.0f, strR_ = 0.0f, strEcho_ = 0.0f, strReverb_ = 0.0f;
    bool atmosRunning_ = false;
    float atmosLevel_ = 0.0f, atmosEcho_ = 0.0f, atmosReverb_ = 0.0f;
    bool tapeRunning_ = false;
    float tapeL_ = 0.0f, tapeR_ = 0.0f, tapeEcho_ = 0.0f, tapeReverb_ = 0.0f;
    Reverb reverb_;
    float rsend_[kVoices] = {};
    float reverbReturn_ = 0.0f;
    float gainL_[kVoices] = {}, gainR_[kVoices] = {}, send_[kVoices] = {};
    float echoReturn_ = 0.0f, master_ = 1.0f;
};

} // namespace eph
