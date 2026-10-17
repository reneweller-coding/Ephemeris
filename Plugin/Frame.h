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
 *   genre's action (held: its second), left hand's height the master filter, right hand's the echo throw;
 * - **the ways around the panel** (15.10.2026, from Empyrean): the standalone's window back where it was left
 *   (WindowKeeper), the command palette (Ctrl+K: any control, preset or action; CommandPalette, revealControl), the
 *   info line (what the control under the mouse does; InfoLine), the first steps (Tour), the preset browser
 *   (PresetBrowser) and modulation at the knob (ModulationAtKnob: arcs, the knob's menu, Alt-drag).
 *
 * Nothing here knows an engine: an editor hands its processor's functions in.
 *
 * @note Identical in Ephemeris, Totality, Parhelion and Phosphene (Plugin/Frame.h and Frame.cpp), and Noctuary uses its
 *       settings, keys and headset parts: a change is made in all of them.
 * @author Rene Weller
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
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace frame
{

// ===================================================================================================== the skin

/** @brief The families of function: the same colour means the same kind of control on every page of every generator. */
enum class FamilyE
{
    SOURCE,
    FILTER,
    ENVELOPE,
    MOTION,
    SPACE
};

/** @brief An instrument's look: its palette, its picture and its logo. */
struct Skin
{
    juce::String m_name;            ///< as the title shows it
    juce::Colour m_bg;              ///< the window
    juce::Colour m_panel;           ///< a page (translucent: the backdrop shows faintly through it)
    juce::Colour m_group;           ///< a group box
    juce::Colour m_raised;          ///< buttons, menus, the knobs' bodies
    juce::Colour m_edge;            ///< hairlines
    juce::Colour m_ink;             ///< text
    juce::Colour m_dim;             ///< names, secondary text
    juce::Colour m_faint;           ///< tracks, axes, the off state
    juce::Colour m_accent;          ///< the instrument's own colour: the title, the tab in front, the playhead's region
    juce::Colour m_onset;           ///< the playhead, a mute
    juce::Colour m_good;            ///< a meter in range
    juce::Colour m_bad;             ///< a meter over
    juce::Colour m_families[5];     ///< Source, Filter, Envelope, Motion, Space
    juce::Colour m_decks[3];        ///< deck A, B, C
    float        m_radius = 4.0f;   ///< corners of buttons, menus and boxes
    float        m_tracking = 0.06f;   ///< the title's letter spacing, of its height
    juce::String m_typeface;           ///< the title's typeface (empty: the panel's own)
    bool         m_titleBold = true;   ///< the title bold
    bool m_valueInKnob = false;        ///< a knob without a text box shows its value inside (Phosphene's dense pages)
    bool m_glow = false;               ///< arcs and the title drawn with a soft glow
    /**
     * @brief The picture behind the panel, as compressed bytes (BinaryData; null: none). Bytes and not a juce::Image: a
     *        skin is a static, and an image destroyed after JUCE has shut down (a plugin's unloading) hangs the host.
     */
    const void            *m_backdropData = nullptr;
    int                    m_backdropSize = 0;                 ///< the size of backdropData, bytes
    juce::Rectangle<float> m_crop{ 0.0f, 0.0f, 1.0f, 1.0f };   ///< the part of it used, normalised
    float                  m_backdropTop = 0.7f;               ///< its strength behind the header
    float                  m_backdropPage = 0.35f;             ///< behind the pages (under the translucent panel)
    /** @brief Draws the logo into a square. */
    std::function<void( juce::Graphics &, juce::Rectangle<float> )> m_logo;
    /**
     * @brief The backdrop's picture (from JUCE's image cache, which lives as long as JUCE does).
     * @return the backdrop's picture
     */
    juce::Image backdrop() const
    {
        return m_backdropData != nullptr ? juce::ImageCache::getFromMemory( m_backdropData, m_backdropSize )
                                         : juce::Image();
    }

    /**
     * a family's colour
     * @param[in] f  the family
     * @return its colour
     */
    juce::Colour family( FamilyE f ) const { return m_families[static_cast<int>( f )]; }
    /**
     * a deck's colour
     * @param[in] d  the deck
     * @return its colour
     */
    juce::Colour deck( int d ) const { return m_decks[juce::jlimit( 0, 2, d )]; }
};

// =============================================================================================== the look and feel

/**
 * @brief Every control drawn from a Skin. A slider with the property "live" (0..1, normalised) shows where its value
 *        plays (LiveRings): a thin bright arc from where it stands to there, and a dot.
 */
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    /**
     * @brief A look and feel drawing with @p skin (it must outlive this object).
     * @param[in] skin  the skin it draws with
     */
    explicit LookAndFeel( const Skin &skin );
    /**
     * the skin it draws with
     * @return the skin it draws with
     */
    const Skin &skin() const { return m_skin; }

    /**
     * @brief A knob: the skin's cap, its arc in the slider's fill colour from the start to the value, the value inside where
     *        the skin says so.
     * @param[in,out] g           the graphics context to draw into
     * @param[in]     x           the knob's left edge
     * @param[in]     y           its top edge
     * @param[in]     width       its width
     * @param[in]     height      its height
     * @param[in]     pos         the value's position, 0..1
     * @param[in]     startAngle  the angle of the lowest value in radians
     * @param[in]     endAngle    the angle of the highest value in radians
     * @param[in,out] slider      the knob
     */
    void drawRotarySlider( juce::Graphics &, int x, int y, int width, int height, float pos, float startAngle,
                           float endAngle, juce::Slider & ) override;
    /**
     * @brief A fader or a horizontal slider: a thin track, the filled part in the slider's colour, a flat thumb.
     * @param[in,out] g       the graphics context to draw into
     * @param[in]     x       the slider's left edge
     * @param[in]     y       its top edge
     * @param[in]     width   its width
     * @param[in]     height  its height
     * @param[in]     pos     the thumb's position in pixels
     * @param[in]     minPos  the position of the lowest value
     * @param[in]     maxPos  the position of the highest value
     * @param[in]     style   the slider's style
     * @param[in,out] slider  the slider
     */
    void drawLinearSlider( juce::Graphics &, int x, int y, int width, int height, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider & ) override;
    /**
     * @brief A switch: a small pill, lit in its tick colour when on, the text beside it.
     * @param[in,out] g            the graphics context to draw into
     * @param[in,out] b            the switch
     * @param[in]     highlighted  true while the pointer is over it
     * @param[in]     down         true while it is pressed
     */
    void drawToggleButton( juce::Graphics &, juce::ToggleButton &, bool highlighted, bool down ) override;
    /**
     * @brief A chooser: the skin's raised box and a small arrow in the box's arrow colour.
     * @param[in,out] g       the graphics context to draw into
     * @param[in]     width   the box's width
     * @param[in]     height  its height
     * @param[in]     down    true while it is pressed
     * @param[in]     bx      the arrow area's left edge
     * @param[in]     by      its top edge
     * @param[in]     bw      its width
     * @param[in]     bh      its height
     * @param[in,out] box     the chooser
     */
    void drawComboBox( juce::Graphics &, int width, int height, bool down, int bx, int by, int bw, int bh,
                       juce::ComboBox & ) override;
    /**
     * @brief A button: a flat rounded box, brighter while the pointer is over it, darker while pressed.
     * @param[in,out] g            the graphics context to draw into
     * @param[in,out] b            the button
     * @param[in]     background   the button's colour
     * @param[in]     highlighted  true while the pointer is over it
     * @param[in]     down         true while it is pressed
     */
    void drawButtonBackground( juce::Graphics &, juce::Button &, const juce::Colour &background, bool highlighted,
                               bool down ) override;
    /**
     * @brief A main tab: its name in the skin's tab font, the one in front underlined in the accent.
     * @param[in,out] b            the tab
     * @param[in,out] g            the graphics context to draw into
     * @param[in]     isMouseOver  true while the pointer is over it
     * @param[in]     isMouseDown  true while it is pressed
     */
    void drawTabButton( juce::TabBarButton &, juce::Graphics &, bool isMouseOver, bool isMouseDown ) override;
    /**
     * @brief The tab bar's background: nothing but a line under the tabs.
     * @param[in,out] bar  the tab bar
     * @param[in,out] g    the graphics context to draw into
     */
    void drawTabbedButtonBarBackground( juce::TabbedButtonBar &, juce::Graphics & ) override;
    /** @brief Nothing behind the tab in front (the line under the tabs is the bar background). */
    void drawTabAreaBehindFrontButton( juce::TabbedButtonBar &, juce::Graphics &, int, int ) override;
    /**
     * @brief A tab's width: its name at the tab font, with room on either side.
     * @param[in,out] b         the tab
     * @param[in]     tabDepth  the tab bar's depth
     * @return the tab's width in pixels
     */
    int getTabButtonBestWidth( juce::TabBarButton &, int tabDepth ) override;
    /**
     * @brief The skin's tab font at the height the bar gives.
     * @param[in] height  the height the bar gives
     * @return the tab font
     */
    juce::Font getTabButtonFont( juce::TabBarButton &, float height ) override;
    /**
     * @brief The skin's text font for a chooser.
     * @return the chooser's font
     */
    juce::Font getComboBoxFont( juce::ComboBox & ) override;
    /**
     * @brief The skin's text font for a button, a little smaller in a low one.
     * @param[in] buttonHeight  the button's height
     * @return the button's font
     */
    juce::Font getTextButtonFont( juce::TextButton &, int buttonHeight ) override;
    /**
     * @brief The skin's text font for the menus.
     * @return the menus' font
     */
    juce::Font getPopupMenuFont() override;
    /**
     * @brief A slider's value box: centred, in the skin's ink, without the default box's frame.
     * @param[in,out] slider  the slider
     * @return the value box, owned by the slider
     */
    juce::Label *createSliderTextBox( juce::Slider & ) override;
    /**
     * @brief A menu's background in the skin's panel colour with its edge.
     * @param[in,out] g       the graphics context to draw into
     * @param[in]     width   the menu's width
     * @param[in]     height  its height
     */
    void drawPopupMenuBackground( juce::Graphics &, int width, int height ) override;
    /**
     * @brief A tooltip: the skin's raised colour, its ink, a rounded edge.
     * @param[in,out] g       the graphics context to draw into
     * @param[in]     text    the tooltip's text
     * @param[in]     width   its width
     * @param[in]     height  its height
     */
    void drawTooltip( juce::Graphics &, const juce::String &text, int width, int height ) override;

private:
    const Skin &m_skin;   ///< the colours, fonts and shapes it draws with
};

/**
 * @brief Draws the instrument's name in the skin's title style (letter-spaced, glowing where the skin glows).
 * @param[in,out] g       the graphics context to draw into
 * @param[in]     skin    the skin
 * @param[in]     r       where the title goes
 * @param[in]     height  the title's height in pixels
 */
void drawTitle( juce::Graphics &g, const Skin &skin, juce::Rectangle<float> r, float height );
/**
 * @brief The width drawTitle needs for the name at @p height.
 * @param[in] skin    the skin
 * @param[in] height  the title's height in pixels
 * @return the title's width in pixels
 */
float titleWidth( const Skin &skin, float height );

// =========================================================================================================== icons

/**
 * @brief A tab of a row of tabs drawn as the frame's TabbedComponent draws its own (a generator whose pages are not a
 *        TabbedComponent -- Phosphene's -- uses these): the name, lit in the accent and underlined while it is open.
 */
class FlatTab final : public juce::Button
{
public:
    /**
     * @brief A tab named @p name that never takes the keyboard focus (the keys stay the editor's).
     * @param[in] name  the tab's name
     */
    explicit FlatTab( const juce::String &name ) : juce::Button( name ) { setWantsKeyboardFocus( false ); }
    /**
     * @brief Draws the tab as the frame's tab bar draws its tabs: the name, underlined in the accent when it is on.
     * @param[in,out] g            the graphics context to draw into
     * @param[in]     highlighted  true while the pointer is over it
     * @param[in]     down         true while it is pressed
     */
    void paintButton( juce::Graphics &g, bool highlighted, bool down ) override;
    /**
     * @brief The width the name needs at the frame's tab font.
     * @return the width in pixels
     */
    int bestWidth() const;
};

/**
 * @brief A small button with a drawn glyph (no font needed; a vector, sharp at every window size): the tools, the
 *        ratings, the transport and the files. Its words are its tooltip (and its name, for a screen reader) -- since
 *        03.10.2026 every unambiguous action of the family's panels is such a button.
 */
class IconButton final : public juce::Button
{
public:
    /** @brief The glyphs it can draw. */
    enum class IconE
    {
        UNDO,          ///< an arrow turning back
        REDO,          ///< an arrow turning on
        HELP,          ///< a question mark
        SETTINGS,      ///< a cog
        THUMB_UP,      ///< a raised thumb: I like this
        THUMB_DOWN,    ///< a lowered thumb: not this
        HEADSET,       ///< a visor: the Meta Quest
        PLAY,          ///< a triangle: start
        STOP,          ///< a square: stop
        RECORD,        ///< a red disc: record the output
        SPEAKER,       ///< a speaker sounding: the output on (pressed, it mutes)
        SPEAKER_OFF,   ///< a speaker crossed out: the output muted
        DICE,          ///< a die showing five: a new seed
        SAVE,          ///< a disk: save to a file
        OPEN,          ///< a folder: load from a file
        MIDI_LEARN,    ///< a MIDI socket: bind the next controller that moves
        SEARCH,        ///< a magnifier: the command palette (15.10.2026)
        NIGHT,         ///< a crescent: the night mode
        COMPARE        ///< a frame half filled (before | after): hold to hear the preset as it was
    };
    /**
     * @brief A button with glyph @p icon and @p tooltip; it never takes the keyboard focus.
     * @param[in] icon     the glyph
     * @param[in] tooltip  the tooltip
     */
    IconButton( IconE icon, const juce::String &tooltip );
    /**
     * @brief Draws another glyph from now on (a play button that becomes a stop button); repaints only on a change.
     * @param[in] icon  the glyph
     */
    void setIcon( IconE icon )
    {
        if ( icon == m_icon ) return;
        m_icon = icon;
        repaint();
    }
    /**
     * @brief Draws @p icon and says @p tooltip from now on; nothing happens when both are as they were (a timer may
     *         call it every tick).
     * @param[in] icon     the glyph
     * @param[in] tooltip  the tooltip
     */
    void show( IconE icon, const juce::String &tooltip )
    {
        setIcon( icon );
        if ( getTooltip() != tooltip ) setTooltip( tooltip );
    }
    /**
     * @brief The glyph it draws.
     * @return the glyph it draws
     */
    IconE icon() const { return m_icon; }
    /**
     * @brief Draws the button's background as the look and feel draws a button, the glyph on it.
     * @param[in,out] g            the graphics context to draw into
     * @param[in]     highlighted  true while the pointer is over it
     * @param[in]     down         true while it is pressed
     */
    void paintButton( juce::Graphics &g, bool highlighted, bool down ) override;

private:
    /**
     * @brief Draws the glyphs from Play on into the square @p r, in @p ink, lines with @p stroke.
     * @param[in,out] g       the graphics context to draw into
     * @param[in]     r       the square the glyph fills
     * @param[in]     ink     the glyph's colour
     * @param[in]     stroke  the stroke of its lines
     */
    void paintGlyph( juce::Graphics &g, juce::Rectangle<float> r, juce::Colour ink,
                     const juce::PathStrokeType &stroke ) const;

    IconE m_icon;   ///< the glyph drawn
};

/** @brief The width of an icon button in a header row, its margins included (Play, Mute, the die ...). */
constexpr int IconWidth = 42;

// ========================================================================================================== header

/**
 * @brief The parts of the header, in their places (layoutHeader). Null parts are left out; widths of 0 take the
 *        default. The first row: logo, title, choices (style, key, scale), modes (connected buttons), length label and
 *        slider, actions (Compose, New seed), play, mute. The second: like, dislike, status, curation, the update notice,
 *        and the tools (undo, redo, help, settings) at its right end.
 */
struct Header
{
    juce::Rectangle<float> *m_logo = nullptr;    ///< where the logo is drawn (filled by layoutHeader), or null
    juce::Component        *m_title = nullptr;   ///< the instrument's name, drawn in the skin's title font
    int                     m_titleWidth = 0;    ///< the width the name needs (titleWidth())
    std::vector<std::pair<juce::Component *, int>>
        m_choices;   ///< the choosers of the first row (style, key, scale ...) with their widths
    /** @brief the buttons that choose what is composed (track or mix ...) with their widths, joined (connectModes) */
    std::vector<std::pair<juce::Component *, int>> m_modes;
    juce::Component                               *m_lengthLabel = nullptr;   ///< the length's name, or null
    juce::Component *m_length = nullptr;   ///< the length slider (the rest of the first row goes to it), or null
    std::vector<std::pair<juce::Component *, int>>
                     m_actions;              ///< the actions of the first row (compose, new seed ...) with their widths
    juce::Component *m_play = nullptr;       ///< the play button
    juce::Component *m_mute = nullptr;       ///< the mute button, or null
    juce::Component *m_like = nullptr;       ///< the thumb up (the rating of what plays), or null
    juce::Component *m_dislike = nullptr;    ///< the thumb down, or null
    juce::Component *m_status = nullptr;     ///< the status line of the second row
    juce::Component *m_curation = nullptr;   ///< what is rerolled or locked, right of the status, or null
    juce::Component *m_update = nullptr;     ///< the link to a newer release, shown when there is one, or null
    std::vector<juce::Component *> m_tools;   ///< left to right; a null makes a gap between groups
};
constexpr int HeaderRow = 34;   ///< the first row's height
constexpr int StatusRow = 22;   ///< the second's
/**
 * @brief Lays the header out in the top of @p area (which loses the two rows and a gap).
 * @param[in,out] area  the editor's area; the header's rows are taken off its top
 * @param[in]     h     the header's components
 */
void layoutHeader( juce::Rectangle<int> &area, const Header &h );
/**
 * @brief Makes @p modes look like one control: connected edges, lit when chosen.
 * @param[in,out] modes  the buttons, one of which is on
 * @param[in]     on     the colour of the one that is on
 */
void connectModes( std::initializer_list<juce::Button *> modes, juce::Colour on );

// ======================================================================================================== backdrop

/** @brief The skin's picture behind the panel, cached at the window's size. */
class Backdrop
{
public:
    /**
     * @brief Paints the window's background into @p area: the bg colour, and with the picture on, the picture covering it,
     *        strong down to @p headerBottom and faint below, under a vignette.
     * @param[in,out] g             the graphics context to draw into
     * @param[in]     area          the window's area
     * @param[in]     headerBottom  where the header ends
     * @param[in]     skin          the skin
     * @param[in]     on            true to show the picture
     */
    void paint( juce::Graphics &g, juce::Rectangle<int> area, int headerBottom, const Skin &skin, bool on );

private:
    juce::Image          m_cache;               ///< the picture scaled to the area it is drawn into
    juce::Rectangle<int> m_cachedFor;           ///< the area m_cache was made for
    int                  m_cachedHeader = -1;   ///< the header's bottom m_cache was made for
    bool                 m_cachedOn = false;    ///< whether the picture was on when m_cache was made
};

// ======================================================================================================== settings

/**
 * @brief The instrument's settings: not the project's, the person's (application data/\<name\>/\<name\>.frame). They live
 *        until JUCE shuts down (a registry deleted at shutdown), never into a plugin's unloading.
 */
class Settings final : public juce::ChangeBroadcaster
{
public:
    /** @brief Whether the headset's controls are shown: when a headset sends its hands, always, or never (nothing listens). */
    enum class HeadsetModeE
    {
        AUTO,
        ON,
        OFF
    };
    /**
     * @brief The settings of instrument @p app (one object per name, message thread).
     * @param[in] app  the instrument's name
     * @return its settings
     */
    static Settings &of( const juce::String &app );
    /**
     * @brief The settings of @p app, read from its file in the user's application data (made on first use).
     * @param[in] app  the instrument's name
     */
    explicit Settings( const juce::String &app );

    /**
     * Auto: shown when a headset sends; On: always; Off: never, nothing listens
     * @return when the headset's controls are shown
     */
    HeadsetModeE headset() const;
    /**
     * @brief Shows the headset's controls in mode @p m from now on; written at once.
     * @param[in] m  the mode
     */
    void setHeadset( HeadsetModeE m );
    /**
     * the UDP port the hands arrive on (9100 + the instrument's offset)
     * @return the UDP port the hands arrive on
     */
    int headsetPort() const;
    /**
     * the picture behind the panel
     * @return true when the picture is shown behind the panel
     */
    bool backdrop() const;
    /**
     * @brief Shows the picture behind the panel or not; written at once.
     * @param[in] on  true to show the picture
     */
    void setBackdrop( bool on );
    /**
     * @brief The overview above the tabs (01.10.2026: folded away, the pages get its height).
     * @return true when the overview is shown above the tabs
     */
    bool overview() const;
    /**
     * @brief Shows the overview above the tabs or folds it away; written at once.
     * @param[in] on  true to show the overview
     */
    void setOverview( bool on );
    /**
     * @brief Ableton Link in the standalone (02.10.2026, LinkClock.h): tempo and bar phase with other apps; off by
     *        default.
     * @return true when the standalone joins Ableton Link
     */
    bool link() const;
    /**
     * @brief Joins the Link session or leaves it (the standalone reads it on its timer); written at once.
     * @param[in] on  true to join the Link session
     */
    void setLink( bool on );
    /**
     * @brief Score cues to a visualiser over OSC (02.10.2026; Noctuary's -- the others have parameters); off by
     *        default.
     * @return true when the score cues are sent
     */
    bool cues() const;
    /**
     * @brief Sends the score cues or stops; written at once.
     * @param[in] on  true to send the score cues
     */
    void setCues( bool on );
    /**
     * @brief The UDP port the score cues go to on this machine (9000, as the family's parameters say).
     * @return the UDP port the cues go to
     */
    int cuePort() const;
    /** @brief The instrument's part in the family jam (02.10.2026, Jam.h): none, leading the others, or following one. */
    enum class JamRoleE
    {
        OFF,
        LEAD,
        FOLLOW
    };
    /**
     * @brief Its part in the family jam; Off by default.
     * @return its part in the family jam
     */
    JamRoleE jamRole() const;
    /**
     * @brief Takes part in the family jam as @p r from now on; written at once.
     * @param[in] r  the part
     */
    void setJamRole( JamRoleE r );
    /**
     * @brief The instrument's name, as given to of().
     * @return the instrument's name
     */
    juce::String app() const { return m_app; }
    /**
     * @brief A value of the panel's own under @p key (15.10.2026: the window's state, the tour seen, the favourites and
     *        the recent presets), @p fallback when there is none.
     * @param[in] key       the key
     * @param[in] fallback  what comes back when nothing is stored
     * @return the stored value, or the fallback
     */
    juce::String value( const juce::String &key, const juce::String &fallback = {} ) const;
    /**
     * @brief Writes @p v under @p key (no change message: nothing on the panel follows it); @p now false lets the file's
     *        own timer write it a few seconds later (a window being dragged).
     * @param[in] key  the key
     * @param[in] v    the value
     * @param[in] now  true to write the file at once
     */
    void setValue( const juce::String &key, const juce::String &v, bool now = true );

private:
    juce::String m_app;   ///< the instrument's name
    std::unique_ptr<juce::PropertiesFile>
        m_file;   ///< the settings file (\<app\>.frame in the user's application data)
};

/** @brief What the settings menu shows and does; empty functions leave their items out. */
struct SettingsMenu
{
    juce::String                  m_app;         ///< the instrument's name: the menu's header and the settings it reads
    juce::String                  m_version;     ///< the version shown in the header and in About
    std::function<bool()>         m_updatesOn;   ///< whether the update check is on (empty: no item)
    std::function<void( bool )>   m_setUpdates;         ///< switches the update check
    std::function<bool()>         m_canFullScreen;      ///< whether full screen is offered (the standalone only)
    std::function<bool()>         m_isFullScreen;       ///< whether the window fills the screen now
    std::function<void()>         m_toggleFullScreen;   ///< enters or leaves full screen
    std::function<void( float )>  m_setWindowScale;     ///< 1.0: the design size
    std::function<juce::String()> m_headsetStatus;      ///< a line on what the headset receiver hears
    juce::String                  m_headsetOffText = "Off: do not listen";   ///< what Off means for this instrument
    std::function<void( juce::PopupMenu & )> m_headsetItems;   ///< more items in the headset's submenu (calibrate ...)
    std::function<void( juce::PopupMenu & )> m_moreItems;      ///< the instrument's own items, before Keys and About
    std::function<juce::String()>
        m_linkStatus;   ///< the standalone's Ableton Link: a line on the session (empty: no item)
    std::function<juce::String()> m_cueStatus;   ///< the score cues' switch, with a line on them (empty: no item)
    std::function<juce::String()>
        m_jamStatus;                         ///< the family jam's roles, with a line on what is heard (empty: no item)
    std::function<juce::String()> m_about;   ///< more lines for "About"
    std::function<void()>         m_showAbout;   ///< the instrument's own About (replaces the frame's)
    /**
     * @brief Shows the menu under @p target.
     * @param[in,out] target  the component the menu appears under
     */
    void show( juce::Component &target ) const;
};

// ============================================================================================================ keys

/** @brief The actions behind the keys; an empty one leaves its key to whoever wants it. */
struct Keys
{
    std::function<void()> m_playStop;     ///< Space: play or stop
    std::function<void()> m_undo;         ///< Ctrl+Z: undo
    std::function<void()> m_redo;         ///< Ctrl+Y and Ctrl+Shift+Z: redo
    std::function<void()> m_help;         ///< F1: the help
    std::function<void()> m_fullScreen;   ///< F11: full screen
    std::function<void()> m_escape;       ///< Esc: close the help, leave full screen
    std::function<void()> m_save;         ///< Ctrl+S: save the set
    std::function<void()> m_open;         ///< Ctrl+O: open a set
    std::function<void()> m_exportFile;   ///< Ctrl+E: export
    std::function<void()> m_palette;      ///< Ctrl+K: the command palette (15.10.2026)
};
/**
 * @brief Handles @p key as every generator does.
 * @param[in] key   the key pressed
 * @param[in] keys  what each of the frame's keys does
 * @return true when it was one of the frame's keys
 */
bool handleKey( const juce::KeyPress &key, const Keys &keys );
/**
 * @brief The keys in words (the settings menu and the help).
 * @return the keys and what they do, a line each
 */
juce::String keysText();
/**
 * @brief Buttons, switches and menus under @p root never take the keyboard focus: the keys stay the editor's.
 * @param[in,out] root  the editor
 */
void keepKeysForEditor( juce::Component &root );

// ============================================================================================================ help

/**
 * @brief The help: a list of topics and the text of the one chosen -- the manual's prose (chapters.txt: "== Title |
 *        modules | shot ==" starts a chapter, '#' a comment, blank lines part paragraphs, lines indented by four spaces
 *        stand as they are) and the topics the editor makes when it is opened.
 */
class HelpView final : public juce::Component, private juce::ListBoxModel
{
public:
    /**
     * @brief The help made of the manual's prose @p chapters (chapters.txt: "== Title | ... ==" lines and their paragraphs),
     *        drawn with @p skin.
     * @param[in] chapters  the manual's prose
     * @param[in] skin      the skin it draws with
     */
    HelpView( const juce::String &chapters, const Skin &skin );
    /** @brief Topics made each time the help opens (name, text), shown first. */
    std::function<std::vector<std::pair<juce::String, juce::String>>()> m_extraTopics;
    std::function<void()>                                               m_onClose;   ///< the close button or Esc
    void refresh();   ///< remakes the extra topics, keeps the one chosen
    /**
     * @brief Shows topic @p index (the chapters first, then the extra topics).
     * @param[in] index  the topic
     */
    void showTopic( int index );
    /** @brief The topics left, the text right, the close button at the top right. */
    void resized() override;
    /**
     * @brief The panel's background behind the list and the text.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override;
    /**
     * @brief Esc closes the help (onClose); the other keys go on to the editor.
     * @param[in] key  the key pressed
     * @return true when the key was used
     */
    bool keyPressed( const juce::KeyPress &key ) override;

private:
    /**
     * @brief How many topics there are (the list box asks).
     * @return how many topics there are
     */
    int getNumRows() override { return m_names.size(); }
    /**
     * @brief A topic's name in the list, the chosen one in the accent.
     * @param[in]     row       the row
     * @param[in,out] g         the graphics context to draw into
     * @param[in]     width     the row's width
     * @param[in]     height    the row's height
     * @param[in]     selected  true when the row is chosen
     */
    void paintListBoxItem( int row, juce::Graphics &g, int width, int height, bool selected ) override;
    /**
     * @brief Shows the topic chosen in the list.
     * @param[in] lastRowSelected  the row chosen
     */
    void selectedRowsChanged( int lastRowSelected ) override;
    const Skin                                        &m_skin;       ///< the colours and fonts it draws with
    std::vector<std::pair<juce::String, juce::String>> m_chapters;   ///< the manual's chapters: title and text
    juce::StringArray m_names;              ///< the topics in the list: the chapters, then the extra topics
    juce::StringArray m_texts;              ///< the text of each topic
    juce::ListBox     m_list;               ///< the list of topics
    juce::TextEditor  m_text;               ///< the chosen topic's text, read only
    juce::TextButton  m_close{ "Close" };   ///< closes the help (onClose)
};

// ============================================================================================================ undo

/**
 * @brief One step of undo: what changed and nothing else -- the values that moved (before, after) and the rest of the
 *        state as text (the seed, the rerolls). Only what changed, because the engine writes on the knobs by itself (a
 *        track's sounds when it begins): an undo must not take that back.
 */
struct UndoStep
{
    juce::String                               m_what;     ///< "Cutoff", "New seed", "reroll blocks"
    std::vector<std::tuple<int, float, float>> m_values;   ///< store id, before, after (real values)
    /** @brief the rest of the state before the step, as the processor writes it (seed, rerolls ...) */
    juce::String m_extraBefore, m_extraAfter;   ///< the rest of the state, as the processor writes it
    /**
     * @brief Whether the step changed nothing at all (it is not recorded then).
     * @return true when the step changed nothing
     */
    bool empty() const { return m_values.empty() && m_extraBefore == m_extraAfter; }
};

/**
 * @brief Undo and redo, and the bookkeeping of a step: a step opens (the knobs and the rest as they were), the knobs
 *        named in it may move, and it closes (what moved since is the step). Steps nest: a preset's twenty knobs, or a
 *        drag of two, are one step. Message thread only.
 */
class UndoHistory
{
public:
    /**
     * @brief Opens a step named @p what with the state as it is (@p values, @p extra); nested ones join the outer.
     * @param[in] what       the step's name
     * @param[in] values     the values as they are
     * @param[in] extra      the state beyond the values as it is
     * @param[in] allValues  true to record every value, not only those touched
     */
    void begin( const juce::String &what, std::vector<float> values, juce::String extra, bool allValues );
    /**
     * @brief Names a value the open step may record (a knob gesture's); a step opened with allValues takes every one.
     * @param[in] id  the store id
     */
    void touch( int id )
    {
        if ( m_depth > 0 ) m_touched.push_back( id );
    }
    /**
     * @brief Closes a step with the state as it is now; the outermost close records it, unless nothing changed.
     * @param[in] values  the values as they are now
     * @param[in] extra   the state beyond the values as it is now
     */
    void end( const std::vector<float> &values, const juce::String &extra );
    /**
     * @brief Whether a step is open (begin() without its end()).
     * @return true while a step is open
     */
    bool open() const { return m_depth > 0; }
    /**
     * @brief Whether there is a step to undo.
     * @return true when there is a step to undo
     */
    bool canUndo() const { return !m_undo.empty(); }
    /**
     * @brief Whether there is an undone step to make again.
     * @return true when there is an undone step to make again
     */
    bool canRedo() const { return !m_redo.empty(); }
    /**
     * the step undo takes back
     * @return the name of the step undo takes back
     */
    juce::String undoName() const { return m_undo.empty() ? juce::String() : m_undo.back().m_what; }
    /**
     * the step redo makes again
     * @return the name of the step redo makes again
     */
    juce::String redoName() const { return m_redo.empty() ? juce::String() : m_redo.back().m_what; }
    /**
     * @brief Takes the last step back: apply its "before" (null: nothing to undo).
     * @return the step to apply backwards, null when there is none
     */
    const UndoStep *undo();
    /**
     * @brief Makes the last undone step again: apply its "after" (null: nothing to redo).
     * @return the step to apply again, null when there is none
     */
    const UndoStep *redo();
    /** @brief Forgets every step (a new piece loaded, a state restored). */
    void clear()
    {
        m_undo.clear();
        m_redo.clear();
    }

private:
    static constexpr size_t Steps = 200;   ///< the most steps kept; the oldest go first
    std::deque<UndoStep>    m_undo;        ///< the steps that can be undone, the newest at the back
    std::deque<UndoStep>    m_redo;        ///< the undone steps that can be made again, the newest at the back
    int                     m_depth = 0;   ///< how deeply begin() calls are nested (only the outermost end() records)
    juce::String            m_what;        ///< the open step's name
    juce::String            m_extra;       ///< the rest of the state when the step began
    std::vector<float>      m_before;      ///< every value when the step began
    std::vector<int>        m_touched;     ///< the store ids touch() named while the step was open
    bool m_all = false;   ///< every value may have changed (a preset, a new seed): compare all of them at end()
};

// ======================================================================================================== sub-tabs

/**
 * @brief A part of the panel that can bring one of its children into view: a row of sub-tabs, a page split into
 *        sections, a list that scrolls. revealControl asks every such ancestor of a control in turn.
 */
class Revealer
{
public:
    virtual ~Revealer() = default;
    /**
     * @brief Brings @p part -- one of this component's descendants -- into view (its tab, its section).
     * @param[in,out] part  the descendant to bring into view
     */
    virtual void reveal( juce::Component &part ) = 0;
    /** @brief Makes every part it makes only when first shown (so a search finds them); nothing by default. */
    virtual void makeAll() {}
    /**
     * @brief Shows the control for store id @p id where it shows one of several at a time (an instance chooser: a lane
     *        of the kit); false when it has none for that id (the default).
     * @param[in] id  the store id
     * @return false when no control for that id is shown here
     */
    virtual bool bringId( int id )
    {
        (void)id;
        return false;
    }
};

/** @brief A row of small tabs above a page: one tab of the main row holding several pages (the synths of a group). */
class SubTabs final : public juce::Component, public Revealer
{
public:
    /**
     * @brief A row of small tabs drawn with @p skin; pages are added with add().
     * @param[in] skin  the skin it draws with
     */
    explicit SubTabs( const Skin &skin ) : m_skin( skin ) {}
    /**
     * @brief A page, made the first time it is shown; @p colour marks its tab (transparent: the accent).
     * @param[in] name    the page's name
     * @param[in] make    makes the page
     * @param[in] colour  its tab's mark
     */
    void add( const juce::String &name, std::function<std::unique_ptr<juce::Component>()> make,
              juce::Colour colour = juce::Colours::transparentBlack );
    /**
     * shows page @p index
     * @param[in] index  the page
     */
    void show( int index );
    /**
     * @brief The index of the page shown, -1 before any.
     * @return the index of the page shown, -1 before any
     */
    int current() const { return m_current; }
    /**
     * @brief How many pages there are.
     * @return how many pages there are
     */
    int count() const { return static_cast<int>( m_pages.size() ); }
    /**
     * @brief The name of page @p i.
     * @param[in] i  the page
     * @return its name
     */
    juce::String name( int i ) const { return m_pages[static_cast<size_t>( i )].m_name; }
    /**
     * the page shown (null before the first)
     * @return the page shown, null before the first
     */
    juce::Component *page() const;
    std::function<void( int )> m_onChange;   ///< after a page was chosen
    /** @brief The row of tabs at the top, the page shown under it. */
    void resized() override;
    /**
     * @brief The line under the row of tabs.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override;
    /**
     * @brief Shows the page holding @p part (revealControl).
     * @param[in,out] part  the descendant to bring into view
     */
    void reveal( juce::Component &part ) override;
    /** @brief Makes every page not yet made, hidden (revealControl's search finds their controls then). */
    void makeAll() override;

private:
    /** @brief A page: its tab and how it is made the first time it is shown. */
    struct Page
    {
        juce::String m_name;     ///< the tab's name
        juce::Colour m_colour;   ///< the tab's colour when it is in front (transparent: the accent)
        std::function<std::unique_ptr<juce::Component>()>
                                          m_make;     ///< makes the page (called once, the first time it is shown)
        std::unique_ptr<juce::Component>  m_comp;     ///< the page, once made
        std::unique_ptr<juce::TextButton> m_button;   ///< the page's tab
    };
    const Skin       &m_skin;           ///< the colours it draws with
    std::vector<Page> m_pages;          ///< the pages, in the order of their tabs
    int               m_current = -1;   ///< the page shown, -1 before any
};

// ======================================================================================================= sections

/**
 * @brief Whether a group of a synth's page belongs to its modulation -- the LFOs, the mod envelope, the matrix, the
 *        trance gate (01.10.2026). A page that does not fit the window shows its sound and its modulation as sections
 *        of their own (planSections).
 * @param[in] title  the group's title
 * @return true when the group belongs to the Modulation section
 */
bool isModulationGroup( const juce::String &title );

/** @brief A page's groups as sections, each to be shown alone, and the sections' names. */
struct SectionPlan
{
    std::vector<std::vector<int>> m_groups;   ///< per section the group indices, in the page's order
    juce::StringArray             m_names;    ///< per section its name for the switch
};

/**
 * @brief Splits a page's groups into sections that each fit @p available pixels (01.10.2026: no page scrolls at the
 *        window's usual size). One section when the whole page fits (or overshoots by no more than @p tolerance); else
 *        the sound's groups and the modulation's (isModulationGroup) apart, each in the page's order and cut where the
 *        next group would not fit any more. Names: "Sound" and "Modulation" for the first of each kind where the page
 *        has both ("Sound" also where it begins with a synth's presets), else the first group's title (without its
 *        number: "LFO 1" -> "LFO", "Matrix 1-2" -> "Matrix"). FAMILY_NO_SECTIONS=1 keeps every page whole (the
 *        manual's full-page pictures).
 * @param[in] titles     the groups' titles, in the page's order
 * @param[in] available  the height the page has
 * @param[in] height     the height the page needs showing only the given groups (laid out as the page lays them out)
 * @param[in] tolerance  the pixels a section may overflow before it is cut
 * @return the plan
 */
SectionPlan planSections( const juce::StringArray &titles, int available,
                          const std::function<int( const std::vector<int> & )> &height, int tolerance = 0 );

/** @brief The switch between a page's sections ("Sound" | "Modulation" | ...), drawn as the frame's small tabs. */
class SectionSwitch final : public juce::Component
{
public:
    /**
     * @brief A switch drawn with @p skin with a button per name in @p names; the first is chosen.
     * @param[in] skin   the skin it draws with
     * @param[in] names  the sections' names
     */
    explicit SectionSwitch( const Skin &skin, const juce::StringArray &names = { "Sound", "Modulation" } );
    /**
     * @brief The section chosen.
     * @return the section chosen
     */
    int current() const { return m_current; }
    /**
     * @brief How many sections there are.
     * @return how many sections there are
     */
    int count() const { return m_buttons.size(); }
    /**
     * @brief The name of section @p index.
     * @param[in] index  the section
     * @return its name
     */
    juce::String name( int index ) const
    {
        return m_buttons[index] != nullptr ? m_buttons[index]->getButtonText() : juce::String();
    }
    /**
     * without onChange
     * @param[in] index  the section
     */
    void setCurrent( int index );
    /**
     * new sections (the current one kept where it still exists)
     * @param[in] names  the sections' names
     */
    void setNames( const juce::StringArray &names );
    std::function<void( int )> m_onChange;   ///< after a section was chosen
    /**
     * the width its buttons need
     * @return the width its buttons need
     */
    int bestWidth() const;
    /** @brief The buttons side by side, each as wide as its name needs. */
    void resized() override;

private:
    const Skin                        &m_skin;          ///< the colours it draws with
    juce::OwnedArray<juce::TextButton> m_buttons;       ///< a button per section
    int                                m_current = 0;   ///< the section chosen
};

/**
 * @brief How far every page of @p tabs reaches past the window (its viewports' overflow in pixels), every small tab
 *        of a page of several too, one line each: "Synths: Lead\<TAB\>412". The pages are shown one after the other and
 *        the one in front again at the end. For the screenshot mode's <P>_PAGE_REPORT (01.10.2026): which page still
 *        scrolls at the window's usual size, and by how much.
 * @param[in,out] tabs  the editor's tabs
 * @return the report
 */
juce::String pageReport( juce::TabbedComponent &tabs );

/**
 * @brief The overflow of the viewports in @p c and below it (the largest), in pixels; 0 where nothing scrolls.
 * @param[in,out] c  the page
 * @return the pixels it overflows, 0 when it fits
 */
int pageOverflow( juce::Component &c );

/**
 * @brief One page's line of pageReport() -- and a line per section where it has a switch (Sound, Modulation).
 * @param[in,out] page  the page
 * @param[in]     name  its name
 * @return the report's lines of the page
 */
juce::String pageLines( juce::Component &page, const juce::String &name );

// ======================================================================================================== controls

/**
 * @brief One modulation on a knob, as the knob draws it (15.10.2026, after Empyrean): an arc round the knob over the span
 *        the source can move it, faint, and a dot where it moves it now -- in the source's colour.
 */
struct ModArc
{
    float        m_depth = 0.0f;       ///< how far it moves the knob, of the knob's whole range (-1 .. 1)
    bool         m_unipolar = false;   ///< the source swings 0 .. 1 (else -1 .. 1, both ways round the value)
    float        m_now = 0.0f;         ///< the source now (times a via source's amount)
    juce::Colour m_colour;             ///< the source's colour
};

/** @brief What a control's right-click menu does with a store id. */
struct ControlActions
{
    std::function<void( int )>         m_learn;           ///< binds the next MIDI controller to the id
    std::function<int( int )>          m_controllerFor;   ///< the controller bound to it, -1 none
    std::function<void( int )>         m_forget;          ///< unbinds it
    std::function<void( int )>         m_reset;           ///< back to its default
    std::function<juce::String( int )> m_describe;        ///< a line on it (the menu's header)
    /** @brief More items for id @p id, added after the others (an instrument's own: Empyrean's modulation at the knob). */
    std::function<void( juce::PopupMenu &, int )> m_more;
    /**
     * @brief An Alt-drag on knob @p id (an instrument's own: Empyrean sets a modulation's depth by it): phase 0 the
     *        press (return false to let the knob turn as usual), 1 each drag, 2 the release.
     */
    std::function<bool( int, const juce::MouseEvent &, int )> m_altDrag;
    /**
     * @brief Shows the menu for id @p id at @p target.
     * @param[in,out] target  the control clicked
     * @param[in]     id      its store id
     */
    void showMenu( juce::Component &target, int id ) const;
};
/** @brief A knob or fader with the right-click menu (ControlActions) and its store id. */
class Knob final : public juce::Slider
{
public:
    /**
     * @brief A knob or fader of style @p style with its value box at @p box; a right click offers @p actions for store id @p
     *        id.
     * @param[in] style    the slider style
     * @param[in] box      where the value box goes
     * @param[in] actions  what its right click offers
     * @param[in] id       the store id it edits
     */
    Knob( SliderStyle style, TextEntryBoxPosition box, const ControlActions *actions, int id );
    /**
     * @brief A right click shows the menu (ControlActions::showMenu); an Alt-drag goes to ControlActions::altDrag when
     *        it takes it; any other click turns the knob.
     * @param[in] e  the mouse event
     */
    void mouseDown( const juce::MouseEvent &e ) override;
    /**
     * @brief The drag: the knob's, or the Alt-drag's.
     * @param[in] e  the drag
     */
    void mouseDrag( const juce::MouseEvent &e ) override;
    /**
     * @brief The release: the knob's, or the Alt-drag's end.
     * @param[in] e  the release
     */
    void mouseUp( const juce::MouseEvent &e ) override;
    /**
     * @brief The store id it edits.
     * @return the store id it edits
     */
    int id() const { return m_id; }

private:
    const ControlActions *m_actions;               ///< what its right-click menu does (null: no menu)
    int                   m_id;                    ///< the store id it edits
    bool                  m_altDragging = false;   ///< an Alt-drag took this press
};
/** @brief A menu with the right-click menu. */
class Choice final : public juce::ComboBox
{
public:
    /**
     * @brief A chooser whose right click offers @p actions for store id @p id.
     * @param[in] actions  what its right click offers
     * @param[in] id       the store id it edits
     */
    Choice( const ControlActions *actions, int id ) : m_actions( actions ), m_id( id ) {}
    /**
     * @brief A right click shows the menu (ControlActions::showMenu); any other click opens the list.
     * @param[in] e  the mouse event
     */
    void mouseDown( const juce::MouseEvent &e ) override;
    /**
     * @brief The store id it edits.
     * @return the store id it edits
     */
    int id() const { return m_id; }

private:
    const ControlActions *m_actions;   ///< what its right-click menu does (null: no menu)
    int                   m_id;        ///< the store id it edits
};
/** @brief A switch with the right-click menu. */
class Switch final : public juce::ToggleButton
{
public:
    /**
     * @brief A switch whose right click offers @p actions for store id @p id.
     * @param[in] actions  what its right click offers
     * @param[in] id       the store id it edits
     */
    Switch( const ControlActions *actions, int id ) : m_actions( actions ), m_id( id ) {}
    /**
     * @brief A right click shows the menu (ControlActions::showMenu) and does not toggle.
     * @param[in] e  the mouse event
     */
    void mouseDown( const juce::MouseEvent &e ) override;
    /**
     * @brief Toggles on a left click only (a right click's release does nothing).
     * @param[in] e  the mouse event
     */
    void mouseUp( const juce::MouseEvent &e ) override;
    /**
     * @brief The store id it edits.
     * @return the store id it edits
     */
    int id() const { return m_id; }

private:
    const ControlActions *m_actions;   ///< what its right-click menu does (null: no menu)
    int                   m_id;        ///< the store id it edits
};

/**
 * @brief Keeps the "live" property of sliders up to date: where each one's value plays (normalised; NaN from the source:
 *        nothing to show). The look and feel draws it.
 */
class LiveRings final : private juce::Timer
{
public:
    /**
     * @brief @p playedNormalised gives the value store id @p id plays, normalised, or NaN.
     * @param[in] playedNormalised  gives the value a store id plays, normalised, or NaN
     */
    explicit LiveRings( std::function<float( int )> playedNormalised );
    ~LiveRings() override { stopTimer(); }
    /**
     * follows @p slider (it must outlive this object, or call clear)
     * @param[in,out] slider  the slider to follow
     * @param[in]     id      its store id
     */
    void add( juce::Slider &slider, int id );
    /** @brief Follows no slider any more (a page that rebuilds its controls). */
    void clear() { m_items.clear(); }
    /**
     * @brief The modulations on each followed knob (ModulationAtKnob), drawn as arcs (the slider property "mods").
     * @param[in] arcs  gives the modulation arcs of a store id
     */
    void setModArcs( std::function<std::vector<ModArc>( int )> arcs ) { m_arcs = std::move( arcs ); }

private:
    /** @brief Reads what every followed knob plays at and sets its ring (the slider property "live"), and its arcs. */
    void timerCallback() override;
    std::function<float( int )> m_played;   ///< the value a store id plays at, normalised 0..1 (the processor knows)
    std::function<std::vector<ModArc>( int )> m_arcs;   ///< the modulations on a store id (empty: none drawn)
    /** @brief the followed sliders and their store ids (a slider deleted meanwhile is skipped) */
    std::vector<std::pair<juce::Component::SafePointer<juce::Slider>, int>> m_items;
};

// ========================================================================================================= console

/**
 * @brief One strip of a console: the source's name and the sound it plays, its pan and sends, the fader beside the
 *        meter -- an RMS bar with a 300 ms release and a peak line that holds 1.5 s, then falls 20 dB a second (after
 *        Phosphene's and Ephemeris' mixers). Every control is the same host parameter as on the source's own page.
 */
class ChannelStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    /** @brief A control of the strip: its parameter, store id, the name under it, and whether it is a send. */
    struct Control
    {
        juce::RangedAudioParameter *m_param = nullptr;   ///< the host parameter it edits
        int                         m_id = -1;           ///< its store id (the right-click menu)
        juce::String                m_label;             ///< the name under it
        bool                        m_send = false;      ///< a send: shown only while the console shows the sends
    };
    /**
     * @brief A strip drawn with @p skin: @p name and its @p colour on top, @p fader, the @p knobs above it, a meter beside
     *        it.
     * @param[in]  skin     the skin it draws with
     * @param[in]  name     the strip's name
     * @param[in]  colour   its colour
     * @param[in]  fader    its fader
     * @param[in]  knobs    its knobs
     * @param[in]  actions  what the controls' right click offers
     * @param[out] live     where the knobs' played values show, or null
     */
    ChannelStrip( const Skin &skin, const juce::String &name, juce::Colour colour, Control fader,
                  std::vector<Control> knobs, const ControlActions *actions, LiveRings *live );
    /**
     * @brief A new reading (linear peak and RMS) covering @p seconds.
     * @param[in] peak     the linear peak
     * @param[in] rms      the linear RMS
     * @param[in] seconds  the time the reading covers
     */
    void meter( float peak, float rms, double seconds );
    /**
     * @brief Shows the sends or folds them away (the console's Show sends).
     * @param[in] on  true to show the sends
     */
    void showSends( bool on )
    {
        m_sends = on;
        resized();
    }
    /**
     * the sound it plays, under the name
     * @param[in] s  the sound's name
     */
    void setSound( const juce::String &s );
    /**
     * @brief A button under the name (a mute); the strip does not own it.
     * @param[in,out] b  the button, not owned; null for none
     */
    void setHeadButton( juce::Component *b );
    /**
     * @brief The held peak, dB.
     * @return the held peak in dB
     */
    float peakDb() const { return m_holdDb; }
    /**
     * @brief The name, the sound, the meter (RMS, the held peak) and the scale.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics & ) override;
    /** @brief The head button, the knobs (the sends only when shown), the fader and the meter. */
    void resized() override;

private:
    const Skin                                      &m_skin;            ///< the colours and fonts it draws with
    juce::String                                     m_name;            ///< the strip's name
    juce::String                                     m_sound;           ///< the sound its source plays (setSound)
    juce::Colour                                     m_colour;          ///< the source's family colour
    bool                                             m_sends = false;   ///< the sends are shown
    std::unique_ptr<Knob>                            m_fader;           ///< the level fader
    std::unique_ptr<juce::SliderParameterAttachment> m_faderLink;       ///< the fader on its parameter
    std::vector<std::unique_ptr<Knob>>               m_knobs;   ///< the small knobs above the fader (pan, sends ...)
    std::vector<std::unique_ptr<juce::Label>>        m_names;   ///< the names under the knobs
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> m_links;    ///< the knobs on their parameters
    std::vector<bool>                                             m_isSend;   ///< per knob: whether it is a send
    juce::Component     *m_head = nullptr;     ///< the button under the name (a mute), not owned; null: none
    juce::Rectangle<int> m_meterArea;          ///< where the meter is drawn
    float                m_rmsDb = -100.0f;    ///< the RMS of the last reading, dB
    float                m_holdDb = -100.0f;   ///< the held peak, dB (falls after m_holdAge)
    double               m_holdAge = 0.0;      ///< seconds since the held peak was set
};

