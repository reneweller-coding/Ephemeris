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
    int backdropSize = 0;   ///< the size of backdropData, bytes
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
    /** @brief A look and feel drawing with @p skin (it must outlive this object). */
    explicit LookAndFeel(const Skin& skin);
    const Skin& skin() const { return skin_; }   ///< the skin it draws with

    /**
     * @brief A knob: the skin's cap, its arc in the slider's fill colour from the start to the value, the value inside where
     *        the skin says so.
     */
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float pos, float startAngle, float endAngle,
                          juce::Slider&) override;
    /** @brief A fader or a horizontal slider: a thin track, the filled part in the slider's colour, a flat thumb. */
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float pos, float minPos, float maxPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    /** @brief A switch: a small pill, lit in its tick colour when on, the text beside it. */
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    /** @brief A chooser: the skin's raised box and a small arrow in the box's arrow colour. */
    void drawComboBox(juce::Graphics&, int width, int height, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    /** @brief A button: a flat rounded box, brighter while the pointer is over it, darker while pressed. */
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& background, bool highlighted, bool down) override;
    /** @brief A main tab: its name in the skin's tab font, the one in front underlined in the accent. */
    void drawTabButton(juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    /** @brief The tab bar's background: nothing but a line under the tabs. */
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override;
    /** @brief Nothing behind the tab in front (the line under the tabs is the bar background). */
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int, int) override;
    /** @brief A tab's width: its name at the tab font, with room on either side. */
    int getTabButtonBestWidth(juce::TabBarButton&, int tabDepth) override;
    /** @brief The skin's tab font at the height the bar gives. */
    juce::Font getTabButtonFont(juce::TabBarButton&, float height) override;
    /** @brief The skin's text font for a chooser. */
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    /** @brief The skin's text font for a button, a little smaller in a low one. */
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    /** @brief The skin's text font for the menus. */
    juce::Font getPopupMenuFont() override;
    /** @brief A slider's value box: centred, in the skin's ink, without the default box's frame. */
    juce::Label* createSliderTextBox(juce::Slider&) override;
    /** @brief A menu's background in the skin's panel colour with its edge. */
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    /** @brief A tooltip: the skin's raised colour, its ink, a rounded edge. */
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;

private:
    const Skin& skin_;   ///< the colours, fonts and shapes it draws with
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
    /** @brief A tab named @p name that never takes the keyboard focus (the keys stay the editor's). */
    explicit FlatTab(const juce::String& name) : juce::Button(name) { setWantsKeyboardFocus(false); }
    /** @brief Draws the tab as the frame's tab bar draws its tabs: the name, underlined in the accent when it is on. */
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    /** @brief The width the name needs at the frame's tab font. */
    int bestWidth() const;
};

/** @brief A small button with a drawn glyph (no font needed): undo, redo, help, settings, the ratings, play, stop. */
class IconButton final : public juce::Button {
public:
    /** @brief The glyphs it can draw. */
    enum class Icon { Undo, Redo, Help, Settings, ThumbUp, ThumbDown, Headset };
    /** @brief A button with glyph @p icon and @p tooltip; it never takes the keyboard focus. */
    IconButton(Icon icon, const juce::String& tooltip);
    /** @brief Draws another glyph from now on (a play button that becomes a stop button). */
    void setIcon(Icon icon) { icon_ = icon; repaint(); }
    /** @brief Draws the button's background as the look and feel draws a button, the glyph on it. */
    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    Icon icon_;   ///< the glyph drawn
};

// ========================================================================================================== header

/**
 * @brief The parts of the header, in their places (layoutHeader). Null parts are left out; widths of 0 take the
 *        default. The first row: logo, title, choices (style, key, scale), modes (connected buttons), length label and
 *        slider, actions (Compose, New seed), play, mute. The second: like, dislike, status, curation, the update notice,
 *        and the tools (undo, redo, help, settings) at its right end.
 */
