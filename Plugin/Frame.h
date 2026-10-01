/**
 * @file Frame.h
 * @brief The frame the generators share (01.10.2026): one way of working all of them, and a skin of its own for each.
 *
 * The user (01.10.2026): the generators should be unified "damit potentielle Nutzer nicht soviel umlernen müssen --
 * allerdings nur, wenn es nicht auf Kosten der Übersichtlichkeit geht", each with a style of its own, and the headset's
 * gestures offered only when a Quest is there or asked for. What is the same everywhere lives here:
 *
 * - **the look and feel** of every control, drawn from a Skin: the knob with the live ring (where a value plays away
 *   from where it stands), the fader, the switch, the menu, the tabs;
 * - **the header**: logo and name, style, key, scale, the choice of a single piece or a set and its length, Compose,
 *   New seed, Play and Mute in the first row; the ratings, the status and the curation, then undo, redo, help and the
 *   settings in the second (layoutHeader);
 * - **the backdrop**, the skin's picture: strong behind the header, faint behind the pages, switchable;
 * - **the settings** (one per instrument, not per project): the update check, the backdrop, the headset, the window's
 *   size, full screen, the keys and the version;
 * - **the keys**: Space play and stop, Ctrl+Z and Ctrl+Y undo and redo, F1 help, F11 full screen, Esc back, Ctrl+S save,
 *   Ctrl+O open, Ctrl+E export;
 * - **the help**: the manual's prose by topic, with topics an editor makes on the spot (its tab's parameters);
 * - **undo** (UndoHistory), **sub-tabs** (SubTabs), the **controls** with their right-click menu (MIDI learn, forget,
 *   default; Knob, Choice, Switch) and the **live rings** (LiveRings);
 * - **the headset** (Headset): the hands of a Meta Quest arriving as OSC from the instrument's Quest app (bridge mode),
 *   and the grammar every app shares -- left pinch play and stop, both hands pinched the next one, right pinch the
 *   genre's action (held: its second), left hand's height the master filter, right hand's the echo throw.
 *
 * Nothing here knows an engine: an editor hands its processor's functions in.
 *
 * @note Identical in Ephemeris, Totality, Parhelion and Phosphene (Plugin/Frame.h and Frame.cpp), and Noctuary uses its
 *       settings, keys and headset parts: a change is made in all of them.
 */
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_osc/juce_osc.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace frame {

// ===================================================================================================== the skin

/** @brief The families of function: the same colour means the same kind of control on every page of every generator. */
enum class Family { Source, Filter, Envelope, Motion, Space };

/** @brief An instrument's look: its palette, its picture and its logo. */
struct Skin {
    juce::String name;                 ///< as the title shows it
    juce::Colour bg;                   ///< the window
    juce::Colour panel;                ///< a page (translucent: the backdrop shows faintly through it)
    juce::Colour group;                ///< a group box
    juce::Colour raised;               ///< buttons, menus, the knobs' bodies
    juce::Colour edge;                 ///< hairlines
    juce::Colour ink;                  ///< text
    juce::Colour dim;                  ///< names, secondary text
    juce::Colour faint;                ///< tracks, axes, the off state
    juce::Colour accent;               ///< the instrument's own colour: the title, the tab in front, the playhead's region
    juce::Colour onset;                ///< the playhead, a mute
    juce::Colour good;                 ///< a meter in range
    juce::Colour bad;                  ///< a meter over
    juce::Colour families[5];          ///< Source, Filter, Envelope, Motion, Space
    juce::Colour decks[3];             ///< deck A, B, C
    float radius = 4.0f;               ///< corners of buttons, menus and boxes
    float tracking = 0.06f;            ///< the title's letter spacing, of its height
    juce::String typeface;             ///< the title's typeface (empty: the panel's own)
    bool titleBold = true;             ///< the title bold
    bool valueInKnob = false;          ///< a knob without a text box shows its value inside (Phosphene's dense pages)
    bool glow = false;                 ///< arcs and the title drawn with a soft glow
    /**
     * @brief The picture behind the panel, as compressed bytes (BinaryData; null: none). Bytes and not a juce::Image: a
     *        skin is a static, and an image destroyed after JUCE has shut down (a plugin's unloading) hangs the host.
     */
    const void* backdropData = nullptr;
    int backdropSize = 0;
    juce::Rectangle<float> crop{ 0.0f, 0.0f, 1.0f, 1.0f };   ///< the part of it used, normalised
    float backdropTop = 0.7f;          ///< its strength behind the header
    float backdropPage = 0.35f;        ///< behind the pages (under the translucent panel)
    /** @brief Draws the logo into a square. */
    std::function<void(juce::Graphics&, juce::Rectangle<float>)> logo;
    /** @brief The backdrop's picture (from JUCE's image cache, which lives as long as JUCE does). */
    juce::Image backdrop() const { return backdropData != nullptr ? juce::ImageCache::getFromMemory(backdropData, backdropSize) : juce::Image(); }

    juce::Colour family(Family f) const { return families[static_cast<int>(f)]; }   ///< a family's colour
    juce::Colour deck(int d) const { return decks[juce::jlimit(0, 2, d)]; }         ///< a deck's colour
};

// =============================================================================================== the look and feel

/**
 * @brief Every control drawn from a Skin. A slider with the property "live" (0..1, normalised) shows where its value
 *        plays (LiveRings): a thin bright arc from where it stands to there, and a dot.
 */
class LookAndFeel : public juce::LookAndFeel_V4 {
public:
    explicit LookAndFeel(const Skin& skin);
    const Skin& skin() const { return skin_; }   ///< the skin it draws with

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float pos, float startAngle, float endAngle,
                          juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& background, bool highlighted, bool down) override;
    void drawTabButton(juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override;
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) override;
    int getTabButtonBestWidth(juce::TabBarButton&, int tabDepth) override;
    juce::Font getTabButtonFont(juce::TabBarButton&, float height) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;

private:
    const Skin& skin_;
};

