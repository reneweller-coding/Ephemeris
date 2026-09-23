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
 * in beats and jumps (Engine::seek) when the host does. The standalone has its own play and stop. Tempo
 * changes inside a piece belong to the score; in a host the host's tempo map is not followed yet
 * (PLAN 10), the export carries the piece's own tempo map.
 */
#pragma once
#include "eph/Engine.h"
#include "eph/SetFile.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <memory>
#include <mutex>

/** @brief One entry of the ParamStore as a host parameter. */
class StoreParameter final : public juce::RangedAudioParameter {
public:
    StoreParameter(eph::ParamStore& store, int id, const juce::String& name);
    float getValue() const override;
    void setValue(float newValue) override;
    float getDefaultValue() const override;
    juce::String getName(int maximumStringLength) const override;
    juce::String getLabel() const override;
    int getNumSteps() const override;
    bool isDiscrete() const override;
    bool isBoolean() const override;
    juce::String getText(float normalisedValue, int maximumStringLength) const override;
    float getValueForText(const juce::String& text) const override;
    const juce::NormalisableRange<float>& getNormalisableRange() const override { return range_; }
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
    EphemerisProcessor();
    ~EphemerisProcessor() override;

    // Composing and curating.
    void compose();                                  ///< compose with the current settings, seed and rerolls
    void newSeed();                                  ///< a fresh seed, no rerolls, then compose
    void reroll(const juce::String& unit);           ///< draw one unit again, then compose
    bool isComposing() const { return composing_.load(); }
    uint64_t seed() const { return seed_; }
    double concertMinutes() const;                   ///< compose.concert_minutes
    juce::String curationText() const;               ///< the rerolls, for the panel

    // Transport (the standalone's; a host drives its own).
    void setPlaying(bool on) { playing_ = on; }
    bool isPlaying() const { return playing_.load(); }
    void seekTo(double beat) { seekRequest_ = beat; }
    double positionBeats() const { return position_.load(); }
    /** @brief A copy of the markers and the length, for the arrange view. */
    void arrangement(std::vector<eph::Marker>& markers, double& lengthBeats, double& seconds) const;

    // Files.
    bool saveSet(const juce::File& file);
    bool loadSet(const juce::File& file);
    /** @brief Renders the current score offline to WAV and MIDI beside each other, on a thread; returns at once. */
    void exportTo(const juce::File& wav);
    juce::String status() const;                     ///< one line for the panel

    eph::ParamStore& store() { return engine_.params(); }
    StoreParameter* parameter(int id) { return id >= 0 && id < static_cast<int>(params_.size()) ? params_[static_cast<size_t>(id)] : nullptr; }

    // juce::AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Set"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    void run() override;          // the composer thread
    void timerCallback() override;   // loads a finished score on the message thread
    eph::Score composeNow();

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
};
