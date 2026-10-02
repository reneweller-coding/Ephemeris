/**
 * @file PluginEditor.h
 * @brief The plugin's panel (PLAN 8.1): the concert on top, the instrument in tabs below.
 *
 * Top, the shared frame's header (Frame.h, 01.10.2026): style, key, scale, a piece, a concert or a night set and its
 * length, compose, a new seed, play, mute; the status and the rerolls, then undo, redo, help and the settings; the
 * arrange view -- the sections of the piece as blocks, the playhead, click to jump. Below, the tabs in the order every
 * generator has them: Set, Arrange (the rerolls, the arrangement large and the automation -- the composed player's moves
 * on the knobs), Rack, Synths (the row voices, the lead, the drone, the poly synth, the tape keys, the strings),
 * Atmosphere, Drums, Rooms (echo and spring, the hall), Mixer (the console, the master), Perform, Export, Style. The
 * synth pages are generated from the parameter tables (the rows and the voices with a selector for the instance), as in
 * Noctuary and Phosphene, so a parameter that exists is on the panel without anyone writing it there.
 *
 * `EPH_SHOT` (a PNG file) and `EPH_TAB` (a tab index) render the panel into a picture after the first piece is
 * composed and quit the standalone -- how the layout is checked without a person looking. `EPH_SHOT_SIZE` ("1600x2400") the window's size, `EPH_SHOT_FULL` a window as tall as the page in front needs, `EPH_SHOT_AT` (a beat)
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
    /** @brief Slot @p number of the matrix, drawn in @p colour. */
    ModSlotControl(int number, juce::Colour colour);
    /** @brief The number, the two menus and the amount in a row. */
    void resized() override;
    /** @brief The number. */
    void paint(juce::Graphics& g) override;
    juce::ComboBox source;   ///< the slot's source
    juce::ComboBox target;   ///< the slot's target
    juce::Slider amount;   ///< the slot's amount, -1 .. 1

private:
    int number_;   ///< the slot's number
};

/**
 * @brief The parameters of one module instance as knobs, menus and switches: every control with the frame's right-click
 *        menu (MIDI learn, forget, default), a double click to its default, and the live ring of the moves.
 */
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
    /** @brief The height the page has in its window (the scrolling page around it says so before heightFor). */
    void setAvailableHeight(int h) { available_ = h; }
    /** @brief The page's parameters in words, group by group (the help's topic for the tab). */
    juce::String describe() const;