struct Header {
    juce::Rectangle<float>* logo = nullptr;   ///< where the logo is drawn (filled by layoutHeader), or null
    juce::Component* title = nullptr;   ///< the instrument's name, drawn in the skin's title font
    int titleWidth = 0;   ///< the width the name needs (titleWidth())
    std::vector<std::pair<juce::Component*, int>> choices;   ///< the choosers of the first row (style, key, scale ...) with their widths
    /** @brief the buttons that choose what is composed (track or mix ...) with their widths, joined (connectModes) */
    std::vector<std::pair<juce::Component*, int>> modes;
    juce::Component* lengthLabel = nullptr;   ///< the length's name, or null
    juce::Component* length = nullptr;   ///< the length slider (the rest of the first row goes to it), or null
    std::vector<std::pair<juce::Component*, int>> actions;   ///< the actions of the first row (compose, new seed ...) with their widths
    juce::Component* play = nullptr;   ///< the play button
    juce::Component* mute = nullptr;   ///< the mute button, or null
    juce::Component* like = nullptr;   ///< the thumb up (the rating of what plays), or null
    juce::Component* dislike = nullptr;   ///< the thumb down, or null
    juce::Component* status = nullptr;   ///< the status line of the second row
    juce::Component* curation = nullptr;   ///< what is rerolled or locked, right of the status, or null
    juce::Component* update = nullptr;   ///< the link to a newer release, shown when there is one, or null
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
    juce::Image cache_;   ///< the picture scaled to the area it is drawn into
    juce::Rectangle<int> cachedFor_;   ///< the area cache_ was made for
    int cachedHeader_ = -1;   ///< the header's bottom cache_ was made for
    bool cachedOn_ = false;   ///< whether the picture was on when cache_ was made
};

// ======================================================================================================== settings

/**
 * @brief The instrument's settings: not the project's, the person's (application data/<name>/<name>.frame). They live
 *        until JUCE shuts down (a registry deleted at shutdown), never into a plugin's unloading.
 */
class Settings final : public juce::ChangeBroadcaster {
public:
    /** @brief Whether the headset's controls are shown: when a headset sends its hands, always, or never (nothing listens). */
    enum class HeadsetMode { Auto, On, Off };
    /** @brief The settings of instrument @p app (one object per name, message thread). */
    static Settings& of(const juce::String& app);
    /** @brief The settings of @p app, read from its file in the user's application data (made on first use). */
    explicit Settings(const juce::String& app);

    HeadsetMode headset() const;                  ///< Auto: shown when a headset sends; On: always; Off: never, nothing listens
    /** @brief Shows the headset's controls in mode @p m from now on; written at once. */
    void setHeadset(HeadsetMode m);
    int headsetPort() const;                      ///< the UDP port the hands arrive on (9100 + the instrument's offset)
    bool backdrop() const;                        ///< the picture behind the panel
    /** @brief Shows the picture behind the panel or not; written at once. */
    void setBackdrop(bool on);
    /** @brief The overview above the tabs (01.10.2026: folded away, the pages get its height). */
    bool overview() const;
    /** @brief Shows the overview above the tabs or folds it away; written at once. */
    void setOverview(bool on);
    /** @brief Ableton Link in the standalone (02.10.2026, LinkClock.h): tempo and bar phase with other apps; off by default. */
    bool link() const;
    /** @brief Joins the Link session or leaves it (the standalone reads it on its timer); written at once. */
    void setLink(bool on);
    /** @brief The instrument's name, as given to of(). */
    juce::String app() const { return app_; }

private:
    juce::String app_;   ///< the instrument's name
    std::unique_ptr<juce::PropertiesFile> file_;   ///< the settings file (<app>.frame in the user's application data)
};

