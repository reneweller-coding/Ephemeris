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
    ParamStore& params() { return m_params; }
    /** @brief The parameters, read-only. */
    const ParamStore& params() const { return m_params; }

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
    void setTapeSingers(int n) { m_tapeSingers = n; m_tape.setSingers(n); }
    /**
     * @brief Live play (01.10.2026; the plugin's engine): the keyboard and the composer switch act. Off (renders, exports,
     *        the measuring engines, the default): the piece plays as it was composed.
     */
    void setLive(bool on) { m_live = on; }
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
    uint32_t soundsVersion() const { return m_soundsVersion.load(std::memory_order_relaxed); }
    /** @brief The beat of the knob settings that hold (0: the first piece's; -1: none). */
    double soundGroup() const { return m_soundGroup.load(std::memory_order_relaxed); }

    /** @brief The score being played. */
    const Score& score() const { return m_score; }
    /** @brief Current position in seconds. */
    double seconds() const { return static_cast<double>(m_sample) / m_sampleRate; }
    /** @brief Current position in samples (the clock of NoteTap's notes). */
    int64_t samplePosition() const { return m_sample; }
    /** @brief From now on every composer note it plays is also written to @p tap (null: stops; NoteTap.h, MIDI out). */
    void setNoteTap(NoteTap* tap) { m_noteTap = tap; }
    /**
     * @brief The family jam (02.10.2026, the plugin's Jam.h), live only: the composer's notes @p transpose semitones
     *        further (on top of perform.transpose, from the next note on), the leader's @p energy (0..1; negative: none)
     *        on the rows' filters while the performer's hand rests, and with @p rhythmOut the drums out for its break.
     */
    void setJam(int transpose, float energy, bool rhythmOut) { m_jamTranspose = transpose; m_jamEnergy = energy; m_jamDrumsOut = rhythmOut; }
    /** @brief Current position in beats. */
    double beat() const { return m_score.m_tempo.beatAt(seconds()); }
    /** @brief Length of the score in seconds. */
    double lengthSeconds() const { return m_score.m_tempo.secondsAt(m_score.m_lengthBeats); }
    /** @brief Sample rate set by prepare(). */
    double sampleRate() const { return m_sampleRate; }
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
    static constexpr int Channels = Rows + 7;
    /** @brief Name of channel @p c, for a display. */
    static const char* channelName(int c);
    /**
     * @brief Gathers each channel's peak and mean square from now on: what the channel puts into the mix,
     *        after its fader and pan. Off unless switched on, and reading only: the mix is the same to the bit.
     */
    void setMetering(bool on) { m_metering = on; }
    /**
     * @brief Hands over and clears what was gathered since the last call.
     * @param peak  Channels peaks (linear, the louder side)
     * @param sumSq Channels sums of squares (the mean of both sides per sample)
     * @return the number of samples gathered
     */
    int takeMeters(float* peak, double* sumSq);
    /**
     * @brief Stems: from now on every process() call also writes, per channel, what it puts into the mix (after
     *        its fader and pan, as the meters read it) into @p left and @p right, and at index Channels the
     *        rooms' return (tape echo, springs, hall). Each buffer holds at least the block of the call. The
     *        sum of the stems is the mix before the master (level, compressor, limiter). Null switches them off;
     *        reading only, the mix is the same to the bit.
     */
    void setStems(float* const* left, float* const* right) { m_stemL = left; m_stemR = right; }
    /** @brief The true-peak limiter on (default) or off: an archive master without a limiter (the addon's 9). */
    void setLimiter(bool on) { m_limiterOn = on; }
    /** @brief The score's cue marks (Cue.h), built by load(); the audio thread may read them between loads. */
    const std::vector<CueMark>& cueMarks() const { return m_cueMarks; }