private:
    /** @brief Makes the controls of every group (once, in the constructor). */
    void build();
    /** @brief Applies factory preset @p index to the page's synth (the instance shown). */
    void choosePreset(int index);
    /** @brief Fills the list: the factory presets in their groups, then the user's under "User". */
    void fillPresets();
    EphemerisProcessor& proc_;   ///< the processor: the parameters, the undo, MIDI learn
    frame::ControlActions actions_;               ///< the controls' right-click menu
    frame::LiveRings live_;                       ///< where each knob's value plays
    std::vector<std::pair<eph::Module, int>> groups_;   ///< the module instances shown, in order
    int instances_;   ///< how many instances the modules have
    juce::ComboBox instance_;   ///< the instance selector (above one instance)
    /// The factory presets of the page's synth (Presets.h): a list in groups, and a step back and forth.
    eph::Module presetModule_ = eph::Module::Count;
    int presetCount_ = 0;   ///< how many factory presets the page's synth has
    int presetIndex_ = -1;   ///< the preset chosen, -1 none
    juce::ComboBox preset_;   ///< the presets in their groups, then the user's
    juce::TextButton prev_ { "<" };   ///< the preset before
    juce::TextButton next_ { ">" };   ///< the preset after
    frame::IconButton save_ { frame::IconButton::Icon::Save, "Save the sound as a preset of your own" };   ///< saves the page as a user preset: a disk
    std::vector<eph::SoundPreset> user_;   ///< the user's presets of the synth (ids from 5001 in the list)
    std::unique_ptr<juce::AlertWindow> nameDialog_;   ///< the user preset's name dialog while it is open
    juce::ToggleButton allRows_ { "All rows" };   ///< on a page with instances: a preset goes to every one
    juce::Label composed_;   ///< the preset the composer chose for this piece (compose.pick_sounds), where the piece now is
    int shown_ = -2;         ///< the preset composed_ shows (-1 none)
    void timerCallback() override;   ///< follows the piece: the composer's preset of the moment
    // An effect page (no factory presets): the user's presets of the whole page, every module of it, by full key.
    bool pagePresets_ = false;   ///< an effect page: the user presets are the whole page's
    std::vector<std::pair<juce::String, juce::String>> pageUser_;   ///< name and text
    /** @brief The folder of this page's user presets. */
    juce::File pageFolder() const;
    /** @brief Fills the list with the page's user presets. */
    void fillPagePresets();
    juce::OwnedArray<juce::Component> controls_;   ///< the knobs, menus, switches and preset bars
    juce::OwnedArray<juce::Label> labels_;   ///< the names above them
    /** @brief A control of the page: its name above it, large or not (EditorTheme.h, layoutOf). */
    struct Cell {
        int control = -1;          ///< index into controls_ and labels_
        bool big = false;          ///< a large encoder
        bool narrow = false;       ///< a narrow menu (short entries: an LFO's shape and sync)
        int kind = 0;              ///< 0 a knob, 1 a menu, 2 a switch, 3 a slot of the modulation matrix
        juce::Rectangle<int> bounds;   ///< where it sits on the page
    };
    /** @brief A titled group of cells, drawn as a box. */
    struct Box {
        juce::String title;   ///< the group's title
        juce::Colour colour;   ///< the module family's colour
        std::vector<Cell> cells;   ///< its controls
        juce::Rectangle<int> bounds;   ///< where the box sits on the page
    };
    std::vector<Box> boxes_;   ///< the groups, in order
    /**
     * @brief A page taller than its window shows its groups in sections, one at a time (01.10.2026, frame::planSections):
     *        the sound and the modulation apart, each cut where the window ends.
     */
    frame::SectionSwitch section_{ ephui::skin() };
    bool split_ = false;                          ///< more than one section
    std::vector<int> sectionOf_;                  ///< per box its section
    const std::vector<int>* measuring_ = nullptr; ///< while planning: the boxes being measured
    int available_ = 0;   ///< the height of the window (setAvailableHeight)
    int plannedWidth_ = -1;   ///< the width the sections were planned for, -1 never
    int plannedAvailable_ = -1;   ///< the height the sections were planned for, -1 never
    bool shown(size_t box) const;                 ///< the box is in the section shown (or being measured)
    void plan(int width);                         ///< the sections of a page @p width wide in available_
    void applySection();                          ///< the controls of the section shown, the others hidden
    void refit();                                 ///< laid out again, and the scrolling page around it measures again
    int top() const;   ///< height of the instance and preset bar
    /** @brief Places the boxes and their cells in @p area (@p apply: move the components too); returns the height used. */
    int layoutBoxes(juce::Rectangle<int> area, bool apply);
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliders_;   ///< the knobs on their parameters
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> combos_;   ///< the menus on their parameters
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttons_;   ///< the switches on their parameters
};

/**
 * @brief The sections of the piece as blocks, with the playhead, and the instrumentation matrix (PLAN 8.1,
 *        Arrange): under the sections' names a lane per layer -- the rows (brighter the more of them play),
 *        the lead, the tape keys, the strings, the drone, the drums -- lit where it has notes.
 *
 * It zooms (01.10.2026, as Parhelion's and Totality's do): the mouse wheel in and out around the pointer, down to four
 * bars; a drag, Shift with the wheel or the wheel sideways moves along; a double click shows the whole length again.
 * A ruler of bars (a piece) or of minutes (a concert, a night set); once a beat has two pixels, the notes themselves on
 * the lanes; zoomed, a bar along the bottom where the window lies in the whole. The window pages on with the playhead,
 * unless it was moved away from it. A concert's pieces are apart by a line, seen whole by their numbers. What does not
 * move is drawn into an image when the window changes; the playhead goes over it.
 */
