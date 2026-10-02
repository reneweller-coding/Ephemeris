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
#include <atomic>
#include "eph/synth/Atmos.h"
#include "eph/synth/Drums.h"
#include "eph/fx/Dynamics.h"
#include "eph/synth/ModVoice.h"
#include "eph/NoteTap.h"
#include "eph/Params.h"
#include "eph/Cue.h"
#include "eph/fx/Reverb.h"
#include "eph/fx/Plate.h"
#include "eph/fx/Bbd.h"
#include "eph/Score.h"
#include "eph/fx/Spring.h"
#include "eph/synth/StringMachine.h"
#include "eph/synth/Poly.h"
#include "eph/fx/TapeEcho.h"
#include "eph/fx/Rooms.h"
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
    /**
     * @brief Replaces the score (not from the audio thread) and returns to its start.
     * @param sounds put the knob settings of the score's start (Score::knobs, the composer's sounds) on the knobs
     *               now; false where the knobs already hold them (the plugin decides which synths take a new sound,
     *               a restored state or a loaded set already has them). Later ones -- a concert's next pieces -- the
     *               engine puts on the knobs where they begin, on the sample.
     */
    void load(const Score& score, bool sounds = true);
    /**
     * @brief The quality level's one setting so far: the singers per choir key (TapeKeys::setSingers), 6 on
     *        the desktop, 3 on the Quest. Kept across load().
     */
    void setTapeSingers(int n) { tapeSingers_ = n; tape_.setSingers(n); }
    /**
     * @brief Live play (01.10.2026; the plugin's engine): the keyboard and the composer switch act. Off (renders, exports,
     *        the measuring engines, the default): the piece plays as it was composed.
     */
    void setLive(bool on) { live_ = on; }
    /**
     * @name A MIDI keyboard (01.10.2026, perform.keyboard_part)
     * The keys play a voice with its sound as its page has it, untransposed; perform.keyboard_mode Replace leaves that
     * voice's generated notes out (by channel: from a voice's first played key on), Layer plays over them;
     * perform.composer off leaves every generated note out. Live play only (setLive).
     * @{ */
    /**
     * @brief Queues a key for the next process() call, @p offset samples into it: it is played on that sample. The
     *        rendering thread, before process().
     * @param pitch MIDI note; @param velocity 1..127; @param channel 0..15 (by channel); @param on false: its release
     */
    void queueLive(int offset, int pitch, int velocity, int channel, bool on, int target = -1);
    /** @brief The target a key on @p channel plays when it names none (perform.keyboard_part; perform::keys). */
    int keyTargetFor(int channel) const { return keyboardTarget(channel); }
    /** @brief Releases every played key and forgets the queued ones (a stop, another keyboard target). */
    void liveAllOff();
    /** @} */

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
    /**
     * @brief The loudness corrections of the score playing (a trim per LevelMark, Leveler.h), found after it began: the
     *        plugin and the Quest measure a piece while it already plays. The difference to what played glides in over
     *        a few seconds. Writes a few numbers, allocates nothing; not while process() runs.
     */
    void setLevelTrims(const std::vector<float>& trims);
    /** @brief Counts the knob settings the engine has put on the knobs while playing or seeking (a concert's next
     *         piece): the plugin tells the host and the pages when it moves. */
    uint32_t soundsVersion() const { return soundsVersion_.load(std::memory_order_relaxed); }
    /** @brief The beat of the knob settings that hold (0: the first piece's; -1: none). */
    double soundGroup() const { return soundGroup_.load(std::memory_order_relaxed); }

    /** @brief The score being played. */
    const Score& score() const { return score_; }
    /** @brief Current position in seconds. */
    double seconds() const { return static_cast<double>(sample_) / sampleRate_; }
    /** @brief Current position in samples (the clock of NoteTap's notes). */
    int64_t samplePosition() const { return sample_; }
    /** @brief From now on every composer note it plays is also written to @p tap (null: stops; NoteTap.h, MIDI out). */
    void setNoteTap(NoteTap* tap) { noteTap_ = tap; }
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
    /**
     * @brief As played(), without its cache (01.10.2026): the panel's live rings read it on the message thread while the
     *        audio thread plays -- reading only, so the cache the audio thread keeps is never written from two threads.
     */
    float playedNow(int id) const;

    /** @brief The channel strips, in the order they are mixed: the eight rows, the lead, the drone, the tape
     *         keys, the string machine, the drums, the atmosphere. */
    static constexpr int kChannels = kRows + 7;
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
    /**
     * @brief Stems: from now on every process() call also writes, per channel, what it puts into the mix (after
     *        its fader and pan, as the meters read it) into @p left and @p right, and at index kChannels the
     *        rooms' return (tape echo, springs, hall). Each buffer holds at least the block of the call. The
     *        sum of the stems is the mix before the master (level, compressor, limiter). Null switches them off;
     *        reading only, the mix is the same to the bit.
     */
    void setStems(float* const* left, float* const* right) { stemL_ = left; stemR_ = right; }
    /** @brief The true-peak limiter on (default) or off: an archive master without a limiter (the addon's 9). */
    void setLimiter(bool on) { limiterOn_ = on; }
    /** @brief The score's cue marks (Cue.h), built by load(); the audio thread may read them between loads. */
    const std::vector<CueMark>& cueMarks() const { return cueMarks_; }

