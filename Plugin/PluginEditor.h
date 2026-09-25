/**
 * @file PluginEditor.h
 * @brief The plugin's panel (PLAN 8.1): the concert on top, the instrument in tabs below.
 *
 * Top: style, key, scale, lengths, compose, a new seed, play; a row of rerolls (one per unit of
 * SetFile.h) and the files; the arrange view -- the sections of the piece as blocks, the playhead,
 * click to jump. Below: the mixer (EditorMixer.h: a strip per source with its meter), then a tab per
 * part of the instrument, generated from the parameter tables (the rows and the voices with a selector
 * for the instance), as in Noctuary and Phosphene, so a parameter that exists is on the panel without
 * anyone writing it there.
 *
 * `EPH_SHOT` (a PNG file) and `EPH_TAB` (a tab index) render the panel into a picture after the first piece is
 * composed and quit the standalone -- how the layout is checked without a person looking. `EPH_SHOT_AT` (a beat)
 * jumps there first; with `EPH_PLAY` set, the mixer's meters then show that place of the piece.
 */
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

/** @brief The parameters of one module instance as knobs, menus and switches. */
class ParamPage final : public juce::Component {
public:
    /**
     * @brief A page for the modules in @p groups.
     * @param p         the processor
     * @param groups    module and instance pairs, shown one after the other
     * @param instances how many instances the modules have; above one, a selector picks the instance
     */
    ParamPage(EphemerisProcessor& p, std::vector<std::pair<eph::Module, int>> groups, int instances);
    void resized() override;   ///< lays the controls out in a grid

private:
    void build();
    EphemerisProcessor& proc_;
    std::vector<std::pair<eph::Module, int>> groups_;
    int instances_;
    juce::ComboBox instance_;
    juce::OwnedArray<juce::Component> controls_;
    juce::OwnedArray<juce::Label> labels_;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliders_;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> combos_;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttons_;
};

/**
 * @brief The sections of the piece as blocks, with the playhead, and the instrumentation matrix (PLAN 8.1,
 *        Arrange): under the sections' names a lane per layer -- the rows (brighter the more of them play),
 *        the lead, the tape keys, the strings, the drone, the drums -- lit where it has notes.
 */
class ArrangeView final : public juce::Component {
public:
    explicit ArrangeView(EphemerisProcessor& p) : proc_(p) {}   ///< shows @p p's score
    void paint(juce::Graphics& g) override;                     ///< the sections, the matrix and the playhead
    void mouseDown(const juce::MouseEvent& e) override;         ///< jumps to the clicked position

private:
    static constexpr int kLanes = 6;      ///< rows, lead, tape keys, strings, drone, drums
    static constexpr int kBins = 480;     ///< columns of the matrix across the piece
    void rebuild();                       ///< the matrix of a new score
    EphemerisProcessor& proc_;
    int version_ = -1;                    ///< the score the matrix was built from
    std::vector<float> lanes_;            ///< kLanes x kBins activity, 0..1
};

/** @brief The editor. */
class EphemerisEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit EphemerisEditor(EphemerisProcessor& p);   ///< builds the panel for @p p
    ~EphemerisEditor() override;                       ///< stops the refresh timer
    void paint(juce::Graphics& g) override;            ///< the background
    void resized() override;                           ///< the top bar, the arrange view, the tabs

private:
    void timerCallback() override;
    EphemerisProcessor& proc_;
    juce::Label title_, status_, rerolls_;
    juce::ComboBox style_, key_, scale_;
    juce::Slider minutes_, concert_;
    juce::Label minutesLabel_, concertLabel_;
    juce::TextButton compose_{ "Compose" }, seed_{ "New seed" }, play_{ "Play" }, mute_{ "Mute" };
    juce::OwnedArray<juce::TextButton> rerollButtons_;
    juce::TextButton save_{ "Save set" }, load_{ "Load set" }, export_{ "Export WAV + MIDI" };
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> combos_;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliders_;
    ArrangeView arrange_;
    juce::TabbedComponent tabs_{ juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::String shotPath_;
    int shotTicks_ = 0;
};