/**
 * @brief The strips side by side, a switch that unfolds the sends of all of them; where they would be narrower than a
 *        hand can use (MinStrip), the console scrolls sideways, as a desk's does.
 */
class Console final : public juce::Component
{
public:
    /**
     * @brief An empty console drawn with @p skin; strips are added with add().
     * @param[in] skin  the skin it draws with
     */
    explicit Console( const Skin &skin );
    /**
     * appends a strip
     * @param[in] strip  the strip
     * @return the strip, now the console's
     */
    ChannelStrip &add( std::unique_ptr<ChannelStrip> strip );
    /**
     * @brief How many strips there are.
     * @return how many strips there are
     */
    int size() const { return static_cast<int>( m_strips.size() ); }
    /**
     * @brief Strip @p i.
     * @param[in] i  the strip's index
     * @return the strip
     */
    ChannelStrip &strip( int i ) { return *m_strips[static_cast<size_t>( i )]; }
    /** @brief The fold button on top, the strips side by side (scrolled sideways where they are narrower than MinStrip). */
    void resized() override;
    static constexpr int MinStrip = 64;   ///< the narrowest strip

private:
    const Skin                                &m_skin;                   ///< the colours it draws with
    juce::TextButton                           m_fold{ "Show sends" };   ///< shows or folds the sends of every strip
    bool                                       m_sends = false;          ///< the sends are shown
    juce::Viewport                             m_view;   ///< scrolls the strips sideways where the window is too narrow
    juce::Component                            m_inner;    ///< holds the strips inside the viewport
    std::vector<std::unique_ptr<ChannelStrip>> m_strips;   ///< the strips, left to right
};