/** @brief Draws the instrument's name in the skin's title style (letter-spaced, glowing where the skin glows). */
void drawTitle(juce::Graphics& g, const Skin& skin, juce::Rectangle<float> r, float height);
/** @brief The width drawTitle needs for the name at @p height. */
float titleWidth(const Skin& skin, float height);

// =========================================================================================================== icons

/**
 * @brief A tab of a row of tabs drawn as the frame's TabbedComponent draws its own (a generator whose pages are not a
 *        TabbedComponent -- Phosphene's -- uses these): the name, lit in the accent and underlined while it is open.
 */
class FlatTab final : public juce::Button {
public:
    explicit FlatTab(const juce::String& name) : juce::Button(name) { setWantsKeyboardFocus(false); }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    /** @brief The width the name needs at the frame's tab font. */
    int bestWidth() const;
};

/** @brief A small button with a drawn glyph (no font needed): undo, redo, help, settings, the ratings, play, stop. */
class IconButton final : public juce::Button {
public:
    enum class Icon { Undo, Redo, Help, Settings, ThumbUp, ThumbDown, Headset };
    IconButton(Icon icon, const juce::String& tooltip);
    void setIcon(Icon icon) { icon_ = icon; repaint(); }
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    Icon icon_;
};

// ========================================================================================================== header

/**
 * @brief The parts of the header, in their places (layoutHeader). Null parts are left out; widths of 0 take the
 *        default. The first row: logo, title, choices (style, key, scale), modes (connected buttons), length label and
 *        slider, actions (Compose, New seed), play, mute. The second: like, dislike, status, curation, the update notice,
 *        and the tools (undo, redo, help, settings) at its right end.
 */
struct Header {
    juce::Rectangle<float>* logo = nullptr;
    juce::Component* title = nullptr;
    int titleWidth = 0;
    std::vector<std::pair<juce::Component*, int>> choices;
    std::vector<std::pair<juce::Component*, int>> modes;
    juce::Component* lengthLabel = nullptr;
    juce::Component* length = nullptr;
    std::vector<std::pair<juce::Component*, int>> actions;
    juce::Component* play = nullptr;
    juce::Component* mute = nullptr;
    juce::Component* like = nullptr;
    juce::Component* dislike = nullptr;
    juce::Component* status = nullptr;
    juce::Component* curation = nullptr;
    juce::Component* update = nullptr;
    std::vector<juce::Component*> tools;   ///< left to right; a null makes a gap between groups
};
constexpr int kHeaderRow = 34;     ///< the first row's height
constexpr int kStatusRow = 22;     ///< the second's
/** @brief Lays the header out in the top of @p area (which loses the two rows and a gap). */
void layoutHeader(juce::Rectangle<int>& area, const Header& h);
/** @brief Makes @p modes look like one control: connected edges, lit when chosen. */
void connectModes(std::initializer_list<juce::Button*> modes, juce::Colour on);