/** @brief What the settings menu shows and does; empty functions leave their items out. */
struct SettingsMenu {
    juce::String app;   ///< the instrument's name: the menu's header and the settings it reads
    juce::String version;   ///< the version shown in the header and in About
    std::function<bool()> updatesOn;   ///< whether the update check is on (empty: no item)
    std::function<void(bool)> setUpdates;   ///< switches the update check
    std::function<bool()> canFullScreen;   ///< whether full screen is offered (the standalone only)
    std::function<bool()> isFullScreen;   ///< whether the window fills the screen now
    std::function<void()> toggleFullScreen;   ///< enters or leaves full screen
    std::function<void(float)> setWindowScale;    ///< 1.0: the design size
    std::function<juce::String()> headsetStatus;  ///< a line on what the headset receiver hears
    juce::String headsetOffText = "Off: do not listen";   ///< what Off means for this instrument
    std::function<void(juce::PopupMenu&)> headsetItems;   ///< more items in the headset's submenu (calibrate ...)
    std::function<void(juce::PopupMenu&)> moreItems;      ///< the instrument's own items, before Keys and About
    std::function<juce::String()> linkStatus;     ///< the standalone's Ableton Link: a line on the session (empty: no item)
    std::function<juce::String()> about;          ///< more lines for "About"
    std::function<void()> showAbout;              ///< the instrument's own About (replaces the frame's)
    /** @brief Shows the menu under @p target. */
    void show(juce::Component& target) const;
};

// ============================================================================================================ keys

/** @brief The actions behind the keys; an empty one leaves its key to whoever wants it. */
struct Keys {
    std::function<void()> playStop;   ///< Space: play or stop
    std::function<void()> undo;   ///< Ctrl+Z: undo
    std::function<void()> redo;   ///< Ctrl+Y and Ctrl+Shift+Z: redo
    std::function<void()> help;   ///< F1: the help
    std::function<void()> fullScreen;   ///< F11: full screen
    std::function<void()> escape;   ///< Esc: close the help, leave full screen
    std::function<void()> save;   ///< Ctrl+S: save the set
    std::function<void()> open;   ///< Ctrl+O: open a set
    std::function<void()> exportFile;   ///< Ctrl+E: export
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
    /**
     * @brief The help made of the manual's prose @p chapters (chapters.txt: "== Title | ... ==" lines and their paragraphs),
     *        drawn with @p skin.
     */
    HelpView(const juce::String& chapters, const Skin& skin);
    /** @brief Topics made each time the help opens (name, text), shown first. */
    std::function<std::vector<std::pair<juce::String, juce::String>>()> extraTopics;
    std::function<void()> onClose;                       ///< the close button or Esc
    void refresh();                                      ///< remakes the extra topics, keeps the one chosen
    /** @brief Shows topic @p index (the chapters first, then the extra topics). */
    void showTopic(int index);
    /** @brief The topics left, the text right, the close button at the top right. */
    void resized() override;
    /** @brief The panel's background behind the list and the text. */
    void paint(juce::Graphics& g) override;
    /** @brief Esc closes the help (onClose); the other keys go on to the editor. */
    bool keyPressed(const juce::KeyPress& key) override;

private:
    /** @brief How many topics there are (the list box asks). */
    int getNumRows() override { return names_.size(); }
    /** @brief A topic's name in the list, the chosen one in the accent. */
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    /** @brief Shows the topic chosen in the list. */
    void selectedRowsChanged(int lastRowSelected) override;
    const Skin& skin_;   ///< the colours and fonts it draws with
    std::vector<std::pair<juce::String, juce::String>> chapters_;   ///< the manual's chapters: title and text
    juce::StringArray names_;   ///< the topics in the list: the chapters, then the extra topics
    juce::StringArray texts_;   ///< the text of each topic
    juce::ListBox list_;   ///< the list of topics
    juce::TextEditor text_;   ///< the chosen topic's text, read only
    juce::TextButton close_{ "Close" };   ///< closes the help (onClose)
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
    /** @brief the rest of the state before the step, as the processor writes it (seed, rerolls ...) */
    juce::String extraBefore, extraAfter;                ///< the rest of the state, as the processor writes it
    /** @brief Whether the step changed nothing at all (it is not recorded then). */
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
    /** @brief Whether a step is open (begin() without its end()). */
    bool open() const { return depth_ > 0; }
    /** @brief Whether there is a step to undo. */
    bool canUndo() const { return !undo_.empty(); }
    /** @brief Whether there is an undone step to make again. */
    bool canRedo() const { return !redo_.empty(); }
    juce::String undoName() const { return undo_.empty() ? juce::String() : undo_.back().what; }   ///< the step undo takes back
    juce::String redoName() const { return redo_.empty() ? juce::String() : redo_.back().what; }   ///< the step redo makes again
    /** @brief Takes the last step back: apply its "before" (null: nothing to undo). */
    const UndoStep* undo();
    /** @brief Makes the last undone step again: apply its "after" (null: nothing to redo). */
    const UndoStep* redo();
    /** @brief Forgets every step (a new piece loaded, a state restored). */
    void clear() { undo_.clear(); redo_.clear(); }

private:
    static constexpr size_t kSteps = 200;   ///< the most steps kept; the oldest go first
    std::deque<UndoStep> undo_;   ///< the steps that can be undone, the newest at the back
    std::deque<UndoStep> redo_;   ///< the undone steps that can be made again, the newest at the back
    int depth_ = 0;   ///< how deeply begin() calls are nested (only the outermost end() records)
    juce::String what_;   ///< the open step's name
    juce::String extra_;   ///< the rest of the state when the step began
    std::vector<float> before_;   ///< every value when the step began
    std::vector<int> touched_;   ///< the store ids touch() named while the step was open
    bool all_ = false;   ///< every value may have changed (a preset, a new seed): compare all of them at end()
};