// ========================================================================================================= headset

/** @brief The two hands as the Quest app sends them: height (0 low .. 1 high, 0.5 the middle), pinch, tracked. */
struct Hands
{
    float m_height[2] = { 0.5f, 0.5f };      ///< left, right
    bool  m_pinch[2] = { false, false };     ///< left, right: the thumb and the index finger touch
    bool  m_tracked[2] = { false, false };   ///< left, right: the headset sees the hand
};

/** @brief What the hands did since the last poll, in the grammar every generator's Quest app shares. */
struct HeadsetEvents
{
    bool  m_playStop = false;      ///< a short left pinch
    bool  m_next = false;          ///< both hands pinched together
    bool  m_action = false;        ///< a short right pinch: the genre's action
    bool  m_hold = false;          ///< a right pinch held 0.6 s: its second
    bool  m_holdEnded = false;     ///< that held pinch opened again (a momentary second action ends)
    bool  m_filterMoved = false;   ///< the filter control moved since the last poll
    bool  m_throwMoved = false;    ///< the throw control moved since the last poll
    float m_filter = 0.0f;         ///< -1 (low pass) .. 0 (open) .. 1 (high pass): the left hand's height
    float m_throwAmount = 0.0f;    ///< 0 .. 1: the right hand from the middle up
};