private:
    /**
     * @brief The sources, in the order they are mixed: the rows' voices, the lead and the drone (all
     *        ModVoices), then the tape keys, the string machine, the drums and the atmosphere.
     */
    enum SourceE : int { SRC_LEAD = Rows, SRC_DRONE, SRC_TAPE, SRC_STRINGS, SRC_POLY, SRC_DRUMS, SRC_ATMOS, SOURCES };
    static constexpr int ModVoices = SRC_TAPE;   ///< sources that are a ModVoice

    /** @brief A note event on the sample grid. */
    struct Ev {
        int64_t m_sample;   ///< when
        uint8_t m_on;       ///< 0 off, 1 on (offs first at equal samples)
        uint8_t m_source;   ///< the Source it goes to
        bool m_accent;      ///< accented step
        bool m_legato;      ///< the note before slides into this one
        int m_pitch;        ///< MIDI note (the drums: a General MIDI instrument)
        float m_velocity;   ///< 0..1
        int m_id;           ///< pairs an off with its on
        float m_bright = 0.0f;   ///< the note's cutoff offset in octaves (NoteEvent::bright)
        float m_decay = 0.0f;    ///< the note's filter decay in octaves of its time (NoteEvent::decay)
    };
    /** @brief Hands event @p e to its source. */
    void dispatch(const Ev& e);
    /** @brief The gestures on one parameter, in time order, with a cursor. */
    struct Track {
        int m_param;                     ///< the knob
        std::vector<Gesture> m_gestures; ///< its gestures
        size_t m_cursor = 0;             ///< index of the latest gesture that has started, or gestures.size()
        float m_offset = 0.0f;           ///< the offset at the current cell
    };
    /** @brief A channel strip: where a source's signal goes, and whether the source runs at all. */
    struct Strip {
        bool m_running = false;   ///< decided at a cell's start and on a note-on (see renderSpan)
        float m_gainL = 0.0f;     ///< level into the left channel (pan law included)
        float m_gainR = 0.0f;     ///< level into the right channel
        float m_echo = 0.0f;      ///< send into the tape echo (and its springs)
        float m_reverb = 0.0f;    ///< send into the hall
        float m_echo2 = 0.0f;     ///< send into the second echo (the rows)
        float m_lowCut = 0.0f;    ///< the strip's high pass in Hz, 0 none (the production guide's 4.2)
        Svf m_hpL;   ///< the low cut's state, left
        Svf m_hpR;   ///< ... right
        float m_lpHz = 0.0f;      ///< the distance's low pass in Hz, 0 none (the addon's distance macro)
        Svf m_lpL;   ///< the distance's low pass, left
        Svf m_lpR;   ///< ... right
        Svf m_splitLo[2];   ///< the band's lower split (300 Hz), per channel
        Svf m_splitHi[2];   ///< its upper split (5 kHz), per channel
        float m_blend = 0.0f;     ///< send into the blend room (the addon's serial far space)
        float m_early = 0.0f;     ///< send into the early reflections (send A)
        float m_shimmer = 0.0f;   ///< send into the effect hall (send D)
        Svf m_band[2][6];         ///< the six bands a spectral duck works in (the addon's 4)
        float m_punch = 0.0f;     ///< the transient shaper's amount (rows)
        float m_envFast = 0.0f;   ///< the transient shaper's fast follower
        float m_envSlow = 0.0f;   ///< ... and its slow one
    };
    /** @brief The buses of one span: the mix, the echo send, the hall send. */
    struct Buses {
        float* m_L;        ///< the dry mix, left (the output buffer)
        float* m_R;        ///< ... right
        float* m_echoL;    ///< into the tape echo, left
        float* m_echoR;    ///< ... right
        float* m_hallL;    ///< into the hall, left
        float* m_hallR;    ///< ... right
        float* m_echo2L;   ///< into the second echo, left
        float* m_echo2R;   ///< ... right
        float* m_rowsL;    ///< the rows' dry sum, left: it ducks the rooms' returns and the pads
        float* m_rowsR;    ///< ... right
        float* m_padsL;    ///< the pads' sum (strings, tape keys), left: it ducks the atmosphere
        float* m_padsR;    ///< ... right
        float* m_blendL;   ///< into the blend room, left
        float* m_blendR;   ///< ... right
        float* m_earlyL;   ///< into the early reflections, left
        float* m_earlyR;   ///< ... right
        float* m_shimL;    ///< into the effect hall, left
        float* m_shimR;    ///< ... right
    };
    /** @brief Reads the knobs and the automation for the next raster cell: the mixer, its effects, the master. */
    void updateCell();
    /**
     * @brief Renders @p n samples into @p L and @p R within one raster cell, up to the next note: the sources through their
     *        strips, the rooms, the mix bus.
     */
    void renderSpan(float* L, float* R, int n);
    /** @brief A voice's settings from a module laid out like the voice table (voice, lead, drone). */
    VoiceSettings voiceSettings(ModuleE m, int instance, bool vibrato) const;
    /**
     * @brief A synth's modulation from its parameters (Modulation.h), as played (the composer's sounds arrive as offsets): the
     *        modulation envelope at @p env (-1: none; its times times @p envToMs), @p lfos LFOs from @p lfo, @p slots
     *        slots from @p slot; @p shortMatrix: the destinations of the smaller matrix (shortModDest).
     */
    ModSettings modSettings(ModuleE m, int instance, int env, float envToMs, int lfo, int lfos, int slot, int slots, bool shortMatrix) const;
    /** @brief Level, equal-power pan (times @p width) and sends of strip @p s. */
    void setStrip(int s, float levelDb, float pan, float echo, float reverb, float width = 1.0f, float echo2 = 0.0f,
                  float distance = 0.0f);
    /** @brief The strip's low cut (a second-order Butterworth high pass), @p hz 0 for none. */
    void setLowCut(int s, float hz);
    /** @brief Adds source @p source's output through its strip to the buses (and the meter); @p echoSend false
     *         leaves the echo send alone. */
    void mix(int source, const float* xl, const float* xr, int n, const Buses& b, bool echoSend = true,
             const float* midGain = nullptr);

    ParamStore m_params;   ///< the knobs
    Score m_score;   ///< the piece it plays
    double m_sampleRate = 48000.0;   ///< the sample rate, Hz
    int m_maxBlock = 512;   ///< the largest block process() is given
    int64_t m_sample = 0;   ///< the position, samples
    bool m_cellDirty = false;   ///< read the settings at the next sample, not only at the raster (after a seek)
    size_t m_knobCursor = 0;    ///< the next of the score's knob settings to put on the knobs
    std::atomic<double> m_soundGroup { -1.0 };   ///< the beat of the knob settings that hold (the piece whose sounds these are)
    float m_lateDb = 0.0f;                        ///< setLevelTrims: what is left of the step to the new correction, dB
    /** @brief The loudness correction the score sets at @p beat, dB (its LevelMarks, gliding from one to the next). */
    float trimAt(double beat) const;
    std::atomic<uint32_t> m_soundsVersion { 0 };   ///< soundsVersion()
    /** @brief Puts the score's knob settings up to @p beat on the knobs, from the cursor on. */
    void applyKnobs(double beat);
    std::vector<int64_t> m_offAt;   ///< every note's off sample, by its id (a seek chases the notes that sound on)
    NoteTap* m_noteTap = nullptr;   ///< where the composer's played notes go for MIDI out (setNoteTap), or null
    std::vector<uint8_t> m_tapPitch;   ///< by note id: the pitch its on went out with (0xFF: none sounds), for its off
    /** @brief The Part a source plays (the MIDI export's channel of its notes). */
    static PartE partOf(int source);
    std::vector<Ev> m_events;   ///< the score's notes on the sample grid, in time order
    size_t m_evCursor = 0;   ///< index of the next event not yet played
    std::vector<Track> m_tracks;   ///< the set's own automation, one track per knob
    std::vector<int> m_trackOf;   ///< parameter id -> index into m_tracks, or -1

    bool m_metering = false;               ///< setMetering()
    float m_meterPeak[Channels] = {};     ///< takeMeters(): peak per channel
    double m_meterSum[Channels] = {};     ///< takeMeters(): sum of squares per channel
    int m_meterCount = 0;                  ///< takeMeters(): samples gathered
    ModVoiceBank m_voices;   ///< the sources below ModVoices, in lanes
    Strip m_strips[SOURCES];   ///< a strip per source
    TapeKeys m_tape;   ///< the tape keys
    StringMachine m_strings;   ///< the string machine
    PolySynth m_poly;                      ///< the pad synth (Part::Pad)
    DrumKit m_drums;   ///< the drum machine
    Atmos m_atmos;   ///< the atmosphere
    TapeEcho m_echo;   ///< the tape echo
    BbdEcho m_bbd;          ///< echo.type BBD instead of the tape echo, on the same send
    TapeEcho m_echo2;       ///< the second echo (Module::Echo2, keys "delay.*"): its own time for the counter rows
    Plate m_plate;          ///< reverb.type Plate instead of the hall, on the same send
    bool m_bbdOn = false;   ///< echo.type BBD at the current cell
    bool m_plateOn = false;   ///< reverb.type Plate at the current cell
    Spring m_spring;   ///< the springs on the echo send
    Reverb m_reverb;   ///< the hall
    BusCompressor m_comp;   ///< the bus compressor
    TruePeakLimiter m_limiter;   ///< the true-peak limiter (master.ceiling)
    float m_echoReturn = 0.0f;   ///< the echo's return level
    float m_springReturn = 0.0f;   ///< the springs' return level
    float m_reverbReturn = 0.0f;   ///< the hall's return level
    float m_master = 1.0f;   ///< master.level, linear
    float m_echo2Return = 0.0f;   ///< the second echo's return level
    // The production guide's mix bus (25.09.2026): a 20 Hz DC and subsonic filter, the side mono under 100 Hz and
    // widened above 300 Hz, a mono switch; the rooms' returns ducked by the rows.
    Svf m_dcL;   ///< the 20 Hz DC and subsonic filter, left
    Svf m_dcR;   ///< ... right
    Svf m_sideHp;   ///< the side's high pass at 100 Hz: the side mono under it
    Svf m_sideHp300;   ///< the side's high pass at 300 Hz: what the width widens
    float m_width = 1.0f;   ///< master.width, linear: the side above 300 Hz
    float m_energyMid = 0.0f;   ///< the width's guard: the mid's energy (300 ms)
    float m_energyLow = 0.0f;   ///< ... the side's under 300 Hz
    float m_energyHigh = 0.0f;   ///< ... the side's above
    float m_energyCoef = 0.0f;   ///< ... the followers' coefficient
    bool m_mono = false;   ///< master.mono: the mix in mono
    float m_duckEnv = 0.0f;   ///< the rows' envelope that ducks the rooms' returns
    float m_envAttack = 0.0f;   ///< the ducks' followers: attack coefficient (5 ms)
    float m_envRelease = 0.0f;   ///< ... release coefficient
    float m_echoDuckDb = 0.0f;   ///< how far the rows duck the echo's return, dB (echo.duck)
    float m_hallDuckDb = 0.0f;   ///< how far they duck the hall's return, dB (reverb.duck)
    // The addon (25.09.2026): the cascaded duck, the tape keys' allpass spread, the sub solo, the limiter switch.
    float m_cascadeDb = 0.0f;   ///< the cascaded duck's depth: the pads under the rows, dB
    float m_padEnv = 0.0f;   ///< the pads' envelope that ducks the atmosphere
    float m_tapeSpread = 0.0f;   ///< tape.spread: the tape keys' allpass spread
    float m_apL[4] = {};   ///< the spread's allpasses: states, left
    float m_apR[4] = {};   ///< ... right
    float m_apCoefL[4] = {};   ///< ... coefficients, left
    float m_apCoefR[4] = {};   ///< ... right
    bool m_subSolo = false;   ///< master.sub_solo: the band under 80 Hz alone
    bool m_limiterOn = true;   ///< the limiter is in
    Svf m_subL;   ///< the sub solo's low pass, left
    Svf m_subR;   ///< ... right
    /// The blend room (a second plate, short) and its serial feed into the hall; the soft clipper (with first-order
    /// antiderivative anti-aliasing) and the limiter under 80 Hz on the mix bus.
    Plate m_blend;
    EarlyReflections m_early;   ///< the early reflections (send A)
    Shimmer m_shimmer;   ///< the effect hall (send D)
    float m_earlyReturn = 0.0f;   ///< the early reflections' return level
    float m_shimmerReturn = 0.0f;   ///< the effect hall's return level
    float m_tame = 0.0f;   ///< master.tame: the resonance suppressor's amount
    /// The six bands of the spectral duck and of the resonance suppressor: the rows' and the pads' side chains, the
    /// rows' bus (both sides), and their followers.
    static constexpr int Bands = 6;
    Svf m_rowSide[Bands];   ///< the rows' side chain, per band
    Svf m_padSide[Bands];   ///< the pads' side chain, per band
    float m_rowBandEnv[Bands] = {};   ///< the rows' followers, per band
    float m_padBandEnv[Bands] = {};   ///< the pads' followers, per band
    ResonanceTamer m_tamer;   ///< on the rows' bus (fx/Rooms.h)
    float m_duckTable[256] = {};   ///< the spectral duck's gains by amount (0 .. 1 in 256 steps)
    float m_blendReturn = 0.0f;   ///< the blend room's return level
    float m_blendIntoHall = 0.0f;   ///< how much of it feeds the hall
    float m_clipDb = 0.0f;   ///< how far the soft clipper rounds above the ceiling, dB (master.clip)
    float m_clipCeiling = 1.0f;   ///< the limiter's ceiling, linear
    float m_clipPrev[2] = {};   ///< the clipper's last input per channel (its antiderivative)
    float m_subCeiling = 1.0f;   ///< the ceiling of the band under 80 Hz, linear
    float m_subEnv = 0.0f;   ///< that band's follower
    float m_subAttack = 0.0f;   ///< its attack coefficient (1 ms)
    float m_subRelease = 0.0f;   ///< its release coefficient
    float m_punchFastA = 0.0f;   ///< the transient shaper: the fast follower's attack
    float m_punchFastR = 0.0f;   ///< ... its release
    float m_punchSlowA = 0.0f;   ///< ... the slow follower's attack
    float m_punchSlowR = 0.0f;   ///< ... its release
    Svf m_subSplitL[2];   ///< the band under 80 Hz: its low pass, left (two sections)
    Svf m_subSplitR[2];   ///< ... right
    Svf m_subHpL[2];   ///< ... the rest above it, left (two sections)
    Svf m_subHpR[2];   ///< ... right
    int m_transpose = 0;   ///< perform.transpose at the current cell, for the notes that start
    int m_jamTranspose = 0;        ///< the family jam's transposition (setJam), added to m_transpose
    float m_jamEnergy = -1.0f;     ///< the family jam's energy (setJam); negative: no leader
    float m_jamOctaves = 0.0f;     ///< the rows' filters as the jam's energy wants them, octaves, glided (updateCell)
    bool m_jamDrumsOut = false;    ///< the family jam: the drums are out for the leader's break (setJam)
    // Live play and the keyboard (01.10.2026, setLive, queueLive).
    /// Live play (setLive).
    bool m_live = false;
    int m_keyTarget = 0;              ///< perform.keyboard_part (perform::keys), read at every cell in live play
    bool m_keyReplace = true;         ///< perform.keyboard_mode Replace: the played voice's generated notes are left out
    bool m_composerOff = false;       ///< perform.composer off: no generated note at all
    uint32_t m_keyPlayed = 0;         ///< by channel: the targets played since the last liveAllOff (bit = target)
    bool m_liveEvent = false;         ///< dispatch() plays a key now: never silenced
    /** @brief A key queued for the current process() call (queueLive). */
    struct LiveKey {
        int64_t m_at;       ///< the engine sample it plays on
        int m_pitch;        ///< MIDI note
        float m_velocity;   ///< 0..1
        int m_target;       ///< perform::keys (the release's is the one its key went to)
        bool m_on;          ///< false: a release
    };
    static constexpr int LiveQueue = 256;   ///< keys per process() call at most
    LiveKey m_liveQueue[LiveQueue] = {};   ///< the keys queued for this process() call, in time order
    int m_liveCount = 0;   ///< how many keys are queued
    int m_liveCursor = 0;   ///< index of the next queued key not yet played
    uint8_t m_liveTarget[128] = {};   ///< per key: the target it plays + 1 (0: not held)
    int keyboardTarget(int channel) const;   ///< perform.keyboard_part for a key on @p channel (perform::keys; Off: none)
    static int sourceOf(int target);          ///< the Source a keyboard target plays (-1 none)
    static int targetOf(int source);          ///< the keyboard target a Source belongs to (perform::keys; Off: none)
    bool silenced(int source) const;          ///< the composer's note-ons of @p source are left out
    void playLive(const LiveKey& k);          ///< a queued key (or its release), now
    int m_tapeSingers = Singers;   ///< setTapeSingers()
    float* const* m_stemL = nullptr;   ///< setStems(), left
    float* const* m_stemR = nullptr;   ///< setStems(), right
    int m_spanAt = 0;                  ///< where the span being rendered starts in the block of process()
    std::vector<CueMark> m_cueMarks;   ///< cueMarks()
    float m_echoThrow = 0.0f;   ///< perform.throw's addition to every echo send (mix())
    /**
     * @brief What each setter was last called with (updateCell): a setter runs only when its input has
     *        changed. The setters are pure functions of their input, so this changes no sample; it saves
     *        recomputing unchanged coefficients -- the hall's eight powers, the springs', the voices'
     *        exponentials, the pan law -- 1500 times a second. Invalid after load() and seek().
     */
    struct SetCache {
        bool m_valid = false;                   ///< false: every setter runs at the next cell
        VoiceSettings m_voice[ModVoices];      ///< ModVoiceBank::set
        float m_strip[SOURCES][7] = {};        ///< setStrip: level, pan, echo, reverb, width, echo 2, distance
        float m_lowCut[SOURCES] = {};          ///< setLowCut
        TapeSettings m_tape;                    ///< TapeKeys::set
        StringSettings m_strings;               ///< StringMachine::set
        PolySettings m_poly;                    ///< PolySynth::set
        DrumSettings m_drums;                   ///< DrumKit::set
        AtmosSettings m_atmos;                  ///< Atmos::set
        EchoSettings m_echo;                    ///< TapeEcho::set, BbdEcho::set
        int m_echoType = -1;                    ///< which of the two took it
        EchoSettings m_echo2;                   ///< the second echo's TapeEcho::set
        float m_reverb[7] = {};                 ///< Reverb::set or Plate::set, and which
        float m_blend[5] = {};                  ///< the blend room's Plate::set
        float m_early[3] = {};                  ///< EarlyReflections::set
        float m_shimmer[4] = {};                ///< Shimmer::set
        float m_spring[2] = {};                 ///< Spring::set
        float m_compress = 0.0f;   ///< BusCompressor::set
        float m_ceiling = 0.0f;   ///< TruePeakLimiter::set
    } m_cache;   ///< what the setters were last called with
    /** @brief played()'s last result per parameter: knob, offset, value (the curve's log and exp saved). */
    mutable std::vector<std::array<float, 3>> m_playedCache;
};

} // namespace eph