// ======================================================================================================== backdrop

/** @brief The skin's picture behind the panel, cached at the window's size. */
class Backdrop {
public:
    /**
     * @brief Paints the window's background into @p area: the bg colour, and with the picture on, the picture covering it,
     *        strong down to @p headerBottom and faint below, under a vignette.
     */
    void paint(juce::Graphics& g, juce::Rectangle<int> area, int headerBottom, const Skin& skin, bool on);

private:
    juce::Image cache_;
    juce::Rectangle<int> cachedFor_;
    int cachedHeader_ = -1;
    bool cachedOn_ = false;
};

// ======================================================================================================== settings

/**
 * @brief The instrument's settings: not the project's, the person's (application data/<name>/<name>.frame). They live
 *        until JUCE shuts down (a registry deleted at shutdown), never into a plugin's unloading.
 */
class Settings final : public juce::ChangeBroadcaster {
public:
    enum class HeadsetMode { Auto, On, Off };
    /** @brief The settings of instrument @p app (one object per name, message thread). */
    static Settings& of(const juce::String& app);
    explicit Settings(const juce::String& app);

    HeadsetMode headset() const;                  ///< Auto: shown when a headset sends; On: always; Off: never, nothing listens
    void setHeadset(HeadsetMode m);
    int headsetPort() const;                      ///< the UDP port the hands arrive on (9100 + the instrument's offset)
    bool backdrop() const;                        ///< the picture behind the panel
    void setBackdrop(bool on);
    /** @brief The overview above the tabs (01.10.2026: folded away, the pages get its height). */
    bool overview() const;
    void setOverview(bool on);
    juce::String app() const { return app_; }

private:
    juce::String app_;
    std::unique_ptr<juce::PropertiesFile> file_;
};

/** @brief What the settings menu shows and does; empty functions leave their items out. */
struct SettingsMenu {
    juce::String app, version;
    std::function<bool()> updatesOn;
    std::function<void(bool)> setUpdates;
    std::function<bool()> canFullScreen;
    std::function<bool()> isFullScreen;
    std::function<void()> toggleFullScreen;
    std::function<void(float)> setWindowScale;    ///< 1.0: the design size
    std::function<juce::String()> headsetStatus;  ///< a line on what the headset receiver hears
    juce::String headsetOffText = "Off: do not listen";   ///< what Off means for this instrument
    std::function<void(juce::PopupMenu&)> headsetItems;   ///< more items in the headset's submenu (calibrate ...)
    std::function<void(juce::PopupMenu&)> moreItems;      ///< the instrument's own items, before Keys and About
    std::function<juce::String()> about;          ///< more lines for "About"
    std::function<void()> showAbout;              ///< the instrument's own About (replaces the frame's)
    /** @brief Shows the menu under @p target. */
    void show(juce::Component& target) const;
};

// ============================================================================================================ keys

/** @brief The actions behind the keys; an empty one leaves its key to whoever wants it. */
struct Keys {
    std::function<void()> playStop, undo, redo, help, fullScreen, escape, save, open, exportFile;
};
/** @brief Handles @p key as every generator does; true if it was one of the frame's keys. */
bool handleKey(const juce::KeyPress& key, const Keys& keys);
/** @brief The keys in words (the settings menu and the help). */
juce::String keysText();
/** @brief Buttons, switches and menus under @p root never take the keyboard focus: the keys stay the editor's. */
void keepKeysForEditor(juce::Component& root);

// ============================================================================================================ help

/**
 * @brief The help: a list of topics and the text of the one chosen -- the manual's prose (chapters.txt: "== Title |
 *        modules | shot ==" starts a chapter, '#' a comment, blank lines part paragraphs, lines indented by four spaces
 *        stand as they are) and the topics the editor makes when it is opened.
 */