/**
 * @brief The headset's hands, over OSC (UDP): the Quest app in bridge mode sends "/hands" with six floats (left height,
 *        right height, left pinch, right pinch, left tracked, right tracked) about 30 times a second. Listens only while
 *        the settings do not say Off; active() while hands arrived in the last three seconds.
 */
class Headset final : private juce::OSCReceiver::Listener<juce::OSCReceiver::MessageLoopCallback>
{
public:
    Headset();
    ~Headset() override;
    /**
     * @brief Listens on @p port (0: not at all). Message thread.
     * @param[in] port  the UDP port, 0 for none
     * @pre called on the message thread
     */
    void listen( int port );
    /**
     * @brief The UDP port it listens on (0: none).
     * @return the UDP port it listens on, 0 for none
     */
    int port() const { return m_port; }
    /**
     * @brief Whether it listens (a port, and no error opening it).
     * @return true while it listens
     */
    bool listening() const { return m_port > 0 && m_error.isEmpty(); }
    /**
     * @brief Why it does not listen, empty when it does.
     * @return why it does not listen, empty when it does
     */
    juce::String error() const { return m_error; }
    /**
     * hands arrived in the last three seconds
     * @return true when hands arrived in the last three seconds
     */
    bool active() const;
    /**
     * @brief Whether hands ever arrived.
     * @return true when hands ever arrived
     */
    bool everSeen() const { return m_messages > 0; }
    /**
     * @brief The hands as they arrived last.
     * @return the hands as they arrived last
     */
    Hands hands() const { return m_hands; }
    /**
     * who sent them last
     * @return who sent them last
     */
    juce::String source() const { return m_source; }
    /**
     * @brief How many messages arrived.
     * @return how many messages arrived
     */
    juce::int64 messages() const { return m_messages; }
    /**
     * @brief What the hands did since the last call (message thread, every frame of a timer).
     * @return what the hands did since the last call
     * @pre called on the message thread
     */
    HeadsetEvents poll();
    /**
     * @brief A line on what it hears, for the settings and the Perform page.
     * @return the line
     */
    juce::String statusText() const;
    /**
     * @brief Whether the headset's controls should be shown under the settings' @p mode.
     * @param[in] mode  the settings' headset mode
     * @return true when the controls should be shown
     */
    bool shown( Settings::HeadsetModeE mode ) const
    {
        return mode == Settings::HeadsetModeE::ON || ( mode == Settings::HeadsetModeE::AUTO && active() );
    }
    /**
     * @brief Feeds a message as if it had arrived (tests).
     * @param[in] h  the hands
     */
    void inject( const Hands &h );

private:
    /**
     * @brief Reads "/hands" with six floats (heights, pinches, tracked) into the hands (inject).
     * @param[in] m  the OSC message
     */
    void oscMessageReceived( const juce::OSCMessage &m ) override;
    juce::OSCReceiver m_receiver;        ///< the OSC receiver on m_port
    int               m_port = 0;        ///< the port listened on, 0 none
    juce::String      m_error;           ///< why it does not listen (the port taken ...), empty when it does
    juce::String      m_source;          ///< who sent the hands last
    Hands             m_hands;           ///< the hands as they arrived last
    juce::int64       m_messages = 0;    ///< how many messages arrived
    double            m_last = -1.0e9;   ///< when hands last arrived (ms, Time::getMillisecondCounterHiRes)
    // The grammar's state.
    bool   m_was[2] = { false, false };     ///< pinching at the last poll
    bool   m_spoiled[2] = { true, true };   ///< the pinch counts no more (both hands, or a hold, took it)
    bool   m_both = false;                  ///< both closed together: the next one has fired
    double m_since[2] = { 0.0, 0.0 };       ///< when each pinch began (ms)
    bool   m_holding = false;               ///< the right pinch's hold fired and the pinch is still closed
    /** @brief the smoothed filter control, -1 .. 1 */
    float  m_filter = 0.0f, m_throw = 0.0f;   ///< the smoothed controls
    double m_lastPoll = 0.0;                  ///< when poll() ran last (ms; the smoothing's time step)
};