// ======================================================================================================== sub-tabs

/** @brief A row of small tabs above a page: one tab of the main row holding several pages (the synths of a group). */
class SubTabs final : public juce::Component {
public:
    /** @brief A row of small tabs drawn with @p skin; pages are added with add(). */
    explicit SubTabs(const Skin& skin) : skin_(skin) {}
    /** @brief A page, made the first time it is shown; @p colour marks its tab (transparent: the accent). */
    void add(const juce::String& name, std::function<std::unique_ptr<juce::Component>()> make,
             juce::Colour colour = juce::Colours::transparentBlack);
    void show(int index);                         ///< shows page @p index
    /** @brief The index of the page shown, -1 before any. */
    int current() const { return current_; }
    /** @brief How many pages there are. */
    int count() const { return static_cast<int>(pages_.size()); }
    /** @brief The name of page @p i. */
    juce::String name(int i) const { return pages_[static_cast<size_t>(i)].name; }
    juce::Component* page() const;               ///< the page shown (null before the first)
    std::function<void(int)> onChange;            ///< after a page was chosen
    /** @brief The row of tabs at the top, the page shown under it. */
    void resized() override;
    /** @brief The line under the row of tabs. */
    void paint(juce::Graphics& g) override;

private:
    /** @brief A page: its tab and how it is made the first time it is shown. */
    struct Page {
        juce::String name;   ///< the tab's name
        juce::Colour colour;   ///< the tab's colour when it is in front (transparent: the accent)
        std::function<std::unique_ptr<juce::Component>()> make;   ///< makes the page (called once, the first time it is shown)
        std::unique_ptr<juce::Component> comp;   ///< the page, once made
        std::unique_ptr<juce::TextButton> button;   ///< the page's tab
    };
    const Skin& skin_;   ///< the colours it draws with
    std::vector<Page> pages_;   ///< the pages, in the order of their tabs
    int current_ = -1;   ///< the page shown, -1 before any
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
    /** @brief A switch drawn with @p skin with a button per name in @p names; the first is chosen. */
    explicit SectionSwitch(const Skin& skin, const juce::StringArray& names = { "Sound", "Modulation" });
    /** @brief The section chosen. */
    int current() const { return current_; }
    /** @brief How many sections there are. */
    int count() const { return buttons_.size(); }
    /** @brief The name of section @p index. */
    juce::String name(int index) const { return buttons_[index] != nullptr ? buttons_[index]->getButtonText() : juce::String(); }
    void setCurrent(int index);                   ///< without onChange
    void setNames(const juce::StringArray& names);   ///< new sections (the current one kept where it still exists)
    std::function<void(int)> onChange;            ///< after a section was chosen
    int bestWidth() const;                        ///< the width its buttons need
    /** @brief The buttons side by side, each as wide as its name needs. */
    void resized() override;

private:
    const Skin& skin_;   ///< the colours it draws with
    juce::OwnedArray<juce::TextButton> buttons_;   ///< a button per section
    int current_ = 0;   ///< the section chosen
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
    /**
     * @brief A knob or fader of style @p style with its value box at @p box; a right click offers @p actions for store id @p
     *        id.
     */
    Knob(SliderStyle style, TextEntryBoxPosition box, const ControlActions* actions, int id);
    /** @brief A right click shows the menu (ControlActions::showMenu); any other click turns the knob. */
    void mouseDown(const juce::MouseEvent& e) override;
    /** @brief The store id it edits. */
    int id() const { return id_; }

private:
    const ControlActions* actions_;   ///< what its right-click menu does (null: no menu)
    int id_;   ///< the store id it edits
};
/** @brief A menu with the right-click menu. */
class Choice final : public juce::ComboBox {
public:
    /** @brief A chooser whose right click offers @p actions for store id @p id. */
    Choice(const ControlActions* actions, int id) : actions_(actions), id_(id) {}
    /** @brief A right click shows the menu (ControlActions::showMenu); any other click opens the list. */
    void mouseDown(const juce::MouseEvent& e) override;
    /** @brief The store id it edits. */
    int id() const { return id_; }

private:
    const ControlActions* actions_;   ///< what its right-click menu does (null: no menu)
    int id_;   ///< the store id it edits
};
/** @brief A switch with the right-click menu. */
class Switch final : public juce::ToggleButton {
public:
    /** @brief A switch whose right click offers @p actions for store id @p id. */
    Switch(const ControlActions* actions, int id) : actions_(actions), id_(id) {}
    /** @brief A right click shows the menu (ControlActions::showMenu) and does not toggle. */
    void mouseDown(const juce::MouseEvent& e) override;
    /** @brief Toggles on a left click only (a right click's release does nothing). */
    void mouseUp(const juce::MouseEvent& e) override;
    /** @brief The store id it edits. */
    int id() const { return id_; }

private:
    const ControlActions* actions_;   ///< what its right-click menu does (null: no menu)
    int id_;   ///< the store id it edits
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
    /** @brief Follows no slider any more (a page that rebuilds its controls). */
    void clear() { items_.clear(); }

private:
    /** @brief Reads what every followed knob plays at and sets its ring (the slider property "live"). */
    void timerCallback() override;
    std::function<float(int)> played_;   ///< the value a store id plays at, normalised 0..1 (the processor knows)
    /** @brief the followed sliders and their store ids (a slider deleted meanwhile is skipped) */
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
        juce::RangedAudioParameter* param = nullptr;   ///< the host parameter it edits
        int id = -1;   ///< its store id (the right-click menu)
        juce::String label;   ///< the name under it
        bool send = false;   ///< a send: shown only while the console shows the sends
    };
    /**
     * @brief A strip drawn with @p skin: @p name and its @p colour on top, @p fader, the @p knobs above it, a meter beside
     *        it.
     */
    ChannelStrip(const Skin& skin, const juce::String& name, juce::Colour colour, Control fader, std::vector<Control> knobs,
                 const ControlActions* actions, LiveRings* live);
    /** @brief A new reading (linear peak and RMS) covering @p seconds. */
    void meter(float peak, float rms, double seconds);
    /** @brief Shows the sends or folds them away (the console's Show sends). */
    void showSends(bool on) { sends_ = on; resized(); }
    void setSound(const juce::String& s);          ///< the sound it plays, under the name
    /** @brief A button under the name (a mute); the strip does not own it. */
    void setHeadButton(juce::Component* b);
    /** @brief The held peak, dB. */
    float peakDb() const { return holdDb_; }
    /** @brief The name, the sound, the meter (RMS, the held peak) and the scale. */
    void paint(juce::Graphics&) override;
    /** @brief The head button, the knobs (the sends only when shown), the fader and the meter. */
    void resized() override;