class HelpView final : public juce::Component, private juce::ListBoxModel {
public:
    HelpView(const juce::String& chapters, const Skin& skin);
    /** @brief Topics made each time the help opens (name, text), shown first. */
    std::function<std::vector<std::pair<juce::String, juce::String>>()> extraTopics;
    std::function<void()> onClose;                       ///< the close button or Esc
    void refresh();                                      ///< remakes the extra topics, keeps the one chosen
    void showTopic(int index);
    void resized() override;
    void paint(juce::Graphics& g) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    int getNumRows() override { return names_.size(); }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    const Skin& skin_;
    std::vector<std::pair<juce::String, juce::String>> chapters_;
    juce::StringArray names_, texts_;
    juce::ListBox list_;
    juce::TextEditor text_;
    juce::TextButton close_{ "Close" };
};

// ============================================================================================================ undo

/**
 * @brief One step of undo: what changed and nothing else -- the values that moved (before, after) and the rest of the
 *        state as text (the seed, the rerolls). Only what changed, because the engine writes on the knobs by itself (a
 *        track's sounds when it begins): an undo must not take that back.
 */
struct UndoStep {
    juce::String what;                                   ///< "Cutoff", "New seed", "reroll blocks"
    std::vector<std::tuple<int, float, float>> values;   ///< store id, before, after (real values)
    juce::String extraBefore, extraAfter;                ///< the rest of the state, as the processor writes it
    bool empty() const { return values.empty() && extraBefore == extraAfter; }
};

/**
 * @brief Undo and redo, and the bookkeeping of a step: a step opens (the knobs and the rest as they were), the knobs
 *        named in it may move, and it closes (what moved since is the step). Steps nest: a preset's twenty knobs, or a
 *        drag of two, are one step. Message thread only.
 */
class UndoHistory {
public:
    /** @brief Opens a step named @p what with the state as it is (@p values, @p extra); nested ones join the outer. */
    void begin(const juce::String& what, std::vector<float> values, juce::String extra, bool allValues);
    /** @brief Names a value the open step may record (a knob gesture's); a step opened with allValues takes every one. */
    void touch(int id) { if (depth_ > 0) touched_.push_back(id); }
    /** @brief Closes a step with the state as it is now; the outermost close records it, unless nothing changed. */
    void end(const std::vector<float>& values, const juce::String& extra);
    bool open() const { return depth_ > 0; }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    juce::String undoName() const { return undo_.empty() ? juce::String() : undo_.back().what; }   ///< the step undo takes back
    juce::String redoName() const { return redo_.empty() ? juce::String() : redo_.back().what; }   ///< the step redo makes again
    /** @brief Takes the last step back: apply its "before" (null: nothing to undo). */
    const UndoStep* undo();
    /** @brief Makes the last undone step again: apply its "after" (null: nothing to redo). */
    const UndoStep* redo();
    void clear() { undo_.clear(); redo_.clear(); }

private:
    static constexpr size_t kSteps = 200;
    std::deque<UndoStep> undo_, redo_;
    int depth_ = 0;
    juce::String what_, extra_;
    std::vector<float> before_;
    std::vector<int> touched_;
    bool all_ = false;
};

// ======================================================================================================== sub-tabs

/** @brief A row of small tabs above a page: one tab of the main row holding several pages (the synths of a group). */
class SubTabs final : public juce::Component {
public:
    explicit SubTabs(const Skin& skin) : skin_(skin) {}
    /** @brief A page, made the first time it is shown; @p colour marks its tab (transparent: the accent). */
    void add(const juce::String& name, std::function<std::unique_ptr<juce::Component>()> make,
             juce::Colour colour = juce::Colours::transparentBlack);
    void show(int index);                         ///< shows page @p index
    int current() const { return current_; }
    int count() const { return static_cast<int>(pages_.size()); }
    juce::String name(int i) const { return pages_[static_cast<size_t>(i)].name; }
    juce::Component* page() const;               ///< the page shown (null before the first)
    std::function<void(int)> onChange;            ///< after a page was chosen
    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    struct Page {
        juce::String name;
        juce::Colour colour;
        std::function<std::unique_ptr<juce::Component>()> make;
        std::unique_ptr<juce::Component> comp;
        std::unique_ptr<juce::TextButton> button;
    };
    const Skin& skin_;
    std::vector<Page> pages_;
    int current_ = -1;
};