/**
 * @brief The grammar as text (the help).
 * @param[in] action  what the right pinch does
 * @param[in] hold    what holding the right pinch does
 * @return the grammar as text
 */
juce::String headsetGrammar( const juce::String &action, const juce::String &hold );
/**
 * @brief The grammar in short lines, gesture and effect (the Perform page's headset box).
 * @param[in] action  what the right pinch does
 * @param[in] hold    what holding the right pinch does
 * @param[in] left    what the left height moves
 * @param[in] right   what the right height moves
 * @return the moves and what they do
 */
std::vector<std::pair<juce::String, juce::String>> headsetGrammarShort( const juce::String &action,
                                                                        const juce::String &hold,
                                                                        const juce::String &left = "master filter",
                                                                        const juce::String &right = "echo throw" );
/**
 * @brief The Perform page's headset box: its status, the two hands as they stand (a dot per hand on its height, lit while
 *        it pinches) and the grammar in short lines.
 * @param[in,out] g        the graphics context to draw into
 * @param[in]     area     the box's area
 * @param[in]     skin     the skin
 * @param[in]     headset  the headset
 * @param[in]     action   what the right pinch does
 * @param[in]     hold     what holding the right pinch does
 * @param[in]     left     what the left height moves
 * @param[in]     right    what the right height moves
 * @param[in]     framed   true to draw the box's frame
 */