private:
    const Skin& skin_;   ///< the colours and fonts it draws with
    juce::String name_;   ///< the strip's name
    juce::String sound_;   ///< the sound its source plays (setSound)
    juce::Colour colour_;   ///< the source's family colour
    bool sends_ = false;   ///< the sends are shown
    std::unique_ptr<Knob> fader_;   ///< the level fader
    std::unique_ptr<juce::SliderParameterAttachment> faderLink_;   ///< the fader on its parameter
    std::vector<std::unique_ptr<Knob>> knobs_;   ///< the small knobs above the fader (pan, sends ...)
    std::vector<std::unique_ptr<juce::Label>> names_;   ///< the names under the knobs
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> links_;   ///< the knobs on their parameters
    std::vector<bool> isSend_;   ///< per knob: whether it is a send
    juce::Component* head_ = nullptr;   ///< the button under the name (a mute), not owned; null: none
    juce::Rectangle<int> meterArea_;   ///< where the meter is drawn
    float rmsDb_ = -100.0f;   ///< the RMS of the last reading, dB
    float holdDb_ = -100.0f;   ///< the held peak, dB (falls after holdAge_)
    double holdAge_ = 0.0;   ///< seconds since the held peak was set
};

/**
 * @brief The strips side by side, a switch that unfolds the sends of all of them; where they would be narrower than a
 *        hand can use (kMinStrip), the console scrolls sideways, as a desk's does.
 */