class ArrangeView final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer {
public:
    explicit ArrangeView(EphemerisProcessor& p);                ///< shows @p p's score
    void paint(juce::Graphics& g) override;                     ///< the picture of the window, the playhead over it
    void mouseDown(const juce::MouseEvent& e) override;         ///< jumps there (zoomed: on release, if it was no drag)
    void mouseDrag(const juce::MouseEvent& e) override;         ///< moves a zoomed view along
    void mouseUp(const juce::MouseEvent& e) override;           ///< the jump of a zoomed view
    void mouseDoubleClick(const juce::MouseEvent& e) override;  ///< the whole length again
    /** @brief Zooms around the pointer; the wheel sideways, or with Shift, moves along. */
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& e, float scale) override;   ///< a trackpad's pinch zooms
    /** @brief Shows the beats @p from to @p to once a score is there (the pictures' EPH_SHOT_ZOOM). */
    void zoomTo(double from, double to) { wanted_ = { from, to }; }

private:
    /** @brief A note as the zoomed lanes draw it: where it begins and ends (beats), how loud. */
    struct Hit {
        float from;       ///< where it begins, beats
        float to;         ///< where it ends, beats
        float velocity;   ///< how loud, 0..1
    };
    static constexpr int kLanes = 6;             ///< rows, lead, tape keys, strings, drone, drums
    static constexpr double kNarrowest = 16.0;   ///< the least the view shows, in beats (four bars)
    void timerCallback() override;               ///< pages on with the playhead, repaints
    void rebuild();                              ///< the lanes of a new score
    void show(double from, double span);         ///< the window, kept inside the length (all of it: not zoomed)
    void zoomAround(float x, double factor);     ///< the window times @p factor, the beat under @p x staying put
    void window(double& from, double& to) const; ///< the beats shown
    bool zoomed(double& from, double& to) const; ///< the beats shown; false while it is the whole length
    double beatAt(float x) const;                ///< the beat under @p x
    void render(juce::Graphics& g, float w, float h, double from, double to, bool zoom);   ///< all but the playhead
    EphemerisProcessor& proc_;   ///< the processor: what plays, where it is
    int version_ = -1;                    ///< the score the lanes were built from
    double beats_ = 0.0;                  ///< its length
    std::vector<eph::Marker> markers_;    ///< its sections
    eph::TempoMap tempo_;                 ///< its tempo (the ruler's minutes)
    int bins_ = 0;                        ///< the lanes' columns across the score: one a beat
    std::vector<float> lanes_;            ///< kLanes x bins_ activity, 0..1
    std::vector<Hit> hits_[kLanes];       ///< the notes per lane, in the order they begin
    double longest_ = 0.0;                ///< the longest note (how far before a window its notes begin)
    double from_ = 0.0;   ///< the first beat of the window
    double span_ = 0.0;   ///< its length in beats (0: the whole length)
    double lastPos_ = -1.0;   ///< the playhead at the last tick
    double lastFrom_ = 0.0;   ///< the window's first beat at the last tick
    double lastTo_ = 0.0;   ///< the window's last beat at the last tick
    float downX_ = 0.0f;                  ///< where a press began
    double downFrom_ = 0.0;               ///< the window's beginning then
    bool dragged_ = false;   ///< the press has moved the view: no jump on release
    std::pair<double, double> wanted_{ 0.0, 0.0 };   ///< zoomTo's window, until a score takes it
    juce::Image cache_;                   ///< the picture of the window
    double cachedFrom_ = -1.0;   ///< the first beat cache_ was drawn for
    double cachedTo_ = -1.0;   ///< the last beat cache_ was drawn for
    int cachedVersion_ = -1;   ///< the score cache_ was drawn from, -1 none
};

/** @brief A parameter page in a viewport: it scrolls when its groups need more height than the tab gives. */
class ScrollingPage final : public juce::Component {
public:
    explicit ScrollingPage(std::unique_ptr<ParamPage> page);   ///< takes the page over
    void resized() override;                                   ///< the page as wide as the view, as tall as it needs
    /** @brief Sizes @p page for a view of @p area (the scroll bar taken off where it will show). */
    static void fit(ParamPage& page, juce::Viewport& view, juce::Rectangle<int> area);
    const ParamPage& page() const { return *page_; }           ///< the page (the help describes it)
private:
    juce::Viewport view_;   ///< scrolls the page
    std::unique_ptr<ParamPage> page_;   ///< the page
};

