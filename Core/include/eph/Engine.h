/**
 * @file Engine.h
 * @brief The engine: plays a score through the voices, the space and the master.
 *
 * Everything that sounds: the rows of the rack, the lead and the drone on modular voices, the tape keys,
 * the string machine, the drums and the atmosphere; the gestures of the score on every knob; a channel
 * strip per source with level, pan and sends; the tape echo with its springs, the hall; the master with
 * a gentle bus compressor and a true-peak limiter.
 *
 * **Time** is the sample counter; beats are derived from it through the tempo map (Clock.h), never
 * accumulated, so a three-hour concert does not drift.
 *
 * **Determinism across block sizes.** Parameters and gestures are read on an absolute raster of 32
 * samples, and spans are split exactly at note events, so a host's block size cannot change a single
 * sample (the self test renders with blocks of 1, 37 and 512 and compares bits). The rule and the
 * raster are Phosphene's.
 *
 * **Loading.** The score is handed over whole with load(), which allocates and so must not run on the
 * audio thread; the plugin loads on the message thread with processing suspended (Plugin/). A composer
 * thread feeding a lock-free event ring, as in Phosphene, is not needed while a piece is composed in
 * seconds; seek() lets a host move the playhead.
 */
#pragma once
#include <array>
#include "eph/synth/Atmos.h"
#include "eph/synth/Drums.h"
#include "eph/fx/Dynamics.h"
#include "eph/synth/ModVoice.h"
#include "eph/Params.h"
#include "eph/fx/Reverb.h"
#include "eph/Score.h"
#include "eph/fx/Spring.h"
#include "eph/synth/StringMachine.h"
#include "eph/fx/TapeEcho.h"
#include "eph/synth/TapeKeys.h"
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
    /**
     * @brief Jumps to @p beat (a host moved its playhead): every voice falls silent, the next note and
     *        gesture are found again, the settings are read at once. The rooms ring on.
     */
    void seek(double beat);

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

    /** @brief The channel strips, in the order they are mixed: the eight rows, the lead, the drone, the tape
     *         keys, the string machine, the drums, the atmosphere. */
    static constexpr int kChannels = kRows + 6;
    /** @brief Name of channel @p c, for a display. */
    static const char* channelName(int c);
    /**
     * @brief Gathers each channel's peak and mean square from now on: what the channel puts into the mix,
     *        after its fader and pan. Off unless switched on, and reading only: the mix is the same to the bit.
     */
    void setMetering(bool on) { metering_ = on; }
    /**
     * @brief Hands over and clears what was gathered since the last call.
     * @param peak  kChannels peaks (linear, the louder side)
     * @param sumSq kChannels sums of squares (the mean of both sides per sample)
     * @return the number of samples gathered
     */
    int takeMeters(float* peak, double* sumSq);