class Console final : public juce::Component {
public:
    /** @brief An empty console drawn with @p skin; strips are added with add(). */
    explicit Console(const Skin& skin);
    ChannelStrip& add(std::unique_ptr<ChannelStrip> strip);   ///< appends a strip
    /** @brief How many strips there are. */
    int size() const { return static_cast<int>(strips_.size()); }
    /** @brief Strip @p i. */
    ChannelStrip& strip(int i) { return *strips_[static_cast<size_t>(i)]; }
    /** @brief The fold button on top, the strips side by side (scrolled sideways where they are narrower than kMinStrip). */
    void resized() override;
    static constexpr int kMinStrip = 64;     ///< the narrowest strip

private:
    const Skin& skin_;   ///< the colours it draws with
    juce::TextButton fold_{ "Show sends" };   ///< shows or folds the sends of every strip
    bool sends_ = false;   ///< the sends are shown
    juce::Viewport view_;   ///< scrolls the strips sideways where the window is too narrow
    juce::Component inner_;   ///< holds the strips inside the viewport
    std::vector<std::unique_ptr<ChannelStrip>> strips_;   ///< the strips, left to right
};

// ========================================================================================================= headset

/** @brief The two hands as the Quest app sends them: height (0 low .. 1 high, 0.5 the middle), pinch, tracked. */
struct Hands {
    float height[2] = { 0.5f, 0.5f };   ///< left, right
    bool pinch[2] = { false, false };   ///< left, right: the thumb and the index finger touch
    bool tracked[2] = { false, false };   ///< left, right: the headset sees the hand
};