// ======================================================================================================= sections

/**
 * @brief Whether a group of a synth's page belongs to its modulation -- the LFOs, the mod envelope, the matrix, the
 *        trance gate (01.10.2026). A page that does not fit the window shows its sound and its modulation as sections
 *        of their own (planSections).
 */
bool isModulationGroup(const juce::String& title);

/** @brief A page's groups as sections, each to be shown alone, and the sections' names. */
struct SectionPlan {
    std::vector<std::vector<int>> groups;         ///< per section the group indices, in the page's order
    juce::StringArray names;                      ///< per section its name for the switch
};

/**
 * @brief Splits a page's groups into sections that each fit @p available pixels (01.10.2026: no page scrolls at the
 *        window's usual size). One section when the whole page fits (or overshoots by no more than @p tolerance); else
 *        the sound's groups and the modulation's (isModulationGroup) apart, each in the page's order and cut where the
 *        next group would not fit any more. Names: "Sound" and "Modulation" for the first of each kind where the page
 *        has both ("Sound" also where it begins with a synth's presets), else the first group's title (without its
 *        number: "LFO 1" -> "LFO", "Matrix 1-2" -> "Matrix"). FAMILY_NO_SECTIONS=1 keeps every page whole (the
 *        manual's full-page pictures).
 * @param titles the groups' titles, in the page's order
 * @param available the height the page has
 * @param height the height the page needs showing only the given groups (laid out as the page lays them out)
 */
SectionPlan planSections(const juce::StringArray& titles, int available, const std::function<int(const std::vector<int>&)>& height,
                         int tolerance = 0);

/** @brief The switch between a page's sections ("Sound" | "Modulation" | ...), drawn as the frame's small tabs. */
class SectionSwitch final : public juce::Component {
public:
    explicit SectionSwitch(const Skin& skin, const juce::StringArray& names = { "Sound", "Modulation" });
    int current() const { return current_; }
    int count() const { return buttons_.size(); }
    juce::String name(int index) const { return buttons_[index] != nullptr ? buttons_[index]->getButtonText() : juce::String(); }
    void setCurrent(int index);                   ///< without onChange
    void setNames(const juce::StringArray& names);   ///< new sections (the current one kept where it still exists)
    std::function<void(int)> onChange;            ///< after a section was chosen
    int bestWidth() const;                        ///< the width its buttons need
    void resized() override;

private:
    const Skin& skin_;
    juce::OwnedArray<juce::TextButton> buttons_;
    int current_ = 0;
};

/**
 * @brief How far every page of @p tabs reaches past the window (its viewports' overflow in pixels), every small tab
 *        of a page of several too, one line each: "Synths: Lead<TAB>412". The pages are shown one after the other and
 *        the one in front again at the end. For the screenshot mode's <P>_PAGE_REPORT (01.10.2026): which page still
 *        scrolls at the window's usual size, and by how much.
 */
juce::String pageReport(juce::TabbedComponent& tabs);

/** @brief The overflow of the viewports in @p c and below it (the largest), in pixels; 0 where nothing scrolls. */
int pageOverflow(juce::Component& c);

/** @brief One page's line of pageReport() -- and a line per section where it has a switch (Sound, Modulation). */
juce::String pageLines(juce::Component& page, const juce::String& name);

// ======================================================================================================== controls

/** @brief What a control's right-click menu does with a store id. */
struct ControlActions {
    std::function<void(int)> learn;               ///< binds the next MIDI controller to the id
    std::function<int(int)> controllerFor;        ///< the controller bound to it, -1 none
    std::function<void(int)> forget;              ///< unbinds it
    std::function<void(int)> reset;               ///< back to its default
    std::function<juce::String(int)> describe;    ///< a line on it (the menu's header)
    /** @brief Shows the menu for id @p id at @p target. */
    void showMenu(juce::Component& target, int id) const;
};
/** @brief A knob or fader with the right-click menu (ControlActions) and its store id. */
class Knob final : public juce::Slider {
public:
    Knob(SliderStyle style, TextEntryBoxPosition box, const ControlActions* actions, int id);
    void mouseDown(const juce::MouseEvent& e) override;
    int id() const { return id_; }

private:
    const ControlActions* actions_;
    int id_;
};
/** @brief A menu with the right-click menu. */
class Choice final : public juce::ComboBox {
public:
    Choice(const ControlActions* actions, int id) : actions_(actions), id_(id) {}
    void mouseDown(const juce::MouseEvent& e) override;
    int id() const { return id_; }

private:
    const ControlActions* actions_;
    int id_;
};
/** @brief A switch with the right-click menu. */
class Switch final : public juce::ToggleButton {
public:
    Switch(const ControlActions* actions, int id) : actions_(actions), id_(id) {}
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    int id() const { return id_; }

private:
    const ControlActions* actions_;
    int id_;
};