/**
 * @brief The Arrange tab (01.10.2026, as the siblings have it): the rerolls -- each unit on its own stream (SetFile.h),
 *        so a reroll changes that and nothing else --, the arrangement large, and under it the automation: the moves of
 *        the composed player's two hands on the knobs over the whole piece (EditorGestures.h).
 */
class ArrangePage final : public juce::Component, private juce::Timer {
public:
    /** @brief The tab for @p p. */
    explicit ArrangePage(EphemerisProcessor& p);
    /** @brief The rerolls on top, the view large, the automation under it. */
    void resized() override;
    /** @brief The background and the headings. */
    void paint(juce::Graphics& g) override;

private:
    /** @brief Shows what plays under the playhead. */
    void timerCallback() override;
    EphemerisProcessor& proc_;   ///< the processor: what plays, the rerolls
    juce::Label which_;   ///< what plays under the playhead (the status line)
    juce::OwnedArray<juce::TextButton> rerolls_;   ///< a reroll per unit of the piece
    std::unique_ptr<ArrangeView> view_;   ///< the arrangement, large
    std::unique_ptr<juce::Component> moves_;      ///< the automation (GestureView)
};

/** @brief The Export tab (01.10.2026, as the siblings have it): the files, what each holds, the OSC cues' settings. */
class ExportPage final : public juce::Component, private juce::Timer {
public:
    /** @brief The tab for @p p. */
    explicit ExportPage(EphemerisProcessor& p);
    /** @brief The buttons, the status, the cue settings under them. */
    void resized() override;
    /** @brief The background and what each file holds. */
    void paint(juce::Graphics& g) override;
    void exportWith(bool stems);   ///< asks for a file, then exports (Ctrl+E: without the stems)
    void save();                   ///< asks for a file, then saves the set (Ctrl+S)
    void load();                   ///< asks for a set, then loads it (Ctrl+O)

private:
    /** @brief Shows the export's progress and result. */
    void timerCallback() override;
    EphemerisProcessor& proc_;   ///< the processor: the export, the set file
    juce::TextButton wav_{ "WAV + MIDI" };   ///< exports the WAV and the MIDI
    juce::TextButton stems_{ "... with stems" };   ///< ... and the stems
    frame::IconButton save_{ frame::IconButton::Icon::Save, "Save the set (Ctrl+S)" };   ///< saves the set (Ctrl+S): a disk
    frame::IconButton load_{ frame::IconButton::Icon::Open, "Load a set (Ctrl+O)" };   ///< loads a set (Ctrl+O): a folder
    juce::Label status_;   ///< the export's progress and result
    std::unique_ptr<ParamPage> cue_;   ///< the OSC cues' settings
    std::unique_ptr<juce::FileChooser> chooser_;   ///< the file dialog while it is open
};

/**
 * @brief The editor's body: everything, drawn at the design size and scaled to the window (26.09.2026, as Phosphene's),
 *        so a maximised or full-screen window shows the panel larger rather than emptier.
 */
class EditorBody final : public juce::Component {
public:
    std::function<void(juce::Graphics&)> painter;   ///< draws the background and the logo
    std::function<void()> onResize;                 ///< lays the controls out
    /** @brief Draws through painter. */
    void paint(juce::Graphics& g) override { if (painter) painter(g); }
    /** @brief Lays out through onResize. */
    void resized() override { if (onResize) onResize(); }
};