void drawHeadsetBox( juce::Graphics &g, juce::Rectangle<int> area, const Skin &skin, const Headset &headset,
                     const juce::String &action, const juce::String &hold, const juce::String &left = "master filter",
                     const juce::String &right = "echo throw", bool framed = true );

// ---------------------------------------------------------------------------------------------------------------------
// The keyboard's options (02.10.2026): what each generator's perform() does with a played key before the engine has it.
// All of them off by default -- a key plays as it always did.

/**
 * @brief Pitch-class mask (bit k: k semitones above the root) of the scale named @p name; all twelve for an unknown.
 * @param[in] name  the scale's name
 * @return its pitch classes as bits, 0 for none
 */
uint16_t scaleMask( const juce::String &name );
/**
 * @brief The note of the scale (@p root 0..11, @p mask) nearest to @p pitch, a tie going down (Scale Lock).
 * @param[in] pitch  the MIDI note
 * @param[in] root   the key's root, 0 .. 11
 * @param[in] mask   the scale's pitch classes as bits
 * @return the nearest note of the scale
 */
int snapToScale( int pitch, int root, uint16_t mask );
/** @brief The velocity curve names, in the order shapeVelocity() takes them. */
inline const char *const VelocityCurveNames[] = { "As Played", "Soft", "Hard", "Fixed" };
/**
 * @brief A played velocity (1..127) through a curve: 0 as played, 1 soft (a light touch louder), 2 hard, 3 fixed 100.
 * @param[in] velocity  the velocity, 1 .. 127
 * @param[in] curve     the curve
 * @return the shaped velocity
 */
int shapeVelocity( int velocity, int curve );

/** @brief What became of each held key -- the voice and the pitch its press went to --, so that its release goes there. */
class KeyMemory
{
public:
    KeyMemory() { clear(); }
    /** @brief Forgets every held key. */
    void clear();
    /**
     * @brief Key @p key on @p channel (0..15) was pressed and went to @p target (-1: the keyboard's own) as @p pitch.
     * @param[in] channel  the MIDI channel, 0..15
     * @param[in] key      the key's MIDI note
     * @param[in] target   where the press went, -1 for the keyboard's own
     * @param[in] pitch    the pitch it played
     */
    void press( int channel, int key, int target, int pitch );
    /**
     * @brief Where key @p key's press went; false when it is not held (then the release goes as it comes).
     * @param[in]  channel  the MIDI channel, 0..15
     * @param[in]  key      the key's MIDI note
     * @param[out] target   receives where its press went
     * @param[out] pitch    receives the pitch it played
     * @return false when the key is not held
     */
    bool release( int channel, int key, int &target, int &pitch );

private:
    int16_t m_target[16][128];   ///< by channel and key: the target its press went to, -2 for none held
    int16_t m_pitch[16][128];    ///< by channel and key: the pitch its press went as
};

// ========================================================================================================== window

/**
 * @brief The standalone's window comes back where it was left -- its place, its size, maximised or not -- and its moves
 *        are kept (15.10.2026, after Empyrean). JUCE's own window keeps only its corner and pushes the window wholly onto
 *        one screen; this keeps its whole state (ResizableWindow::getWindowStateAsString) in the instrument's settings
 *        ("window") whenever it moves, and brings it back once after the start, onto a screen should that one be gone.
 */
class WindowKeeper final : private juce::ComponentListener
{
public:
    WindowKeeper() = default;
    /** @brief Stops following the window. */
    ~WindowKeeper() override;
    /**
     * @brief Brings @p editor's standalone window back to where @p app's was left and keeps its moves from now on.
     *        Nothing in a host (its window is the host's), nothing a second time. Call it on the turn of the message
     *        loop after parentHierarchyChanged (the window is still putting its content in before).
     * @param[in,out] editor  the editor in the standalone's window
     * @param[in]     app     the instrument's name
     * @return true when it follows a window (now or before)
     */
    bool attach( juce::Component &editor, const juce::String &app );

private:
    /**
     * @brief The window moved or was resized: its state written (not while minimised or in full screen).
     * @param[in,out] c        the window
     * @param[in]     moved    true when it moved
     * @param[in]     resized  true when it was resized
     */
    void componentMovedOrResized( juce::Component &c, bool moved, bool resized ) override;
    juce::Component::SafePointer<juce::ResizableWindow>
                 m_window;   ///< the standalone's window, null before attach (and in a host)
    juce::String m_app;      ///< whose settings
};

// ========================================================================================================== reveal

/**
 * @brief The control for store id @p id under @p root, or null: a Knob, Choice or Switch of the frame, or any component
 *        carrying the property "frameId" (a panel's own controls: Phosphene's pages).
 * @param[out] root  where the search begins
 * @param[in]  id    the store id
 * @return the control, null when there is none
 */
juce::Component *findControl( juce::Component &root, int id );
/**
 * @brief The store id a control stands for (a Knob, Choice, Switch, or the property "frameId"), -1 for none.
 * @param[in] c  the component
 * @return its store id, -1 for none
 */
int controlId( const juce::Component &c );
/**
 * @brief Sets the modulation arcs a knob draws (the slider property "mods"), repainting it only when they changed.
 * @param[in,out] slider  the knob
 * @param[in]     arcs    its modulation arcs
 */
void showModArcs( juce::Slider &slider, const std::vector<ModArc> &arcs );

/**
 * @brief Shows a control: the page holding the one for store id @p id brought to the front (@p showPage), its
 *        sub-tab or section (Revealer), a viewport scrolled to it, and the control lit for a moment (flash).
 * @param[in] pages     the panel's pages (a tab's content each, shown or not)
 * @param[in] showPage  brings page i to the front
 * @param[in] id        the store id
 * @param[in] colour    the light's colour
 * @return false when no page has it
 */
bool revealControl( const std::vector<juce::Component *> &pages, const std::function<void( int )> &showPage, int id,
                    juce::Colour colour );
/**
 * @brief revealControl over a TabbedComponent's pages.
 * @param[in,out] tabs    the editor's tabs
 * @param[in]     id      the store id
 * @param[in]     colour  the colour it lights in
 * @return false when no page has it
 */
bool revealControl( juce::TabbedComponent &tabs, int id, juce::Colour colour );
/**
 * @brief Lights @p c for a moment: a ring round it in @p colour that fades (an overlay on its window, gone by itself).
 * @param[in,out] c       the component
 * @param[in]     colour  the ring's colour
 */
void flash( juce::Component &c, juce::Colour colour );

// ========================================================================================================= palette

/**
 * @brief The command palette (Ctrl+K, 15.10.2026, from Empyrean): type a few letters and find any control (its page
 *        opens and it lights up; "cutoff 800" sets it), any preset, or an action. The letters may be scattered: "mhd"
 *        finds Modal Hall Decay. Up and down choose, Enter runs, Esc closes; a click beside it closes it too.
 */
class CommandPalette final : public juce::Component,
                             private juce::ListBoxModel,
                             private juce::TextEditor::Listener,
                             private juce::KeyListener
{
public:
    /** @brief One thing it can do. */
    struct Command
    {
        juce::String m_title;                 ///< what it is called
        juce::String m_detail;                ///< where it is, or what it does
        juce::String m_kind;                  ///< "Control", "Preset", "Page", "Action", "Yours"
        bool         m_takesNumber = false;   ///< a number after its name is its value ("cutoff 800")
        std::function<void( const juce::String &number )>
            m_run;   ///< what it does (the number, "" when none was typed)
    };
    /**
     * @brief An empty palette drawn with @p skin; open() fills it.
     * @param[in] skin  the skin it draws with
     */
    explicit CommandPalette( const Skin &skin );
    /**
     * @brief Opens it over its parent with @p commands, the search empty and focused.
     * @param[in] commands  what it offers
     */
    void open( std::vector<Command> commands );
    /** @brief Commands made from the words typed, shown first (an instrument's own: presets by how they sound). */
    std::function<std::vector<Command>( const juce::StringArray & )> m_fromWords;
    /** @brief Closes it. */
    void close();
    /**
     * @brief Types into the search (the screenshot modes' way of showing it).
     * @param[in] text  the text typed
     */
    void type( const juce::String &text ) { m_search.setText( text, true ); }
    /**
     * @brief The dimmed window and the panel.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override;
    /** @brief The search on top, the list under it. */
    void resized() override;
    /**
     * @brief A click beside the panel closes it.
     * @param[in] e  the mouse event
     */
    void mouseDown( const juce::MouseEvent &e ) override;

private:
    /** @brief The commands that match the search, best first. */
    void filter();
    /**
     * @brief Runs a shown command and closes.
     * @param[in] row  the row of the command
     */
    void run( int row );
    /**
     * @brief The panel's rectangle.
     * @return the panel's rectangle
     */
    juce::Rectangle<int> panel() const;
    /**
     * @brief How many match.
     * @return how many commands match the search
     */
    int getNumRows() override;
    /**
     * @brief One row: the kind, the title, the detail.
     * @param[in]     row       the row
     * @param[in,out] g         the graphics context to draw into
     * @param[in]     width     the row's width
     * @param[in]     height    the row's height
     * @param[in]     selected  true when the row is chosen
     */
    void paintListBoxItem( int row, juce::Graphics &g, int width, int height, bool selected ) override;
    /**
     * @brief A click runs it.
     * @param[in] row  the row clicked
     * @param[in] e    the mouse event
     */
    void listBoxItemClicked( int row, const juce::MouseEvent &e ) override;
    /**
     * @brief Return runs the chosen one.
     * @param[in] row  the row chosen
     */
    void returnKeyPressed( int row ) override;
    /** @brief Typing filters. */
    void textEditorTextChanged( juce::TextEditor & ) override;
    /** @brief Return in the search runs the chosen one. */
    void textEditorReturnKeyPressed( juce::TextEditor & ) override;
    /** @brief Esc closes. */
    void textEditorEscapeKeyPressed( juce::TextEditor & ) override;
    /**
     * @brief Up and down (and the page keys) in the search move the choice.
     * @param[in]     key     the key pressed
     * @param[in,out] origin  the component that had the focus
     * @return true when the key was used
     */
    bool keyPressed( const juce::KeyPress &key, juce::Component *origin ) override;

    const Skin          &m_skin;        ///< the colours it draws with
    std::vector<Command> m_all;         ///< what it can do
    size_t               m_fixed = 0;   ///< how many of them were given to open() (the rest came from the words)
    std::vector<int>     m_shown;       ///< what matches, best first
    juce::String         m_number;      ///< a number typed after the words
    juce::TextEditor     m_search;      ///< the search
    juce::ListBox        m_list;        ///< the matches
};