private:
    /**
     * @brief The sources, in the order they are mixed: the rows' voices, the lead and the drone (all
     *        ModVoices), then the tape keys, the string machine, the drums and the atmosphere.
     */
    enum Source : int { kSrcLead = kRows, kSrcDrone, kSrcTape, kSrcStrings, kSrcDrums, kSrcAtmos, kSources };
    static constexpr int kModVoices = kSrcTape;   ///< sources that are a ModVoice

    /** @brief A note event on the sample grid. */
    struct Ev {
        int64_t sample;   ///< when
        uint8_t on;       ///< 0 off, 1 on (offs first at equal samples)
        uint8_t source;   ///< the Source it goes to
        bool accent;      ///< accented step
        bool legato;      ///< the note before slides into this one
        int pitch;        ///< MIDI note (the drums: a General MIDI instrument)
        float velocity;   ///< 0..1
        int id;           ///< pairs an off with its on
    };
    /** @brief The gestures on one parameter, in time order, with a cursor. */
    struct Track {
        int param;                     ///< the knob
        std::vector<Gesture> gestures; ///< its gestures
        size_t cursor = 0;             ///< index of the latest gesture that has started, or gestures.size()
        float offset = 0.0f;           ///< the offset at the current cell
    };
    /** @brief A channel strip: where a source's signal goes, and whether the source runs at all. */
    struct Strip {
        bool running = false;   ///< decided at a cell's start and on a note-on (see renderSpan)
        float gainL = 0.0f;     ///< level into the left channel (pan law included)
        float gainR = 0.0f;     ///< level into the right channel
        float echo = 0.0f;      ///< send into the tape echo (and its springs)
        float reverb = 0.0f;    ///< send into the hall
    };
    /** @brief The buses of one span: the mix, the echo send, the hall send. */
    struct Buses {
        float* L; float* R;             ///< the dry mix (the output buffers)
        float* echoL; float* echoR;     ///< into the tape echo
        float* hallL; float* hallR;     ///< into the hall
    };
    void updateCell();
    void renderSpan(float* L, float* R, int n);
    /** @brief A voice's settings from a module laid out like the voice table (voice, lead, drone). */
    VoiceSettings voiceSettings(Module m, int instance, bool vibrato) const;
    /** @brief Level, equal-power pan (times @p width) and sends of strip @p s. */
    void setStrip(int s, float levelDb, float pan, float echo, float reverb, float width = 1.0f);
    /** @brief Adds source @p source's output through its strip to the buses (and the meter); @p echoSend false
     *         leaves the echo send alone. */
    void mix(int source, const float* xl, const float* xr, int n, const Buses& b, bool echoSend = true);

    ParamStore params_;
    Score score_;
    double sampleRate_ = 48000.0;
    int maxBlock_ = 512;
    int64_t sample_ = 0;
    bool cellDirty_ = false;   ///< read the settings at the next sample, not only at the raster (after a seek)
    std::vector<Ev> events_;
    size_t evCursor_ = 0;
    std::vector<Track> tracks_;
    std::vector<int> trackOf_;   ///< parameter id -> index into tracks_, or -1

    bool metering_ = false;               ///< setMetering()
    float meterPeak_[kChannels] = {};     ///< takeMeters(): peak per channel
    double meterSum_[kChannels] = {};     ///< takeMeters(): sum of squares per channel
    int meterCount_ = 0;                  ///< takeMeters(): samples gathered
    ModVoiceBank voices_;   ///< the sources below kModVoices, in lanes
    Strip strips_[kSources];
    TapeKeys tape_;
    StringMachine strings_;
    DrumKit drums_;
    Atmos atmos_;
    TapeEcho echo_;
    Spring spring_;
    Reverb reverb_;
    BusCompressor comp_;
    TruePeakLimiter limiter_;
    float echoReturn_ = 0.0f, springReturn_ = 0.0f, reverbReturn_ = 0.0f, master_ = 1.0f;
    int transpose_ = 0;   ///< perform.transpose at the current cell, for the notes that start
    float echoThrow_ = 0.0f;   ///< perform.throw's addition to every echo send (mix())
    /**
     * @brief What each setter was last called with (updateCell): a setter runs only when its input has
     *        changed. The setters are pure functions of their input, so this changes no sample; it saves
     *        recomputing unchanged coefficients -- the hall's eight powers, the springs', the voices'
     *        exponentials, the pan law -- 1500 times a second. Invalid after load() and seek().
     */
    struct SetCache {
        bool valid = false;                   ///< false: every setter runs at the next cell
        VoiceSettings voice[kModVoices];      ///< ModVoiceBank::set
        float strip[kSources][5] = {};        ///< setStrip: level, pan, echo, reverb, width
        TapeSettings tape;                    ///< TapeKeys::set
        StringSettings strings;               ///< StringMachine::set
        DrumSettings drums;                   ///< DrumKit::set
        AtmosSettings atmos;                  ///< Atmos::set
        EchoSettings echo;                    ///< TapeEcho::set
        float reverb[6] = {};                 ///< Reverb::set
        float spring[2] = {};                 ///< Spring::set
        float compress = 0.0f, ceiling = 0.0f;   ///< BusCompressor::set, TruePeakLimiter::set
    } cache_;
    /** @brief played()'s last result per parameter: knob, offset, value (the curve's log and exp saved). */
    mutable std::vector<std::array<float, 3>> playedCache_;
};

} // namespace eph