/**
 * @brief Keeps the "live" property of sliders up to date: where each one's value plays (normalised; NaN from the source:
 *        nothing to show). The look and feel draws it.
 */
class LiveRings final : private juce::Timer {
public:
    /** @brief @p playedNormalised gives the value store id @p id plays, normalised, or NaN. */
    explicit LiveRings(std::function<float(int)> playedNormalised);
    ~LiveRings() override { stopTimer(); }
    void add(juce::Slider& slider, int id);       ///< follows @p slider (it must outlive this object, or call clear)
    void clear() { items_.clear(); }

private:
    void timerCallback() override;
    std::function<float(int)> played_;
    std::vector<std::pair<juce::Component::SafePointer<juce::Slider>, int>> items_;
};

// ========================================================================================================= console

/**
 * @brief One strip of a console: the source's name and the sound it plays, its pan and sends, the fader beside the
 *        meter -- an RMS bar with a 300 ms release and a peak line that holds 1.5 s, then falls 20 dB a second (after
 *        Phosphene's and Ephemeris' mixers). Every control is the same host parameter as on the source's own page.
 */
class ChannelStrip final : public juce::Component, public juce::SettableTooltipClient {
public:
    /** @brief A control of the strip: its parameter, store id, the name under it, and whether it is a send. */
    struct Control {
        juce::RangedAudioParameter* param = nullptr;
        int id = -1;
        juce::String label;
        bool send = false;
    };
    ChannelStrip(const Skin& skin, const juce::String& name, juce::Colour colour, Control fader, std::vector<Control> knobs,
                 const ControlActions* actions, LiveRings* live);
    /** @brief A new reading (linear peak and RMS) covering @p seconds. */
    void meter(float peak, float rms, double seconds);
    void showSends(bool on) { sends_ = on; resized(); }
    void setSound(const juce::String& s);          ///< the sound it plays, under the name
    /** @brief A button under the name (a mute); the strip does not own it. */
    void setHeadButton(juce::Component* b);
    float peakDb() const { return holdDb_; }
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    const Skin& skin_;
    juce::String name_, sound_;
    juce::Colour colour_;
    bool sends_ = false;
    std::unique_ptr<Knob> fader_;
    std::unique_ptr<juce::SliderParameterAttachment> faderLink_;
    std::vector<std::unique_ptr<Knob>> knobs_;
    std::vector<std::unique_ptr<juce::Label>> names_;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> links_;
    std::vector<bool> isSend_;
    juce::Component* head_ = nullptr;
    juce::Rectangle<int> meterArea_;
    float rmsDb_ = -100.0f, holdDb_ = -100.0f;
    double holdAge_ = 0.0;
};

/**
 * @brief The strips side by side, a switch that unfolds the sends of all of them; where they would be narrower than a
 *        hand can use (kMinStrip), the console scrolls sideways, as a desk's does.
 */
class Console final : public juce::Component {
public:
    explicit Console(const Skin& skin);
    ChannelStrip& add(std::unique_ptr<ChannelStrip> strip);   ///< appends a strip
    int size() const { return static_cast<int>(strips_.size()); }
    ChannelStrip& strip(int i) { return *strips_[static_cast<size_t>(i)]; }
    void resized() override;
    static constexpr int kMinStrip = 64;     ///< the narrowest strip

private:
    const Skin& skin_;
    juce::TextButton fold_{ "Show sends" };
    bool sends_ = false;
    juce::Viewport view_;
    juce::Component inner_;
    std::vector<std::unique_ptr<ChannelStrip>> strips_;
};

// ========================================================================================================= headset