// ======================================================================================================= info line

/**
 * @brief The info line at the bottom of the window (15.10.2026, from Empyrean; as Ableton's Info View): what the control
 *        under the mouse is -- its name, its value now, what moves it -- or, for any other control, its tooltip, without
 *        waiting for a tooltip or opening the manual.
 */
class InfoLine final : public juce::Component, private juce::Timer
{
public:
    /**
     * @brief A line drawn with @p skin that follows the mouse over @p watched and everything in it.
     * @param[in]     skin     the skin it draws with
     * @param[in,out] watched  the component whose controls it tells about
     */
    InfoLine( const Skin &skin, juce::Component &watched );
    /** @brief Stops following the mouse. */
    ~InfoLine() override;
    /**
     * @brief The words for store id @p id: its name into @p name, the rest (its value, what moves it, a sentence) into
     *        @p text; false for an id it does not know (the tooltip is shown then).
     */
    std::function<bool( int id, juce::String &name, juce::String &text )> m_describe;
    /**
     * @brief Tells about store id @p id without the mouse on it (the palette found it).
     * @param[in] id  the store id
     */
    void showControl( int id );
    /**
     * @brief Shows a line of its own (an Alt-drag's depth).
     * @param[in] text  the line
     */
    void showText( const juce::String &text );
    /**
     * @brief The name in the accent, the rest dim.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override;
    static constexpr int Height = 22;   ///< its height in a panel

private:
    /** @brief Follows the mouse over every component of the window (a listener of its own: the line is no parent of them). */
    struct Tracker final : public juce::MouseListener
    {
        InfoLine *m_line = nullptr;   ///< whose
        /**
         * @brief Over a component.
         * @param[in] e  the mouse event
         */
        void mouseMove( const juce::MouseEvent &e ) override { m_line->over( e.eventComponent ); }
        /**
         * @brief Into a component.
         * @param[in] e  the mouse event
         */
        void mouseEnter( const juce::MouseEvent &e ) override { m_line->over( e.eventComponent ); }
        /**
         * @brief Turning a knob: its value follows.
         * @param[in] e  the mouse event
         */
        void mouseDrag( const juce::MouseEvent &e ) override { m_line->over( e.eventComponent ); }
    };
    /**
     * @brief The line for a component: a control's, or its tooltip.
     * @param[in,out] c  the component under the mouse, or null
     */
    void over( juce::Component *c );
    /** @brief The value anew while the mouse rests on a control. */
    void timerCallback() override;

    const Skin      &m_skin;      ///< the colours it draws with
    juce::Component &m_watched;   ///< the window
    Tracker          m_tracker;   ///< the mouse's follower
    int              m_id = -1;   ///< the store id shown, -1 none
    juce::String     m_name;      ///< its name (nothing for a tooltip)
    juce::String     m_text;      ///< the rest of the line
};

// ============================================================================================================ tour

/**
 * @brief The first steps (15.10.2026, from Empyrean): a short tour on the first start, and whenever asked for -- one
 *        control at a time lit in a dimmed window with a sentence on it. Next goes on, Skip ends it.
 */
class Tour final : public juce::Component
{
public:
    /** @brief One step: what is lit, and what is said about it. */
    struct Step
    {
        juce::Component::SafePointer<juce::Component>
                     m_target;   ///< the control lit (null: none, the bubble in the middle)
        juce::String m_title;    ///< its name
        juce::String m_text;     ///< what it does
    };
    /**
     * @brief A tour drawn with @p skin, not yet shown.
     * @param[in] skin  the skin it draws with
     */
    explicit Tour( const Skin &skin );
    /** @brief Called when the tour ends (done or skipped). */
    std::function<void()> m_onDone;
    /**
     * @brief Begins it over its parent.
     * @param[in] steps  the tour's steps
     */
    void start( std::vector<Step> steps );
    /**
     * @brief Whether it is under way.
     * @return true while it is under way
     */
    bool running() const { return isVisible() && !m_steps.empty(); }
    /**
     * @brief The dimmed window with the lit control and the bubble.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override;
    /** @brief The bubble's buttons. */
    void resized() override;

private:
    /**
     * @brief The lit control's rectangle in this component (empty when there is none).
     * @return the lit control's rectangle, empty when there is none
     */
    juce::Rectangle<int> hole() const;
    /**
     * @brief The bubble's rectangle.
     * @return the bubble's rectangle
     */
    juce::Rectangle<int> bubble() const;
    /** @brief The next step, or the end. */
    void next();
    /** @brief Ends it. */
    void finish();

    const Skin       &m_skin;             ///< the colours it draws with
    std::vector<Step> m_steps;            ///< the steps
    size_t            m_at = 0;           ///< the one shown
    juce::TextButton  m_next{ "Next" };   ///< on
    juce::TextButton  m_skip{ "Skip" };   ///< ends it
};

// ================================================================================================= preset browser

/**
 * @brief The preset browser (15.10.2026, from Empyrean): a bank's presets -- the factory's and the player's own -- found
 *        by name, by section and group, by the favourites' star and the recently played. Hold a row's play mark to
 *        listen, double-click or Enter to take it. The favourites and the recent list are kept in the instrument's
 *        settings per bank.
 */
class PresetBrowser final : public juce::Component, private juce::ListBoxModel, private juce::TextEditor::Listener
{
public:
    /** @brief One preset as the browser lists it. */
    struct Entry
    {
        juce::String m_name;            ///< its name
        juce::String m_group;           ///< its group
        juce::String m_section;         ///< the section over its group (empty: none)
        int          m_index = -1;      ///< the instrument's own number for it (a bank's index, or one of the player's)
        bool         m_yours = false;   ///< one of the player's own
        juce::String m_note;            ///< a word after the group ("for the hands")
    };
    /** @brief A bank to browse and what to do with its presets. */
    struct Bank
    {
        juce::String                         m_title;   ///< shown on top ("Bass", "Sounds")
        juce::String                         m_key;     ///< the favourites' and the recent list's name in the settings
        std::vector<Entry>                   m_entries;      ///< its presets, in order
        std::function<void( const Entry & )> m_preview;      ///< while a row's play mark is held (empty: no mark)
        std::function<void()>                m_endPreview;   ///< the mark let go: back to the sound before
        std::function<void( const Entry & )> m_choose;       ///< takes a preset
    };
    /**
     * @brief A browser drawn with @p skin, keeping its lists in @p app's settings.
     * @param[in] skin  the skin it draws with
     * @param[in] app   the instrument whose settings keep the lists
     */
    PresetBrowser( const Skin &skin, const juce::String &app );
    /** @brief Lets go of the lists. */
    ~PresetBrowser() override;
    /**
     * @brief Opens it over its parent on @p bank.
     * @param[in] bank  the presets to show
     */
    void open( Bank bank );
    /** @brief Closes it (a preset listened to is let go). */
    void close();
    /**
     * @brief Notes that preset @p name of bank @p key was played (the recent list; a preset chosen elsewhere too).
     * @param[in] app   the instrument
     * @param[in] key   the bank's key
     * @param[in] name  the preset's name
     */
    static void played( const juce::String &app, const juce::String &key, const juce::String &name );
    /**
     * @brief The dimmed window and the panel.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override;
    /** @brief The search, the categories, the list. */
    void resized() override;
    /**
     * @brief A click beside the panel closes it.
     * @param[in] e  the mouse event
     */
    void mouseDown( const juce::MouseEvent &e ) override;
    /**
     * @brief Esc closes, Enter takes the chosen one.
     * @param[in] key  the key pressed
     * @return true when the key was used
     */
    bool keyPressed( const juce::KeyPress &key ) override;

private:
    /** @brief One category on the left: All, Favourites, Recent, Yours, a section or a group. */
    struct Category
    {
        juce::String m_title;      ///< as listed
        int          m_kind = 0;   ///< 0 all, 1 favourites, 2 recent, 3 yours, 4 a section, 5 a group
        juce::String m_which;      ///< the section's or the group's name
        juce::String m_section;    ///< a group's section (empty: the group wherever it is)
    };
    class Row;          ///< a row of the list: the play mark, the star, the name, the group
    class Categories;   ///< the categories' list

    /** @brief The entries that pass every filter, in order. */
    void filter();
    /**
     * @brief Takes a shown entry.
     * @param[in] row  the row of the entry
     */
    void choose( int row );
    /**
     * @brief The panel's rectangle.
     * @return the panel's rectangle
     */
    juce::Rectangle<int> panel() const;
    /**
     * @brief Whether @p name is a favourite.
     * @param[in] name  the preset's name
     * @return true when it is a favourite
     */
    bool favourite( const juce::String &name ) const;
    /**
     * @brief Stars @p name or takes its star away.
     * @param[in] name  the preset's name
     * @param[in] on    true to star it
     */
    void setFavourite( const juce::String &name, bool on );
    /**
     * @brief How many pass.
     * @return how many entries pass the filter
     */
    int getNumRows() override;
    /** @brief Unused: the rows are components (Row). */
    void paintListBoxItem( int, juce::Graphics &, int, int, bool ) override {}
    /**
     * @brief A row's component.
     * @param[in]     row       the row
     * @param[in]     selected  true when the row is chosen
     * @param[in,out] existing  the component the list had there, or null
     * @return the row's component
     */
    juce::Component *refreshComponentForRow( int row, bool selected, juce::Component *existing ) override;
    /**
     * @brief Double-click takes it.
     * @param[in] row  the row
     */
    void listBoxItemDoubleClicked( int row, const juce::MouseEvent & ) override { choose( row ); }
    /**
     * @brief Return takes it.
     * @param[in] row  the row chosen
     */
    void returnKeyPressed( int row ) override { choose( row ); }
    /** @brief Typing filters. */
    void textEditorTextChanged( juce::TextEditor & ) override { filter(); }
    /** @brief Return in the search takes the chosen one. */
    void textEditorReturnKeyPressed( juce::TextEditor & ) override { choose( m_list.getSelectedRow() ); }
    /** @brief Esc closes. */
    void textEditorEscapeKeyPressed( juce::TextEditor & ) override { close(); }

    const Skin           &m_skin;           ///< the colours it draws with
    juce::String          m_app;            ///< whose settings
    Bank                  m_bank;           ///< the bank open
    juce::StringArray     m_favourites;     ///< its favourites (kept in the settings)
    std::vector<int>      m_shown;          ///< the entries that pass
    std::vector<Category> m_categories;     ///< the categories
    int                   m_category = 0;   ///< the one chosen
    juce::TextEditor      m_search;         ///< by name or group
    juce::Label           m_title;          ///< the bank's title
    std::unique_ptr<Categories>
        m_categoryModel;   ///< the categories' list's model (made before the list, gone after it)
    std::unique_ptr<juce::ListBox> m_categoryList;       ///< the categories' list
    juce::TextButton               m_take{ "Take" };     ///< takes the chosen one
    juce::TextButton               m_close{ "Close" };   ///< closes
    juce::Label                    m_count;              ///< how many pass
    juce::ListBox                  m_list;               ///< the presets that pass
};

// ============================================================================================= modulation at the knob

/**
 * @brief What an instrument's modulation matrix offers the panel (15.10.2026, after Empyrean's KnobMod): the rows on a
 *        store id, their depths, the sources that can be added. ModulationAtKnob makes the arcs, the knob's menu and
 *        the Alt-drag of it.
 */
