/**
 * @file PluginProcessor.h
 * @brief The plugin (PLAN 8.1, Phase 5): the engine, the composer on a thread of its own, transport.
 *
 * **Parameters.** Every entry of the engine's ParamStore is a host parameter (StoreParameter, after
 * Phosphene's): the store is the only place a value lives, read and written as a relaxed atomic, so a
 * knob in the plugin and the same knob in eph_render stand at the same place.
 *
 * **Composing.** "Compose" snapshots the parameters, the seed and the rerolls and hands them to the
 * composer thread; the finished score waits until the message thread loads it into the engine with
 * processing suspended -- the engine allocates when it loads, and the audio thread never does.
 *
 * **Time.** In a host the playhead is the clock: while the host plays, the engine follows its position
 * in beats and jumps (Engine::seek) when the host does, and it plays at the host's tempo: the score is
 * loaded with the host's tempo as a constant (forPlayback), and a new host tempo loads it again on the
 * message thread, from the beat the engine was at. The piece's own tempo changes (a new tempo in a
 * bridge) are the standalone's and the export's; in a DAW the song's tempo track decides. The
 * standalone has its own play and stop.
 *
 * **Performing** (PLAN 8.1). The perform module's controls act on the piece as it plays (Params.h,
 * perform). MIDI reaches them: a key transposes the rows by its distance from middle C, as the
 * transposition key of a sequencer did (it holds until the next key); controllers move the controls
 * they are bound to -- the mod wheel the filter, the expression pedal the echo throw, the sustain pedal
 * the hold -- and any controller can be learned for any of them (learn()). The bindings are part of the
 * state.
 *
 * **Cues** (PLAN 8.3, Cue.h): with cue.enabled the score's beats, sections, keys and conjunctions go out as
 * OSC over UDP to `EPH_CUE_HOST` (default this machine) at cue.port, each at the moment it is heard.
 *
 * **Mute** (after Phosphene). The output can be muted: silence at the very end of processBlock, after the
 * meters and the test recording have read the block. `EPH_MUTE=1` -- and the screenshot mode `EPH_SHOT`
 * -- start the plugin muted, and then it never unmutes itself: an automated run makes no sound.
 */
#pragma once
#include "eph/Presets.h"
#include "eph/Engine.h"
#include "eph/SetFile.h"
#include "Frame.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>

/** @brief One entry of the ParamStore as a host parameter. */
class StoreParameter final : public juce::RangedAudioParameter {
public:
    /** @brief Binds parameter @p id of @p store (which outlives this object) under the display name @p name. */
    StoreParameter(eph::ParamStore& store, int id, const juce::String& name);
    float getValue() const override;   ///< the store's value, normalised
    void setValue(float newValue) override;   ///< writes the store (relaxed atomic)
    float getDefaultValue() const override;   ///< the descriptor's default, normalised
    juce::String getName(int maximumStringLength) const override;   ///< the display name (0: no limit)
    juce::String getLabel() const override;   ///< the unit
    int getNumSteps() const override;   ///< steps of a discrete parameter
    bool isDiscrete() const override;   ///< Int, Choice and Toggle are discrete
    bool isBoolean() const override;   ///< a Toggle is boolean
    juce::String getText(float normalisedValue, int maximumStringLength) const override;   ///< the value as the panel shows it
    float getValueForText(const juce::String& text) const override;   ///< parses a choice name, On/Off or a number
    const juce::NormalisableRange<float>& getNormalisableRange() const override { return range_; }   ///< the store's own mapping
    int paramId() const { return id_; }   ///< the id in the store

private:
    eph::ParamStore& store_;   ///< the store it reads and writes
    int id_;   ///< the id in the store
    juce::String name_;   ///< the display name
    juce::NormalisableRange<float> range_;   ///< the store's mapping between the real and the normalised value
};