/** @brief The editor: the frame's header and keys, the arrange view, the tabs, the help over them. */
class EphemerisEditor final : public juce::AudioProcessorEditor, private juce::Timer, private juce::ChangeListener {
public:
    explicit EphemerisEditor(EphemerisProcessor& p);   ///< builds the panel for @p p
    ~EphemerisEditor() override;                       ///< stops the refresh timer
    void paint(juce::Graphics& g) override;            ///< the background
    void resized() override;                           ///< scales the body to the window
    void parentHierarchyChanged() override;            ///< the standalone's title bar gets a maximise button
    bool keyPressed(const juce::KeyPress& key) override;   ///< the frame's keys (frame::handleKey)

private:
    /** @brief Follows the processor: the status, the play button, the rerolls, the update link; the screenshots. */
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;   ///< the settings changed (the backdrop)
    void layoutBody();                                 ///< the top bar, the arrange view, the tabs, at the design scale
    void toggleFullScreen();                           ///< the standalone's window full screen and back
    bool fullScreen() const;                           ///< whether it is
    bool standalone() const;                           ///< the editor sits in the standalone's window
    void showLength(int kind);                         ///< the length slider for a piece's minutes or a concert's
    void showHelp(bool on);                            ///< the help over the tabs (F1)
    void showSettings();                               ///< the settings menu
    ExportPage* exportPage() const;                    ///< the Export tab's page
    EphemerisProcessor& proc_;   ///< the processor
    frame::LookAndFeel lnf_{ ephui::skin() };          ///< first, so it outlives every component that uses it
    juce::TooltipWindow tooltips_{ nullptr, 700 };   ///< the tooltips, after 700 ms
    EditorBody body_;   ///< everything, at the design size
    frame::Backdrop backdrop_;   ///< the picture behind the panel
    int headerBottom_ = 0;                             ///< where the header ends (the backdrop is strong above)
    // The update check (UpdateCheck.h): a link where a newer version is out; the settings switch it.
    juce::SharedResourcePointer<UpdateCheck> updates_;   ///< the update check, shared by every editor
    juce::HyperlinkButton update_;   ///< the link to a newer release
    juce::Label status_;   ///< what plays, the status line
    juce::Label rerolls_;   ///< what was rerolled or locked
    juce::Component title_;                            ///< the name's place (drawn by the body)
    frame::IconButton undo_{ frame::IconButton::Icon::Undo, "Undo (Ctrl+Z)" };   ///< undo (Ctrl+Z)
    frame::IconButton redo_{ frame::IconButton::Icon::Redo, "Redo (Ctrl+Y)" };   ///< redo (Ctrl+Y)
    frame::IconButton help_{ frame::IconButton::Icon::Help, "Help (F1)" };   ///< the help (F1)
    frame::IconButton settings_{ frame::IconButton::Icon::Settings, "Settings" };   ///< the settings menu
    /** @brief shown while a headset sends its hands */
    frame::IconButton headsetIcon_{ frame::IconButton::Icon::Headset, "A headset sends its hands: the Perform page shows them" };
    std::unique_ptr<frame::HelpView> helpView_;   ///< the help, while it is open
    bool shotHands_ = false;                           ///< EPH_SHOT_HEADSET: the headset's hands kept alive
    juce::Rectangle<float> logo_;   ///< where the logo is drawn, left of the title
    juce::ComboBox style_;   ///< compose.style
    juce::ComboBox key_;   ///< compose.key
    juce::ComboBox scale_;   ///< compose.scale
    /** A piece, a concert or a night set (EphemerisProcessor::chooseKind), and the length of what is chosen. */
    juce::TextButton pieceMode_{ "Piece" };
    juce::TextButton concertMode_{ "Concert" };   ///< a concert (compose.concert_minutes)
    juce::TextButton nightMode_{ "Night set" };   ///< a night set
    juce::Slider length_;   ///< the length: a piece's minutes or a concert's
    juce::Label lengthLabel_;   ///< the length's name
    int lengthKind_ = -1;                              ///< what the slider shows (0: compose.piece_minutes, else concert_minutes)
    bool syncing_ = false;                             ///< the slider is set from its parameter, not by a hand
    juce::TextButton compose_{ "Compose piece" };   ///< composes a piece, a concert or a night set
    frame::IconButton seed_{ frame::IconButton::Icon::Dice, "New seed" };   ///< a new seed, then composes: a die
    frame::IconButton play_{ frame::IconButton::Icon::Play, "Play (Space)" };   ///< play and stop: a triangle, a square
    frame::IconButton mute_{ frame::IconButton::Icon::Speaker, "Silence the output" };   ///< mutes the output: a speaker, crossed out when muted
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> combos_;   ///< the header's menus on their parameters
    ArrangeView arrange_;   ///< the arrange strip on top
    juce::TabbedComponent tabs_{ juce::TabbedButtonBar::TabsAtTop };   ///< the pages
    juce::String shotPath_;   ///< TOT_SHOT: where the screenshot goes, empty none
    int shotTicks_ = 0;   ///< ticks until the screenshot is taken
};