struct ModAccess
{
    /** @brief A row of the matrix on a store id. */
    struct Row
    {
        int          m_index = -1;     ///< the row's number in the instrument's matrix
        juce::String m_name;           ///< its source (and via source)
        juce::Colour m_colour;         ///< its source's colour
        float        m_depth = 0.0f;   ///< its depth, of the knob's range (-1 .. 1)
    };
    /** @brief A source that can be added, in a section of the "Modulate with" menu. */
    struct Source
    {
        int          m_id = 0;    ///< the instrument's number for it
        juce::String m_name;      ///< its name
        juce::String m_section;   ///< its section's heading
    };
    std::function<std::vector<Row>( int id )>    m_rowsOn;     ///< the rows on a store id
    std::function<void( int row, float depth )>  m_setDepth;   ///< a row's depth anew
    std::function<void( int row )>               m_remove;     ///< takes a row out
    std::function<std::vector<Source>( int id )> m_sources;   ///< what can modulate a store id (empty: it is no target)
    std::function<bool( int id, int source )>    m_add;    ///< a new row of @p source on @p id; false when none is free
    std::function<std::vector<ModArc>( int id )> m_arcs;   ///< the arcs on a store id now
    std::function<void( const juce::String & )>  m_say;    ///< shows a line (the info line: an Alt-drag's depth)
    std::function<void( const juce::String & )>
                          m_begin;   ///< opens one undo step (a depth dragged: one step, not one per pixel)
    std::function<void()> m_end;     ///< closes it
};

namespace detail
{
/**
 * @brief Letters and digits only, in lower case: two names compared loosely.
 * @param[in] s  the name
 * @return its letters and digits in lower case
 */
inline juce::String loose( const juce::String &s )
{
    return s.toLowerCase().retainCharacters( "abcdefghijklmnopqrstuvwxyz0123456789" );
}
/**
 * @brief Whether a source swings both ways (an LFO, the key, a random value) rather than 0 .. 1.
 * @param[in] name  the source's name
 * @return true when it swings both ways
 */
inline bool bipolarSource( const juce::String &name )
{
    const juce::String l = name.toLowerCase();
    return l.contains( "lfo" ) || l.contains( "key" ) || l.contains( "random" );
}
/**
 * @brief A source's colour by its kind: an LFO the motion's, an envelope the envelope's, the note's the source's, the
 *        rest the space's.
 * @param[in] name  the source's name
 * @param[in] skin  the skin
 * @return the source's colour
 */
inline juce::Colour sourceColour( const juce::String &name, const Skin &skin )
{
    const juce::String l = name.toLowerCase();
    if ( l.contains( "lfo" ) ) return skin.family( FamilyE::MOTION );
    if ( l.contains( "env" ) ) return skin.family( FamilyE::ENVELOPE );
    if ( l.contains( "vel" ) || l.contains( "key" ) || l.contains( "random" ) || l.contains( "note" ) )
        return skin.family( FamilyE::SOURCE );
    return skin.family( FamilyE::SPACE );
}
/**
 * @brief A source's section in the "Modulate with" menu.
 * @param[in] name  the source's name
 * @return its section in the menu
 */
inline juce::String sourceSection( const juce::String &name )
{
    const juce::String l = name.toLowerCase();
    if ( l.contains( "lfo" ) ) return "LFOs";
    if ( l.contains( "env" ) ) return "Envelopes";
    if ( l.contains( "vel" ) || l.contains( "key" ) || l.contains( "random" ) || l.contains( "note" ) )
        return "The note";
    return "The hand and the track";
}
}   // namespace detail

/**
 * @brief ModAccess for a voice's own matrix as the generators have it (Ephemeris' Modulation.h, then Phosphene's and
 *        Parhelion's): per module instance eight (or more) slots of three knobs -- "mod\<n\>_src", "_dst", "_amount" (or
 *        "mx\<n\>_...") --, the source a menu of the sources, the destination a menu of the voice's targets. A knob is a
 *        target where one of those targets is named as it is ("Cutoff", "Bow Pressure"); its rows are the slots on that
 *        target. A template, because each generator's ParamStore is its own (the same shape: count, desc, key, find,
 *        get, getInt).
 * @param[in,out] store  the parameters
 * @param[in]     set    sets a knob (store id, real value) as the panel does (through the host; inside begin/end one
 *                       step)
 * @param[in]     begin  opens an undo step
 * @param[in]     end    closes it
 * @param[in]     skin   the colours of the sources
 * @return the access to the matrix
 */
template <class StoreT>
ModAccess slotMatrix( StoreT &store, std::function<void( int, float )> set,
                      std::function<void( const juce::String & )> begin, std::function<void()> end, const Skin &skin )
{
    // The slots' first knobs (their sources) per module instance, found once.
    auto cache = std::make_shared<std::map<std::string, std::vector<int>>>();
    auto slotsOf = [&store, cache]( int id ) -> std::vector<int>
    {
        if ( id < 0 || id >= store.count() ) return {};
        const std::string &k = store.key( id );
        const std::string  prefix = k.substr( 0, k.find( '.' ) );
        if ( auto it = cache->find( prefix ); it != cache->end() ) return it->second;
        std::vector<int> out;
        for ( const char *stem : { ".mx", ".mod" } )
        {
            for ( int n = 1; n <= 32; ++n )
            {
                const int src = store.find( prefix + stem + std::to_string( n ) + "_src" );
                if ( src < 0 || src + 2 >= store.count() ) break;
                const std::string &d = store.key( src + 1 );
                if ( d.size() < 4 || d.compare( d.size() - 4, 4, "_dst" ) != 0 ) break;
                out.push_back( src );
            }
            if ( !out.empty() ) break;
        }
        ( *cache )[prefix] = out;
        return out;
    };
    // The target a knob is: the destination named as the knob is (-1: none).
    auto targetOf = [&store, slotsOf]( int id ) -> int
    {
        const std::vector<int> slots = slotsOf( id );
        if ( slots.empty() ) return -1;
        for ( int s : slots )
            if ( id >= s && id <= s + 2 ) return -1;   // a slot's own knob
        const auto &dd = store.desc( slots.front() + 1 );
        if ( dd.m_choices == nullptr ) return -1;
        const juce::String knob = detail::loose( store.desc( id ).m_name );
        if ( knob.isEmpty() ) return -1;
        int best = -1, bestScore = 0;
        for ( int t = 1; t <= static_cast<int>( dd.m_maxValue ); ++t )
        {
            const juce::String tn = detail::loose( dd.m_choices[t] );
            if ( tn.isEmpty() ) continue;
            const int score = knob == tn                                                             ? 3
                              : knob.endsWith( tn ) || ( tn.length() >= 4 && knob.startsWith( tn ) ) ? 2
                              : ( knob.length() >= 4 && tn.endsWith( knob ) )                        ? 1
                                                                                                     : 0;
            if ( score > bestScore )
            {
                bestScore = score;
                best = t;
            }
        }
        return best;
    };
    auto sourceName = [&store]( int slot, int src )
    {
        const auto &sd = store.desc( slot );
        return sd.m_choices != nullptr && src >= 0 && src <= static_cast<int>( sd.m_maxValue )
                   ? juce::String( sd.m_choices[src] )
                   : juce::String( src );
    };
    ModAccess a;
    a.m_begin = begin;
    a.m_end = end;
    a.m_rowsOn = [&store, slotsOf, targetOf, sourceName, &skin]( int id )
    {
        std::vector<ModAccess::Row> rows;
        const int                   t = targetOf( id );
        if ( t < 1 ) return rows;
        for ( int s : slotsOf( id ) )
        {
            const int src = store.getInt( s );
            if ( src <= 0 || store.getInt( s + 1 ) != t ) continue;
            const juce::String name = sourceName( s, src );
            rows.push_back( { s, name, detail::sourceColour( name, skin ), store.get( s + 2 ) } );
        }
        return rows;
    };
    a.m_setDepth = [set]( int row, float d ) { set( row + 2, d ); };
    a.m_remove = [set, begin, end]( int row )
    {
        if ( begin ) begin( "Modulation off" );
        set( row, 0.0f );
        if ( end ) end();
    };
    a.m_sources = [&store, slotsOf, targetOf]( int id )
    {
        std::vector<ModAccess::Source> out;
        if ( targetOf( id ) < 1 ) return out;
        const auto &sd = store.desc( slotsOf( id ).front() );
        if ( sd.m_choices == nullptr ) return out;
        for ( int k = 1; k <= static_cast<int>( sd.m_maxValue ); ++k )
        {
            const juce::String n( sd.m_choices[k] );
            out.push_back( { k, n, detail::sourceSection( n ) } );
        }
        std::stable_sort( out.begin(), out.end(),
                          []( const ModAccess::Source &x, const ModAccess::Source &y )
                          {
                              static const juce::StringArray order{ "LFOs", "Envelopes", "The note",
                                                                    "The hand and the track" };
                              return order.indexOf( x.m_section ) < order.indexOf( y.m_section );
                          } );
        return out;
    };
    a.m_add = [&store, slotsOf, targetOf, set, begin, end]( int id, int source )
    {
        const int t = targetOf( id );
        if ( t < 1 ) return false;
        for ( int s : slotsOf( id ) )
            if ( store.getInt( s ) == 0 )
            {   // the first free slot: the source on this target, a quarter of its span
                if ( begin ) begin( juce::String( "Modulate " ) + store.desc( id ).m_name );
                set( s + 1, static_cast<float>( t ) );
                set( s + 2, 0.25f );
                set( s, static_cast<float>( source ) );
                if ( end ) end();
                return true;
            }
        return false;
    };
    a.m_arcs = [&store, slotsOf, targetOf, sourceName, &skin]( int id )
    {
        std::vector<ModArc> arcs;
        const int           t = targetOf( id );
        if ( t < 1 ) return arcs;
        for ( int s : slotsOf( id ) )
        {
            const int src = store.getInt( s );
            if ( src <= 0 || store.getInt( s + 1 ) != t ) continue;
            const juce::String name = sourceName( s, src );
            // The amount is of the target's span; drawn on the knob's range as it stands (a voice's sources run per note:
            // no "now" to show, the dot stays on the value).
            arcs.push_back( { juce::jlimit( -1.0f, 1.0f, store.get( s + 2 ) ), !detail::bipolarSource( name ), 0.0f,
                              detail::sourceColour( name, skin ) } );
        }
        return arcs;
    };
    return a;
}

/**
 * @brief Modulation at the knob: every matrix row on a knob is an arc round it (LiveRings draws it), the knob's
 *        right-click menu lists the rows with their depth to drag and a button to remove each, and offers every source
 *        ("Modulate with"); an Alt-drag on a modulated knob sets its last row's depth.
 */
class ModulationAtKnob
{
public:
    /**
     * @brief The modulation of @p access.
     * @param[in] access  the matrix it works on
     */
    explicit ModulationAtKnob( ModAccess access ) : m_access( std::move( access ) ) {}
    /**
     * @brief Puts it into the controls' @p actions (more, altDrag) and the knobs' @p rings (the arcs).
     * @param[in,out] actions  the controls' actions
     * @param[in,out] rings    the knobs' rings, or null
     */
    void install( ControlActions &actions, LiveRings *rings );
    /**
     * @brief The modulation items for store id @p id in a knob's menu (nothing for an id that is no target).
     * @param[in,out] m   the knob's menu, added to
     * @param[in]     id  the knob's store id
     */
    void addMenuItems( juce::PopupMenu &m, int id );
    /**
     * @brief An Alt-drag on store id @p id (ControlActions::altDrag): phase 0 the press, 1 each drag, 2 the release.
     * @param[in] id     the knob's store id
     * @param[in] e      the mouse event
     * @param[in] phase  0 the press, 1 a drag, 2 the release
     * @return true when it took the drag
     */
    bool altDrag( int id, const juce::MouseEvent &e, int phase );
    /**
     * @brief What it works on.
     * @return the matrix it works on
     */
    const ModAccess &access() const { return m_access; }

private:
    ModAccess m_access;          ///< the instrument's matrix
    int       m_row = -1;        ///< the row an Alt-drag sets, -1 none
    float     m_depth0 = 0.0f;   ///< its depth when the drag began
};

}   // namespace frame
