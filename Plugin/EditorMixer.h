/**
 * @file EditorMixer.h
 * @brief The mixer page: a channel strip per source of the engine, with a meter, a fader and the sends.
 *
 * The user (25.09.2026): "Bitte für den Mixer auch die Meter-Anzeige wie in Phosphene verwenden." A strip holds,
 * top to bottom, what a console strip holds: the source's pan (where it has one), its sends to the tape echo and
 * the hall, and the fader beside the meter. Every control is the same host parameter the source's own page
 * shows -- two views of one value, no copy.
 *
 * The meter reads what the source puts into the mix (eph::Engine::takeMeters: after fader and pan, before the
 * rooms and the master), as an RMS bar with a 300 ms release and a peak line that holds for a second and a
 * half and then falls at 20 dB a second: the body of the level and its peaks, as a console shows them.
 * @note After Phosphene `Plugin/EditorMixer.h` (24.09.2026), without the mutes Ephemeris does not have.
 */
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <utility>
#include <vector>

/** @brief One strip: name, knobs, fader and meter of one source. */
class MixerStrip final : public juce::Component, public juce::SettableTooltipClient {
public:
    /**
     * @param proc   the processor whose parameters the strip edits
     * @param name   the strip's caption
     * @param colour its accent colour
     * @param level  the fader's parameter (store id)
     * @param knobs  the knobs, top to bottom: store id and the name under the knob
     */
    MixerStrip(EphemerisProcessor& proc, const juce::String& name, juce::Colour colour, int level,
               const std::vector<std::pair<int, const char*>>& knobs);

    /** @brief A new meter reading (linear peak and RMS since the last one) and the time it covers. */
    void meter(float peak, float rms, double seconds);
    /** @brief The meter as it stands, in dB: the RMS bar and the held peak. */
    float rmsDb() const { return rmsDb_; }
    float peakDb() const { return holdDb_; }   ///< @copydoc rmsDb

    void paint(juce::Graphics&) override;   ///< the frame, the name and the meter
    void resized() override;                ///< knobs one under the other, then fader and meter side by side
    /** @brief Shows the sends (@p on) or folds them away, leaving the pan, the fader and the meter. */
    void showSends(bool on) { sends_ = on; resized(); }
    /** @brief The composer's preset for the strip's synth in this piece, under the name (empty: none). */
    void setSound(const juce::String& s) { if (s != sound_) { sound_ = s; setTooltip(s); repaint(); } }

private:
    bool sends_ = false;                                                       ///< the sends unfolded
    std::vector<bool> isSend_;                                                 ///< per knob: a send (not the pan)
    juce::String name_;                                                        ///< the source's name
    juce::String sound_;                                                       ///< the composer's preset of the moment
    juce::Colour colour_;                                                      ///< its accent colour
    std::unique_ptr<juce::Slider> fader_;                                      ///< the level fader
    std::unique_ptr<juce::SliderParameterAttachment> faderLink_;               ///< its host link
    std::vector<std::unique_ptr<juce::Slider>> knobs_;                         ///< pan and sends
    std::vector<std::unique_ptr<juce::Label>> knobNames_;                      ///< their names
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> knobLinks_;  ///< their host links
    juce::Rectangle<int> meterArea_;                                           ///< where the meter is drawn
    float rmsDb_ = -100.0f, holdDb_ = -100.0f;                                 ///< the RMS bar and the held peak, dB
    double holdAge_ = 0.0;                                                     ///< seconds the peak has been held
};

/** @brief The console: one MixerStrip per engine channel, in the mixing order, fed by a 30 Hz timer. */
class MixerConsole final : public juce::Component, private juce::Timer {
public:
    /** @brief Builds a strip per channel and starts the meter timer. */
    explicit MixerConsole(EphemerisProcessor& proc);
    ~MixerConsole() override;   ///< stops the timer

    /** @brief The strips, eph::Engine channel order. */
    int stripCount() const { return static_cast<int>(strips_.size()); }
    MixerStrip& strip(int i) { return *strips_[static_cast<size_t>(i)]; }   ///< @copydoc stripCount

    void resized() override;   ///< the strips side by side, the fold switch above them

private:
    /** @brief Takes the processor's readings and hands them to the strips. */
    void timerCallback() override;
    juce::TextButton fold_ { "Show sends" };   ///< folds the sends of every strip away or out
    bool sends_ = false;
    EphemerisProcessor& proc_;                          ///< the processor whose meters it reads
    std::vector<std::unique_ptr<MixerStrip>> strips_;   ///< one strip per channel
    std::vector<std::pair<eph::Module, int>> synths_;   ///< per strip: the synth whose presets it plays (Count: none)
    int tick_ = 0;                                      ///< timer ticks, to look at the presets now and then
    double lastPoll_ = 0.0;                             ///< when the meters were last read, seconds
};