/** @brief What the hands did since the last poll, in the grammar every generator's Quest app shares. */
struct HeadsetEvents {
    bool playStop = false;              ///< a short left pinch
    bool next = false;                  ///< both hands pinched together
    bool action = false;                ///< a short right pinch: the genre's action
    bool hold = false;                  ///< a right pinch held 0.6 s: its second
    bool holdEnded = false;             ///< that held pinch opened again (a momentary second action ends)
    bool filterMoved = false;   ///< the filter control moved since the last poll
    bool throwMoved = false;   ///< the throw control moved since the last poll
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
    /** @brief The UDP port it listens on (0: none). */
    int port() const { return port_; }
    /** @brief Whether it listens (a port, and no error opening it). */
    bool listening() const { return port_ > 0 && error_.isEmpty(); }
    /** @brief Why it does not listen, empty when it does. */
    juce::String error() const { return error_; }
    bool active() const;                          ///< hands arrived in the last three seconds
    /** @brief Whether hands ever arrived. */
    bool everSeen() const { return messages_ > 0; }
    /** @brief The hands as they arrived last. */
    Hands hands() const { return hands_; }
    juce::String source() const { return source_; }   ///< who sent them last
    /** @brief How many messages arrived. */
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
    /** @brief Reads "/hands" with six floats (heights, pinches, tracked) into the hands (inject). */
    void oscMessageReceived(const juce::OSCMessage& m) override;
    juce::OSCReceiver receiver_;   ///< the OSC receiver on port_
    int port_ = 0;   ///< the port listened on, 0 none
    juce::String error_;   ///< why it does not listen (the port taken ...), empty when it does
    juce::String source_;   ///< who sent the hands last
    Hands hands_;   ///< the hands as they arrived last
    juce::int64 messages_ = 0;   ///< how many messages arrived
    double last_ = -1.0e9;                        ///< when hands last arrived (ms, Time::getMillisecondCounterHiRes)
    // The grammar's state.
    bool was_[2] = { false, false };              ///< pinching at the last poll
    bool spoiled_[2] = { true, true };            ///< the pinch counts no more (both hands, or a hold, took it)
    bool both_ = false;                           ///< both closed together: the next one has fired
    double since_[2] = { 0.0, 0.0 };              ///< when each pinch began (ms)
    bool holding_ = false;                        ///< the right pinch's hold fired and the pinch is still closed
    /** @brief the smoothed filter control, -1 .. 1 */
    float filter_ = 0.0f, throw_ = 0.0f;          ///< the smoothed controls
    double lastPoll_ = 0.0;   ///< when poll() ran last (ms; the smoothing's time step)
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

// ---------------------------------------------------------------------------------------------------------------------
// The keyboard's options (02.10.2026): what each generator's perform() does with a played key before the engine has it.
// All of them off by default -- a key plays as it always did.

/** @brief Pitch-class mask (bit k: k semitones above the root) of the scale named @p name; all twelve for an unknown. */
uint16_t scaleMask(const juce::String& name);
/** @brief The note of the scale (@p root 0..11, @p mask) nearest to @p pitch, a tie going down (Scale Lock). */
int snapToScale(int pitch, int root, uint16_t mask);
/** @brief The velocity curve names, in the order shapeVelocity() takes them. */
inline const char* const kVelocityCurveNames[] = { "As Played", "Soft", "Hard", "Fixed" };
/** @brief A played velocity (1..127) through a curve: 0 as played, 1 soft (a light touch louder), 2 hard, 3 fixed 100. */
int shapeVelocity(int velocity, int curve);

/** @brief What became of each held key -- the voice and the pitch its press went to --, so that its release goes there. */
class KeyMemory {
public:
    KeyMemory() { clear(); }
    /** @brief Forgets every held key. */
    void clear();
    /** @brief Key @p key on @p channel (0..15) was pressed and went to @p target (-1: the keyboard's own) as @p pitch. */
    void press(int channel, int key, int target, int pitch);
    /** @brief Where key @p key's press went; false when it is not held (then the release goes as it comes). */
    bool release(int channel, int key, int& target, int& pitch);

private:
    int16_t target_[16][128];   ///< by channel and key: the target its press went to, -2 for none held
    int16_t pitch_[16][128];    ///< by channel and key: the pitch its press went as
};

} // namespace frame
