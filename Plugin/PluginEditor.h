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
 * composed and quit the standalone -- how the layout is checked without a person looking. `EPH_SHOT_SIZE` ("1600x2400") the window's size, `EPH_SHOT_AT` (a beat)
 * jumps there first; with `EPH_PLAY` set, the mixer's meters then show that place of the piece.
 */
#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include "EditorTheme.h"
#include "UpdateCheck.h"
#include <functional>
#include <memory>
#include <vector>

/**
 * @brief One slot of a synth's modulation matrix (26.09.2026, Modulation.h): its number, a menu for the source, one
 *        for the target, and the amount as a slider that fills from its middle.
 */
class ModSlotControl final : public juce::Component {
public:
    ModSlotControl(int number, juce::Colour colour);
    void resized() override;
    void paint(juce::Graphics& g) override;
    juce::ComboBox source, target;
    juce::Slider amount;

private:
    int number_;
};

/** @brief The parameters of one module instance as knobs, menus and switches. */
class ParamPage final : public juce::Component, private juce::Timer {
public:
    /**
     * @brief A page for the modules in @p groups.
     * @param p         the processor
     * @param groups    module and instance pairs, shown one after the other
     * @param instances how many instances the modules have; above one, a selector picks the instance
     */
    ParamPage(EphemerisProcessor& p, std::vector<std::pair<eph::Module, int>> groups, int instances);
    void resized() override;   ///< lays the groups out, flowing across the page
    void paint(juce::Graphics& g) override;   ///< the group boxes and their titles
    /** @brief The height the page needs at @p width (for a page in a viewport). */
    int heightFor(int width) const;

private:
    void build();
    /** @brief Applies factory preset @p index to the page's synth (the instance shown). */
    void choosePreset(int index);
    /** @brief Fills the list: the factory presets in their groups, then the user's under "User". */
    void fillPresets();
    EphemerisProcessor& proc_;
    std::vector<std::pair<eph::Module, int>> groups_;
    int instances_;
    juce::ComboBox instance_;
    // The factory presets of the page's synth (Presets.h): a list in groups, and a step back and forth.
    eph::Module presetModule_ = eph::Module::Count;
    int presetCount_ = 0, presetIndex_ = -1;
    juce::ComboBox preset_;
    juce::TextButton prev_ { "<" }, next_ { ">" }, save_ { "Save..." };
    std::vector<eph::SoundPreset> user_;   ///< the user's presets of the synth (ids from 5001 in the list)
    std::unique_ptr<juce::AlertWindow> nameDialog_;
    juce::ToggleButton allRows_ { "All rows" };   ///< on a page with instances: a preset goes to every one
    juce::Label composed_;   ///< the preset the composer chose for this piece (compose.pick_sounds), where the piece now is
    int shown_ = -2;         ///< the preset composed_ shows (-1 none)
    void timerCallback() override;   ///< follows the piece: the composer's preset of the moment
    // An effect page (no factory presets): the user's presets of the whole page, every module of it, by full key.
    bool pagePresets_ = false;
    std::vector<std::pair<juce::String, juce::String>> pageUser_;   ///< name and text
    juce::File pageFolder() const;
    void fillPagePresets();
    juce::OwnedArray<juce::Component> controls_;
    juce::OwnedArray<juce::Label> labels_;
    /** @brief A control of the page: its name above it, large or not (EditorTheme.h, layoutOf). */
    struct Cell {
        int control = -1;          ///< index into controls_ and labels_
        bool big = false;          ///< a large encoder
        bool narrow = false;       ///< a narrow menu (short entries: an LFO's shape and sync)
        int kind = 0;              ///< 0 a knob, 1 a menu, 2 a switch, 3 a slot of the modulation matrix
        juce::Rectangle<int> bounds;
    };
    /** @brief A titled group of cells, drawn as a box. */
    struct Box {
        juce::String title;
        juce::Colour colour;
        std::vector<Cell> cells;
        juce::Rectangle<int> bounds;
    };
    std::vector<Box> boxes_;
    int top() const;   ///< height of the instance and preset bar
    /** @brief Places the boxes and their cells in @p area (@p apply: move the components too); returns the height used. */
    int layoutBoxes(juce::Rectangle<int> area, bool apply);
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

/** @brief A parameter page in a viewport: it scrolls when its groups need more height than the tab gives. */
class ScrollingPage final : public juce::Component {
public:
    explicit ScrollingPage(std::unique_ptr<ParamPage> page);   ///< takes the page over
    void resized() override;                                   ///< the page as wide as the view, as tall as it needs
    /** @brief Sizes @p page for a view of @p area (the scroll bar taken off where it will show). */
    static void fit(ParamPage& page, juce::Viewport& view, juce::Rectangle<int> area);
private:
    juce::Viewport view_;
    std::unique_ptr<ParamPage> page_;
};

/**
 * @brief The editor's body: everything, drawn at the design size and scaled to the window (26.09.2026, as Phosphene's),
 *        so a maximised or full-screen window shows the panel larger rather than emptier.
 */
class EditorBody final : public juce::Component {
public:
    std::function<void(juce::Graphics&)> painter;   ///< draws the background and the logo
    std::function<void()> onResize;                 ///< lays the controls out
    void paint(juce::Graphics& g) override { if (painter) painter(g); }
    void resized() override { if (onResize) onResize(); }
};

/** @brief The editor. */
class EphemerisEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit EphemerisEditor(EphemerisProcessor& p);   ///< builds the panel for @p p
    ~EphemerisEditor() override;                       ///< stops the refresh timer
    void paint(juce::Graphics& g) override;            ///< the background
    void resized() override;                           ///< scales the body to the window
    void parentHierarchyChanged() override;            ///< the standalone's title bar gets a maximise button
    bool keyPressed(const juce::KeyPress& key) override;   ///< F11: full screen (the standalone), Esc leaves it

private:
    void timerCallback() override;
    void layoutBody();                                 ///< the top bar, the arrange view, the tabs, at the design scale
    void toggleFullScreen();                           ///< the standalone's window full screen and back
    EphemerisProcessor& proc_;
    ephui::LookAndFeel lnf_;                           ///< first, so it outlives every component that uses it
    juce::TooltipWindow tooltips_{ nullptr, 700 };
    EditorBody body_;
    juce::TextButton full_{ "Full screen" };
    // The update check (UpdateCheck.h): a link where a newer version is out, and the switch.
    juce::SharedResourcePointer<UpdateCheck> updates_;
    juce::HyperlinkButton update_;
    juce::ToggleButton checkUpdates_{ "Update check" };
    juce::Label title_, status_, rerolls_;
    juce::Rectangle<float> logo_;   ///< where the logo is drawn, left of the title
    juce::ComboBox style_, key_, scale_;
    juce::Slider minutes_, concert_;
    juce::Label minutesLabel_, concertLabel_;
    juce::ToggleButton night_ { "Night" };   ///< compose.night_set: the concert as a night set
    std::unique_ptr<juce::ButtonParameterAttachment> nightAttach_;
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
