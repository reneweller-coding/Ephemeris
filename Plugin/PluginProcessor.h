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
    eph::ParamStore& store_;
    int id_;
    juce::String name_;
    juce::NormalisableRange<float> range_;
};

/** @brief The Ephemeris processor. */
class EphemerisProcessor final : public juce::AudioProcessor, private juce::Thread, private juce::Timer {
public:
    EphemerisProcessor();                            ///< registers every parameter and composes a first piece
    ~EphemerisProcessor() override;                  ///< stops the composer and the exporter

    // Composing and curating.
    void compose();                                  ///< compose with the current settings, seed and rerolls
    void newSeed();                                  ///< a fresh seed, no rerolls, then compose
    void reroll(const juce::String& unit);           ///< draw one unit again, then compose
    bool isComposing() const { return composing_.load(); }   ///< whether the composer thread is at work
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
    void run() override;          // the composer thread
    void timerCallback() override;   // loads a finished score on the message thread
    eph::Score composeNow();
    /**
     * @brief @p s as the engine plays it here: in a host at the host's tempo (constant, the piece's tempo
     *        changes come only through the export and its MIDI file), in the standalone as composed.
     */
    eph::Score forPlayback(const eph::Score& s) const;
    /** @brief The performer's MIDI: keys transpose, controllers move what they are bound to (audio thread). */
    void perform(const juce::MidiBuffer& midi);
    /** @brief Sets store id @p id to the real value @p value through its host parameter. */
    void setFromMidi(int id, float value);

    eph::Engine engine_;
    std::vector<StoreParameter*> params_;
    uint64_t seed_ = 1;
    eph::Curation curation_;
    mutable std::mutex lock_;
    std::unique_ptr<eph::Score> pending_;   ///< composed, waiting to be loaded
    eph::Score current_;                    ///< what the engine plays (for reloading and the arrange view)
    std::atomic<bool> composing_{ false }, playing_{ false }, exporting_{ false };
    std::atomic<bool> again_{ false };   ///< compose was asked for while composing: once more when done
    std::atomic<double> position_{ 0.0 }, seekRequest_{ -1.0 };
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    juce::String lastExport_;
    std::unique_ptr<std::thread> exporter_;
    std::vector<float> record_;          ///< interleaved, allocated in prepareToPlay in the test mode only
    size_t recordTarget_ = 0;
    std::atomic<size_t> recordPos_{ 0 };
    bool autoPlay_ = false;
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