/** @brief The two hands as the Quest app sends them: height (0 low .. 1 high, 0.5 the middle), pinch, tracked. */
struct Hands {
    float height[2] = { 0.5f, 0.5f };   ///< left, right
    bool pinch[2] = { false, false };
    bool tracked[2] = { false, false };
};

/** @brief What the hands did since the last poll, in the grammar every generator's Quest app shares. */
struct HeadsetEvents {
    bool playStop = false;              ///< a short left pinch
    bool next = false;                  ///< both hands pinched together
    bool action = false;                ///< a short right pinch: the genre's action
    bool hold = false;                  ///< a right pinch held 0.6 s: its second
    bool holdEnded = false;             ///< that held pinch opened again (a momentary second action ends)
    bool filterMoved = false, throwMoved = false;
    float filter = 0.0f;                ///< -1 (low pass) .. 0 (open) .. 1 (high pass): the left hand's height
    float throwAmount = 0.0f;           ///< 0 .. 1: the right hand from the middle up
};

/**
 * @brief The headset's hands, over OSC (UDP): the Quest app in bridge mode sends "/hands" with six floats (left height,
 *        right height, left pinch, right pinch, left tracked, right tracked) about 30 times a second. Listens only while
 *        the settings do not say Off; active() while hands arrived in the last three seconds.
 */
class Headset final : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback> {
public:
    Headset();
    ~Headset() override;
    /** @brief Listens on @p port (0: not at all). Message thread. */
    void listen(int port);
    int port() const { return port_; }
    bool listening() const { return port_ > 0 && error_.isEmpty(); }
    juce::String error() const { return error_; }
    bool active() const;                          ///< hands arrived in the last three seconds
    bool everSeen() const { return messages_ > 0; }
    Hands hands() const { return hands_; }
    juce::String source() const { return source_; }   ///< who sent them last
    juce::int64 messages() const { return messages_; }
    /** @brief What the hands did since the last call (message thread, every frame of a timer). */
    HeadsetEvents poll();
    /** @brief A line on what it hears, for the settings and the Perform page. */
    juce::String statusText() const;
    /** @brief Whether the headset's controls should be shown under the settings' @p mode. */
    bool shown(Settings::HeadsetMode mode) const { return mode == Settings::HeadsetMode::On || (mode == Settings::HeadsetMode::Auto && active()); }
    /** @brief Feeds a message as if it had arrived (tests). */
    void inject(const Hands& h);

private:
    void oscMessageReceived(const juce::OSCMessage& m) override;
    juce::OSCReceiver receiver_;
    int port_ = 0;
    juce::String error_, source_;
    Hands hands_;
    juce::int64 messages_ = 0;
    double last_ = -1.0e9;                        ///< when hands last arrived (ms, Time::getMillisecondCounterHiRes)
    // The grammar's state.
    bool was_[2] = { false, false };              ///< pinching at the last poll
    bool spoiled_[2] = { true, true };            ///< the pinch counts no more (both hands, or a hold, took it)
    bool both_ = false;                           ///< both closed together: the next one has fired
    double since_[2] = { 0.0, 0.0 };              ///< when each pinch began (ms)
    bool holding_ = false;                        ///< the right pinch's hold fired and the pinch is still closed
    float filter_ = 0.0f, throw_ = 0.0f;          ///< the smoothed controls
    double lastPoll_ = 0.0;
};

/** @brief The grammar as text (the help). */
juce::String headsetGrammar(const juce::String& action, const juce::String& hold);
/** @brief The grammar in short lines, gesture and effect (the Perform page's headset box). */
std::vector<std::pair<juce::String, juce::String>> headsetGrammarShort(const juce::String& action, const juce::String& hold,
                                                                       const juce::String& left = "master filter",
                                                                       const juce::String& right = "echo throw");
/**
 * @brief The Perform page's headset box: its status, the two hands as they stand (a dot per hand on its height, lit while
 *        it pinches) and the grammar in short lines.
 */
void drawHeadsetBox(juce::Graphics& g, juce::Rectangle<int> area, const Skin& skin, const Headset& headset,
                    const juce::String& action, const juce::String& hold, const juce::String& left = "master filter",
                    const juce::String& right = "echo throw", bool framed = true);

} // namespace frame