private:
    /**
     * @brief The sources, in the order they are mixed: the rows' voices, the lead and the drone (all
     *        ModVoices), then the tape keys, the string machine, the drums and the atmosphere.
     */
    enum Source : int { kSrcLead = kRows, kSrcDrone, kSrcTape, kSrcStrings, kSrcPoly, kSrcDrums, kSrcAtmos, kSources };
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
        float bright = 0.0f;   ///< the note's cutoff offset in octaves (NoteEvent::bright)
        float decay = 0.0f;    ///< the note's filter decay in octaves of its time (NoteEvent::decay)
    };
    /** @brief Hands event @p e to its source. */
    void dispatch(const Ev& e);
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
        float echo2 = 0.0f;     ///< send into the second echo (the rows)
        float lowCut = 0.0f;    ///< the strip's high pass in Hz, 0 none (the production guide's 4.2)
        Svf hpL;   ///< the low cut's state, left
        Svf hpR;   ///< ... right
        float lpHz = 0.0f;      ///< the distance's low pass in Hz, 0 none (the addon's distance macro)
        Svf lpL;   ///< the distance's low pass, left
        Svf lpR;   ///< ... right
        Svf splitLo[2];   ///< the band's lower split (300 Hz), per channel
        Svf splitHi[2];   ///< its upper split (5 kHz), per channel
        float blend = 0.0f;     ///< send into the blend room (the addon's serial far space)
        float early = 0.0f;     ///< send into the early reflections (send A)
        float shimmer = 0.0f;   ///< send into the effect hall (send D)
        Svf band[2][6];         ///< the six bands a spectral duck works in (the addon's 4)
        float punch = 0.0f;     ///< the transient shaper's amount (rows)
        float envFast = 0.0f;   ///< the transient shaper's fast follower
        float envSlow = 0.0f;   ///< ... and its slow one
    };
    /** @brief The buses of one span: the mix, the echo send, the hall send. */
    struct Buses {
        float* L;        ///< the dry mix, left (the output buffer)
        float* R;        ///< ... right
        float* echoL;    ///< into the tape echo, left
        float* echoR;    ///< ... right
        float* hallL;    ///< into the hall, left
        float* hallR;    ///< ... right
        float* echo2L;   ///< into the second echo, left
        float* echo2R;   ///< ... right
        float* rowsL;    ///< the rows' dry sum, left: it ducks the rooms' returns and the pads
        float* rowsR;    ///< ... right
        float* padsL;    ///< the pads' sum (strings, tape keys), left: it ducks the atmosphere
        float* padsR;    ///< ... right
        float* blendL;   ///< into the blend room, left
        float* blendR;   ///< ... right
        float* earlyL;   ///< into the early reflections, left
        float* earlyR;   ///< ... right
        float* shimL;    ///< into the effect hall, left
        float* shimR;    ///< ... right
    };
    /** @brief Reads the knobs and the automation for the next raster cell: the mixer, its effects, the master. */
    void updateCell();
    /**
     * @brief Renders @p n samples into @p L and @p R within one raster cell, up to the next note: the sources through their
     *        strips, the rooms, the mix bus.
     */
    void renderSpan(float* L, float* R, int n);
    /** @brief A voice's settings from a module laid out like the voice table (voice, lead, drone). */
    VoiceSettings voiceSettings(Module m, int instance, bool vibrato) const;
    /**
     * @brief A synth's modulation from its parameters (Modulation.h), as played (the composer's sounds arrive as offsets): the
     *        modulation envelope at @p env (-1: none; its times times @p envToMs), @p lfos LFOs from @p lfo, @p slots
     *        slots from @p slot; @p shortMatrix: the destinations of the smaller matrix (shortModDest).
     */
    ModSettings modSettings(Module m, int instance, int env, float envToMs, int lfo, int lfos, int slot, int slots, bool shortMatrix) const;
    /** @brief Level, equal-power pan (times @p width) and sends of strip @p s. */
    void setStrip(int s, float levelDb, float pan, float echo, float reverb, float width = 1.0f, float echo2 = 0.0f,
                  float distance = 0.0f);
    /** @brief The strip's low cut (a second-order Butterworth high pass), @p hz 0 for none. */
    void setLowCut(int s, float hz);
    /** @brief Adds source @p source's output through its strip to the buses (and the meter); @p echoSend false
     *         leaves the echo send alone. */
    void mix(int source, const float* xl, const float* xr, int n, const Buses& b, bool echoSend = true,
             const float* midGain = nullptr);

    ParamStore params_;   ///< the knobs
    Score score_;   ///< the piece it plays
    double sampleRate_ = 48000.0;   ///< the sample rate, Hz
    int maxBlock_ = 512;   ///< the largest block process() is given
    int64_t sample_ = 0;   ///< the position, samples
    bool cellDirty_ = false;   ///< read the settings at the next sample, not only at the raster (after a seek)
    size_t knobCursor_ = 0;    ///< the next of the score's knob settings to put on the knobs
    std::atomic<double> soundGroup_ { -1.0 };   ///< the beat of the knob settings that hold (the piece whose sounds these are)
    float lateDb_ = 0.0f;                        ///< setLevelTrims: what is left of the step to the new correction, dB
    /** @brief The loudness correction the score sets at @p beat, dB (its LevelMarks, gliding from one to the next). */
    float trimAt(double beat) const;
    std::atomic<uint32_t> soundsVersion_ { 0 };   ///< soundsVersion()
    /** @brief Puts the score's knob settings up to @p beat on the knobs, from the cursor on. */
    void applyKnobs(double beat);
    std::vector<int64_t> offAt_;   ///< every note's off sample, by its id (a seek chases the notes that sound on)
    NoteTap* noteTap_ = nullptr;   ///< where the composer's played notes go for MIDI out (setNoteTap), or null
    std::vector<uint8_t> tapPitch_;   ///< by note id: the pitch its on went out with (0xFF: none sounds), for its off
    /** @brief The Part a source plays (the MIDI export's channel of its notes). */
    static Part partOf(int source);
    std::vector<Ev> events_;   ///< the score's notes on the sample grid, in time order
    size_t evCursor_ = 0;   ///< index of the next event not yet played
    std::vector<Track> tracks_;   ///< the set's own automation, one track per knob
    std::vector<int> trackOf_;   ///< parameter id -> index into tracks_, or -1

    bool metering_ = false;               ///< setMetering()
    float meterPeak_[kChannels] = {};     ///< takeMeters(): peak per channel
    double meterSum_[kChannels] = {};     ///< takeMeters(): sum of squares per channel
    int meterCount_ = 0;                  ///< takeMeters(): samples gathered
    ModVoiceBank voices_;   ///< the sources below kModVoices, in lanes
    Strip strips_[kSources];   ///< a strip per source
    TapeKeys tape_;   ///< the tape keys
    StringMachine strings_;   ///< the string machine
    PolySynth poly_;                      ///< the pad synth (Part::Pad)
    DrumKit drums_;   ///< the drum machine
    Atmos atmos_;   ///< the atmosphere
    TapeEcho echo_;   ///< the tape echo
    BbdEcho bbd_;          ///< echo.type BBD instead of the tape echo, on the same send
    TapeEcho echo2_;       ///< the second echo (Module::Echo2, keys "delay.*"): its own time for the counter rows
    Plate plate_;          ///< reverb.type Plate instead of the hall, on the same send
    bool bbdOn_ = false;   ///< echo.type BBD at the current cell
    bool plateOn_ = false;   ///< reverb.type Plate at the current cell
    Spring spring_;   ///< the springs on the echo send
    Reverb reverb_;   ///< the hall
    BusCompressor comp_;   ///< the bus compressor
    TruePeakLimiter limiter_;   ///< the true-peak limiter (master.ceiling)
    float echoReturn_ = 0.0f;   ///< the echo's return level
    float springReturn_ = 0.0f;   ///< the springs' return level
    float reverbReturn_ = 0.0f;   ///< the hall's return level
    float master_ = 1.0f;   ///< master.level, linear
    float echo2Return_ = 0.0f;   ///< the second echo's return level
    // The production guide's mix bus (25.09.2026): a 20 Hz DC and subsonic filter, the side mono under 100 Hz and
    // widened above 300 Hz, a mono switch; the rooms' returns ducked by the rows.
    Svf dcL_;   ///< the 20 Hz DC and subsonic filter, left
    Svf dcR_;   ///< ... right
    Svf sideHp_;   ///< the side's high pass at 100 Hz: the side mono under it
    Svf sideHp300_;   ///< the side's high pass at 300 Hz: what the width widens
    float width_ = 1.0f;   ///< master.width, linear: the side above 300 Hz
    float energyMid_ = 0.0f;   ///< the width's guard: the mid's energy (300 ms)
    float energyLow_ = 0.0f;   ///< ... the side's under 300 Hz
    float energyHigh_ = 0.0f;   ///< ... the side's above
    float energyCoef_ = 0.0f;   ///< ... the followers' coefficient
    bool mono_ = false;   ///< master.mono: the mix in mono
    float duckEnv_ = 0.0f;   ///< the rows' envelope that ducks the rooms' returns
    float envAttack_ = 0.0f;   ///< the ducks' followers: attack coefficient (5 ms)
    float envRelease_ = 0.0f;   ///< ... release coefficient
    float echoDuckDb_ = 0.0f;   ///< how far the rows duck the echo's return, dB (echo.duck)
    float hallDuckDb_ = 0.0f;   ///< how far they duck the hall's return, dB (reverb.duck)
    // The addon (25.09.2026): the cascaded duck, the tape keys' allpass spread, the sub solo, the limiter switch.
    float cascadeDb_ = 0.0f;   ///< the cascaded duck's depth: the pads under the rows, dB
    float padEnv_ = 0.0f;   ///< the pads' envelope that ducks the atmosphere
    float tapeSpread_ = 0.0f;   ///< tape.spread: the tape keys' allpass spread
    float apL_[4] = {};   ///< the spread's allpasses: states, left
    float apR_[4] = {};   ///< ... right
    float apCoefL_[4] = {};   ///< ... coefficients, left
    float apCoefR_[4] = {};   ///< ... right
    bool subSolo_ = false;   ///< master.sub_solo: the band under 80 Hz alone
    bool limiterOn_ = true;   ///< the limiter is in
    Svf subL_;   ///< the sub solo's low pass, left
    Svf subR_;   ///< ... right
    /// The blend room (a second plate, short) and its serial feed into the hall; the soft clipper (with first-order
    /// antiderivative anti-aliasing) and the limiter under 80 Hz on the mix bus.
    Plate blend_;
    EarlyReflections early_;   ///< the early reflections (send A)
    Shimmer shimmer_;   ///< the effect hall (send D)
    float earlyReturn_ = 0.0f;   ///< the early reflections' return level
    float shimmerReturn_ = 0.0f;   ///< the effect hall's return level
    float tame_ = 0.0f;   ///< master.tame: the resonance suppressor's amount
    /// The six bands of the spectral duck and of the resonance suppressor: the rows' and the pads' side chains, the
    /// rows' bus (both sides), and their followers.
    static constexpr int kBands = 6;
    Svf rowSide_[kBands];   ///< the rows' side chain, per band
    Svf padSide_[kBands];   ///< the pads' side chain, per band
    float rowBandEnv_[kBands] = {};   ///< the rows' followers, per band
    float padBandEnv_[kBands] = {};   ///< the pads' followers, per band
    ResonanceTamer tamer_;   ///< on the rows' bus (fx/Rooms.h)
    float duckTable_[256] = {};   ///< the spectral duck's gains by amount (0 .. 1 in 256 steps)
    float blendReturn_ = 0.0f;   ///< the blend room's return level
    float blendIntoHall_ = 0.0f;   ///< how much of it feeds the hall
    float clipDb_ = 0.0f;   ///< how far the soft clipper rounds above the ceiling, dB (master.clip)
    float clipCeiling_ = 1.0f;   ///< the limiter's ceiling, linear
    float clipPrev_[2] = {};   ///< the clipper's last input per channel (its antiderivative)
    float subCeiling_ = 1.0f;   ///< the ceiling of the band under 80 Hz, linear
    float subEnv_ = 0.0f;   ///< that band's follower
    float subAttack_ = 0.0f;   ///< its attack coefficient (1 ms)
    float subRelease_ = 0.0f;   ///< its release coefficient
    float punchFastA_ = 0.0f;   ///< the transient shaper: the fast follower's attack
    float punchFastR_ = 0.0f;   ///< ... its release
    float punchSlowA_ = 0.0f;   ///< ... the slow follower's attack
    float punchSlowR_ = 0.0f;   ///< ... its release
    Svf subSplitL_[2];   ///< the band under 80 Hz: its low pass, left (two sections)
    Svf subSplitR_[2];   ///< ... right
    Svf subHpL_[2];   ///< ... the rest above it, left (two sections)
    Svf subHpR_[2];   ///< ... right
    int transpose_ = 0;   ///< perform.transpose at the current cell, for the notes that start
    // Live play and the keyboard (01.10.2026, setLive, queueLive).
    /// Live play (setLive).
    bool live_ = false;
    int keyTarget_ = 0;              ///< perform.keyboard_part (perform::keys), read at every cell in live play
    bool keyReplace_ = true;         ///< perform.keyboard_mode Replace: the played voice's generated notes are left out
    bool composerOff_ = false;       ///< perform.composer off: no generated note at all
    uint32_t keyPlayed_ = 0;         ///< by channel: the targets played since the last liveAllOff (bit = target)
    bool liveEvent_ = false;         ///< dispatch() plays a key now: never silenced
    /** @brief A key queued for the current process() call (queueLive). */
    struct LiveKey {
        int64_t at;       ///< the engine sample it plays on
        int pitch;        ///< MIDI note
        float velocity;   ///< 0..1
        int target;       ///< perform::keys (the release's is the one its key went to)
        bool on;          ///< false: a release
    };
    static constexpr int kLiveQueue = 256;   ///< keys per process() call at most
    LiveKey liveQueue_[kLiveQueue] = {};   ///< the keys queued for this process() call, in time order
    int liveCount_ = 0;   ///< how many keys are queued
    int liveCursor_ = 0;   ///< index of the next queued key not yet played
    uint8_t liveTarget_[128] = {};   ///< per key: the target it plays + 1 (0: not held)
    int keyboardTarget(int channel) const;   ///< perform.keyboard_part for a key on @p channel (perform::keys; Off: none)
    static int sourceOf(int target);          ///< the Source a keyboard target plays (-1 none)
    static int targetOf(int source);          ///< the keyboard target a Source belongs to (perform::keys; Off: none)
    bool silenced(int source) const;          ///< the composer's note-ons of @p source are left out
    void playLive(const LiveKey& k);          ///< a queued key (or its release), now
    int tapeSingers_ = kSingers;   ///< setTapeSingers()
    float* const* stemL_ = nullptr;   ///< setStems(), left
    float* const* stemR_ = nullptr;   ///< setStems(), right
    int spanAt_ = 0;                  ///< where the span being rendered starts in the block of process()
    std::vector<CueMark> cueMarks_;   ///< cueMarks()
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
        float strip[kSources][7] = {};        ///< setStrip: level, pan, echo, reverb, width, echo 2, distance
        float lowCut[kSources] = {};          ///< setLowCut
        TapeSettings tape;                    ///< TapeKeys::set
        StringSettings strings;               ///< StringMachine::set
        PolySettings poly;                    ///< PolySynth::set
        DrumSettings drums;                   ///< DrumKit::set
        AtmosSettings atmos;                  ///< Atmos::set
        EchoSettings echo;                    ///< TapeEcho::set, BbdEcho::set
        int echoType = -1;                    ///< which of the two took it
        EchoSettings echo2;                   ///< the second echo's TapeEcho::set
        float reverb[7] = {};                 ///< Reverb::set or Plate::set, and which
        float blend[5] = {};                  ///< the blend room's Plate::set
        float early[3] = {};                  ///< EarlyReflections::set
        float shimmer[4] = {};                ///< Shimmer::set
        float spring[2] = {};                 ///< Spring::set
        float compress = 0.0f;   ///< BusCompressor::set
        float ceiling = 0.0f;   ///< TruePeakLimiter::set
    } cache_;   ///< what the setters were last called with
    /** @brief played()'s last result per parameter: knob, offset, value (the curve's log and exp saved). */
    mutable std::vector<std::array<float, 3>> playedCache_;
};

} // namespace eph