/** @brief The Ephemeris processor. */
class EphemerisProcessor final : public juce::AudioProcessor, private juce::Thread, private juce::Timer,
                                 private juce::AudioProcessorParameter::Listener {
public:
    EphemerisProcessor();                            ///< registers every parameter and composes a first piece
    ~EphemerisProcessor() override;                  ///< stops the composer and the exporter

    // Composing and curating.
    void compose();                                  ///< compose with the current settings, seed and rerolls
    void newSeed();                                  ///< a fresh seed, no rerolls, then compose
    void reroll(const juce::String& unit);           ///< draw one unit again, then compose
    bool isComposing() const { return composing_.load(); }   ///< whether the composer thread is at work
    /** @brief What the knobs ask Compose for: 0 a piece, 1 a concert, 2 a night set (compose.concert_minutes, night_set). */
    int chosenKind() const;
    /**
     * @brief A piece, a concert or a night set (message thread, 01.10.2026 -- as Parhelion and Totality have it): the
     *        concert's length to 0, or back to its last (60 min at first), the night switch; then composes, if what plays
     *        is not that already.
     */
    void chooseKind(int kind);
    int playingKind() const { return playingKind_.load(); }   ///< what plays: 0 a piece, 1 a concert, 2 a night set
    /** @brief Sets store id @p id to the real value @p value as a hand on the panel does (one gesture, the host told). */
    void setFromUi(int id, float value);
    uint64_t seed() const { std::lock_guard<std::mutex> g(lock_); return seed_; }   ///< the seed of what plays
    double concertMinutes() const;                   ///< compose.concert_minutes
    juce::String curationText() const;               ///< the rerolls, for the panel

    // Transport (the standalone's; a host drives its own).
    void setPlaying(bool on) { playing_ = on; }      ///< play or stop (the standalone's transport)
    bool isPlaying() const { return playing_.load(); }   ///< whether the standalone plays
    void seekTo(double beat) { seekRequest_ = beat; }    ///< jump to @p beat at the next block
    double positionBeats() const { return position_.load(); }   ///< where the audio thread is, in beats
    /** @brief Counts the scores the engine has loaded: a display compares it to know when to copy again. */
    int scoreVersion() const { return scoreVersion_.load(); }
    /** @brief A copy of the score the engine plays (message thread; for the orrery). */
    void copyScore(eph::Score& out) const { std::lock_guard<std::mutex> g(lock_); out = current_; }
    /**
     * @brief The factory preset the composer chose for instance @p instance of synth @p m where the piece now plays
     *        (Score::sounds, compose.pick_sounds): its index in eph::factoryPresets(m), -1 if none.
     */
    int composedPreset(eph::Module m, int instance) const;
    /** @brief A copy of the markers and the length, for the arrange view. */
    void arrangement(std::vector<eph::Marker>& markers, double& lengthBeats, double& seconds) const;

    // Files.
    bool saveSet(const juce::File& file);            ///< writes seed, lengths, rerolls and parameters as an .ephset
    bool loadSet(const juce::File& file);            ///< reads an .ephset and composes it
    /**
     * @brief Renders the current score offline to WAV and MIDI beside each other, on a thread; returns at once.
     *        With @p stems also a 24-bit WAV per channel strip and one for the rooms (Engine::setStems) into the
     *        folder "<name>_stems" beside them: their sum is the mix before the master.
     */
    void exportTo(const juce::File& wav, bool stems = false);
    juce::String status() const;                     ///< one line for the panel
    /**
     * @brief The test mode (EPH_SEED, EPH_PLAY = seconds, EPH_RECORD = a WAV file): a fixed seed, play at once,
     *        record what the audio thread renders; recordingDone() when the seconds are full.
     */
    bool recordingDone() const { return recordTarget_ > 0 && recordPos_.load() >= recordTarget_; }
    void writeRecording();                            ///< writes the recording (message thread)
    /**
     * @brief The channel meters since the last call (message thread): per eph::Engine channel the peak and the RMS
     *        of what it put into the mix, 0 where nothing was played.
     */
    void takeChannelMeters(float* peak, float* rms);

    // Muting.
    bool muted() const { return mute_.load(std::memory_order_relaxed); }   ///< the output is silenced
    /** @brief Mutes or unmutes; does nothing while `EPH_MUTE` forces it. */
    void setMuted(bool on) { if (!forceMute_) mute_.store(on, std::memory_order_relaxed); }
    bool muteForced() const { return forceMute_; }   ///< `EPH_MUTE` (or `EPH_SHOT`) was set: the switch is stuck on

    // Performing.
    /** @brief Binds the next MIDI controller that arrives to store id @p id; -1 cancels. */
    void learn(int id) { learn_ = id; }
    int learning() const { return learn_.load(); }   ///< the store id waiting for a controller, or -1
    /** @brief The controller bound to store id @p id, or -1. */
    int controllerFor(int id) const;
    /** @brief Unbinds store id @p id (the controls' right-click menu). */
    void forget(int id) { for (auto& c : ccMap_) if (c.load() == id) c = -1; }

    // Undo (the frame, 01.10.2026): a knob turned on the panel, a preset, a new seed, a reroll, a loaded set or a choice of
    // piece, concert or night set is a step; a step holds only what it changed, so taking it back never takes back the
    // sounds the composer wrote on the knobs since. Knobs moved from MIDI or by a host's automation are not steps.
    bool undo();                                     ///< takes the last step back; false if there was none
    bool redo();                                     ///< makes the last undone step again
    juce::String undoName() const { return history_.undoName(); }   ///< what undo takes back (empty: nothing)
    juce::String redoName() const { return history_.redoName(); }   ///< what redo makes again
    /** @brief Opens a step of several knobs (a preset, a reset): they are undone together. Close with endStep. */
    void beginStep(const juce::String& what);
    void endStep();                                  ///< closes beginStep's step
    /** @brief Store id @p id back to its default (one step). */
    void resetToDefault(int id);
    /** @brief The value store id @p id plays at the moment (the moves' offsets), normalised -- NaN where it is the knob's own. */
    float playedNormalised(int id) const;
    /**
     * @brief The headset (the frame): the hands of the Quest app in bridge mode, as OSC, while the settings do not say
     *        Off -- left pinch play and stop, both hands the next piece, right pinch holds the moves, the left hand's
     *        height the rows' filters, the right hand's the echo throw.
     */
    frame::Headset& headset() { return headset_; }

    eph::ParamStore& store() { return engine_.params(); }   ///< the engine's parameters
    /** @brief The host parameter of store id @p id, or null. */
    /** @brief Applies factory preset @p index of @p module (Presets.h) to instance @p instance, through the host's parameters. */
    void applyPreset(eph::Module module, int instance, int index);
    /** @brief Applies @p preset (a factory or a user preset) to instance @p instance of @p module, through the host. */
    void applyPresetValues(eph::Module module, int instance, const eph::SoundPreset& preset);
    /** @brief The user's presets of a synth: the text files in its folder under the application data, by name. */
    std::vector<eph::SoundPreset> userPresets(eph::Module module) const;
    /** @brief Saves the synth's knobs on @p instance as a user preset named @p name; false if it cannot be written. */
    bool saveUserPreset(eph::Module module, int instance, const juce::String& name);
    /** @brief Sets every `key=value` line of @p text (full keys, e.g. reverb.decay) through the host's parameters. */
    void applyKeyText(const juce::String& text);
    /** @brief The host parameter of store id @p id, or null. */
    StoreParameter* parameter(int id) { return id >= 0 && id < static_cast<int>(params_.size()) ? params_[static_cast<size_t>(id)] : nullptr; }

    // juce::AudioProcessor: a stereo instrument without MIDI, one program, the state as XML.
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;   ///< prepares the engine and reloads the score
    void releaseResources() override {}   ///< nothing to release
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;   ///< stereo out only
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;   ///< plays, following the host's playhead
    juce::AudioProcessorEditor* createEditor() override;   ///< the panel
    bool hasEditor() const override { return true; }   ///< it has one
    const juce::String getName() const override { return JucePlugin_Name; }   ///< "Ephemeris"
    bool acceptsMidi() const override { return true; }   ///< MIDI in: the performer's keys and controllers
    bool producesMidi() const override { return false; }   ///< no MIDI out (the export writes files)
    double getTailLengthSeconds() const override { return 8.0; }   ///< the rooms ring on
    int getNumPrograms() override { return 1; }   ///< one program
    int getCurrentProgram() override { return 0; }   ///< always the one
    void setCurrentProgram(int) override {}   ///< nothing to switch
    const juce::String getProgramName(int) override { return "Set"; }   ///< "Set"
    void changeProgramName(int, const juce::String&) override {}   ///< not renameable
    void getStateInformation(juce::MemoryBlock& destData) override;   ///< seed, rerolls and parameters as XML
    void setStateInformation(const void* data, int sizeInBytes) override;   ///< restores them and composes

private:
    /** @brief Nothing: a value alone is no step (the gestures are). */
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override;   ///< a knob on the panel: a step
    std::vector<float> values() const;               ///< every store value (undo)
    juce::String extraState() const;                 ///< seed, rerolls and the concert's length as text (undo)
    void applyExtra(const juce::String& text);       ///< the inverse; composes if seed or rerolls changed
    /** @brief Puts undo step @p s back (@p after false) or makes it again (@p after true). */
    void applyStep(const frame::UndoStep& s, bool after);
    void pollHeadset();                              ///< the hands' events, 30 times a second (message thread)
    frame::UndoHistory history_;                     ///< undo and redo (message thread)
    bool restoring_ = false;                         ///< an undo or the headset moves knobs: no step of their own
    frame::Headset headset_;                         ///< the Quest's hands (message thread)
    juce::TimedCallback headsetTick_{ [this] { pollHeadset(); } };   ///< polls the headset 30 times a second
    void run() override;   ///< the composer thread
    void timerCallback() override;   ///< loads a finished score on the message thread
    eph::Score composeNow(eph::ParamStore& snapshot);   ///< composes with the knobs as they are (copied into @p snapshot)
    /** @brief Hands a piece's loudness corrections, measured while it plays, to the engine (message thread). */
    void takeTrims();
    /**
     * @brief @p s as the engine plays it here: in a host at the host's tempo (constant, the piece's tempo
     *        changes come only through the export and its MIDI file), in the standalone as composed.
     */
    eph::Score forPlayback(const eph::Score& s) const;
    /** @brief The performer's MIDI: keys transpose, controllers move what they are bound to (audio thread). */
    void perform(const juce::MidiBuffer& midi);
    int keyboardSeen_ = 0;   ///< the keyboard target of the last block (audio thread): a change releases every key
    /** @brief Sets store id @p id to the real value @p value through its host parameter. */
    void setFromMidi(int id, float value);

    eph::Engine engine_;   ///< the engine
    std::vector<StoreParameter*> params_;   ///< the host parameters, one per store id (owned by the processor)
    uint64_t seed_ = 1;   ///< the seed of what plays
    eph::Curation curation_;   ///< the rerolls and locks
    mutable std::mutex lock_;   ///< guards what the composer thread hands over
    std::unique_ptr<eph::Score> pending_;   ///< composed, waiting to be loaded
    eph::Score current_;                    ///< what the engine plays (for reloading and the arrange view)
    std::atomic<bool> composing_{ false };   ///< the composer thread works
    std::atomic<bool> playing_{ false };   ///< play is on
    std::atomic<bool> exporting_{ false };   ///< an export runs
    std::atomic<bool> again_{ false };   ///< compose was asked for while composing: once more when done
    // The loudness (Leveler.h): measured on the composer thread once the piece is handed over, while it plays.
    std::atomic<bool> newer_{ false };       ///< a newer piece is asked for: the measuring of the last one stops
    uint64_t composed_ = 0;   ///< counts the compositions
    uint64_t pendingId_ = 0;   ///< pending_'s number (lock_)
    uint64_t playingId_ = 0;   ///< current_'s number (lock_)
    std::vector<float> trims_;               ///< the corrections found for the composition trimsFor_ (lock_)
    uint64_t trimsFor_ = 0;   ///< the composition trims_ and bal_ belong to (lock_)
    bool levelled_ = false;                  ///< current_ carries its corrections (lock_)
    int pendingKind_ = 0;                    ///< what pending_ is: 0 a piece, 1 a concert, 2 a night set (lock_)
    std::atomic<int> playingKind_{ 0 };      ///< ... and current_ (playingKind())
    float concertMinutes_ = 60.0f;           ///< the concert's length while a piece is chosen (chooseKind; the state)
    // The composer's sounds (Score::knobs, 26.09.2026): a new piece puts them on the knobs of the synths whose sound it
    // changes; after a restored state or a loaded set the knobs already hold them.
    std::atomic<bool> adoptNext_{ false };   ///< the next composition's sounds are already on the knobs
    bool pendingAdopt_ = false;              ///< ... and the one in pending_ was such a composition
    uint32_t toldSounds_ = 0;                ///< the engine's soundsVersion() the host and the pages were last told
    /** @brief Puts @p next's sounds at its start on the knobs of every synth whose sound differs from the playing score's. */
    void takeSounds(const eph::Score& next);
    /** @brief Tells the host and the pages the values of the knobs @p s sets (the engine set them). */
    void tellSounds(const eph::Score& s);
    std::atomic<double> position_{ 0.0 };   ///< where the engine is, beats (audio thread writes)
    std::atomic<double> seekRequest_{ -1.0 };   ///< a jump asked for, beats; -1 none
    double sampleRate_ = 48000.0;   ///< the sample rate, Hz
    int blockSize_ = 512;   ///< the largest block, samples
    juce::String lastExport_;   ///< the last export's result, for the Export tab
    std::unique_ptr<std::thread> exporter_;   ///< the export thread while it runs
    std::vector<float> record_;          ///< interleaved, allocated in prepareToPlay in the test mode only
    size_t recordTarget_ = 0;   ///< TOT_RECORD: how many values record_ takes, 0 off
    std::atomic<size_t> recordPos_{ 0 };   ///< how many are written
    bool autoPlay_ = false;   ///< TOT_PLAY: play once the first score is loaded
    std::array<std::atomic<float>, eph::Engine::kChannels> meterPeak_{};   ///< audio thread raises, the editor takes (exchange 0)
    std::array<std::atomic<double>, eph::Engine::kChannels> meterSum_{};   ///< sums of squares since the editor last took them
    std::atomic<int> meterCount_{ 0 };                                      ///< samples in those sums
    std::atomic<int> scoreVersion_{ 0 };     ///< scoreVersion()
    eph::CueSender cues_;                    ///< the OSC cues' socket and thread (message thread starts and stops it)
    eph::CueTap cueTap_;                     ///< audio thread: beat range -> cues
    int cuePort_ = 0;                        ///< the port the sender was started for, 0 = off (message thread)
    double lastBeat_ = -1.0;                 ///< the beat after the last block (audio thread), to see a jump
    std::array<std::atomic<int>, 128> ccMap_{};   ///< controller number -> store id, -1 unbound
    std::atomic<int> learn_{ -1 };                ///< learn()
    std::atomic<bool> mute_{ false };             ///< muted()
    bool forceMute_ = false;                      ///< muteForced()
    std::atomic<double> hostBpm_{ 0.0 };     ///< the host's tempo as the audio thread last saw it, 0 outside a host
    std::atomic<double> playedBpm_{ 0.0 };   ///< the tempo the engine's score was loaded with (forPlayback), 0 as composed
};
