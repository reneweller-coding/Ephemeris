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
 */
#pragma once
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
    /** @brief A copy of the markers and the length, for the arrange view. */
    void arrangement(std::vector<eph::Marker>& markers, double& lengthBeats, double& seconds) const;

    // Files.
    bool saveSet(const juce::File& file);            ///< writes seed, lengths, rerolls and parameters as an .ephset
    bool loadSet(const juce::File& file);            ///< reads an .ephset and composes it
    /** @brief Renders the current score offline to WAV and MIDI beside each other, on a thread; returns at once. */
    void exportTo(const juce::File& wav);
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

    eph::ParamStore& store() { return engine_.params(); }   ///< the engine's parameters
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
    bool acceptsMidi() const override { return false; }   ///< no MIDI in
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
    std::atomic<double> hostBpm_{ 0.0 };     ///< the host's tempo as the audio thread last saw it, 0 outside a host
    std::atomic<double> playedBpm_{ 0.0 };   ///< the tempo the engine's score was loaded with (forPlayback), 0 as composed
};
