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
 * @author Rene Weller
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
class ModSlotControl final : public juce::Component
{
public:
    /**
     * @brief Slot @p number of the matrix, drawn in @p colour.
     * @param[in] number  the slot's number
     * @param[in] colour  its colour
     */
    ModSlotControl( int number, juce::Colour colour );
    /** @brief The number, the two menus and the amount in a row. */
    void resized() override;
    /**
     * @brief The number.
     * @param[in,out] g  the graphics context
     */
    void paint( juce::Graphics &g ) override;
    juce::ComboBox m_source;   ///< the slot's source
    juce::ComboBox m_target;   ///< the slot's target
    juce::Slider   m_amount;   ///< the slot's amount, -1 .. 1

private:
    int m_number;   ///< the slot's number
};

/**
 * @brief The parameters of one module instance as knobs, menus and switches: every control with the frame's right-click
 *        menu (MIDI learn, forget, default), a double click to its default, and the live ring of the moves.
 */
class ParamPage final : public juce::Component, public frame::Revealer, private juce::Timer
{
public:
    /**
     * @brief A page for the modules in @p groups.
     * @param[in,out] p          the processor
     * @param[in]     groups     module and instance pairs, shown one after the other
     * @param[in]     instances  how many instances the modules have; above one, a selector picks the instance
     */
    ParamPage( EphemerisProcessor &p, std::vector<std::pair<eph::ModuleE, int>> groups, int instances );
    void resized() override;   ///< lays the groups out, flowing across the page
    /**
     * the group boxes and their titles
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override;
    /**
     * @brief The height the page needs at @p width (for a page in a viewport).
     * @param[in] width  the page's width
     * @return the height it needs
     */
    int heightFor( int width ) const;
    /**
     * @brief The height the page has in its window (the scrolling page around it says so before heightFor).
     * @param[in] h  the height in pixels
     */
    void setAvailableHeight( int h ) { m_available = h; }
    /**
     * @brief The page's parameters in words, group by group (the help's topic for the tab).
     * @return the description
     */
    juce::String describe() const;
    /**
     * @brief The instance shown (the row), 0 for a page of one.
     * @return the instance shown, 0 for a page of one
     */
    int instance() const { return m_instances > 1 ? m_instance.getSelectedId() - 1 : 0; }
    /**
     * @brief Shows the section holding @p part (the search found it there).
     * @param[in,out] part  the control to bring into view
     */
    void reveal( juce::Component &part ) override;
    /**
     * @brief Shows the instance whose controls hold store id @p id (a row); false when none of them does.
     * @param[in] id  the store id
     * @return false when no instance holds it
     */
    bool bringId( int id ) override;
    /**
     * @brief Shows preset @p menuId as chosen (the browser took it: a factory preset's index + 1, or 5001 + one of the
     *        player's).
     * @param[in] menuId  the menu id
     */
    void chosen( int menuId );

private:
    /** @brief Makes the controls of every group (once, in the constructor). */
    void build();
    /**
     * @brief Applies factory preset @p index to the page's synth (the instance shown).
     * @param[in] index  the factory preset's index
     */
    void choosePreset( int index );
    /** @brief Fills the list: the factory presets in their groups, then the user's under "User". */
    void fillPresets();
    EphemerisProcessor   &m_proc;      ///< the processor: the parameters, the undo, MIDI learn
    frame::ControlActions m_actions;   ///< the controls' right-click menu
    frame::LiveRings      m_live;      ///< where each knob's value plays
    /** @brief The modulation at the knob (15.10.2026): the voice's matrix rows on its knobs, as arcs and in their menus. */
    std::unique_ptr<frame::ModulationAtKnob> m_modulation;
    /** @brief Opens the preset browser on the page's synth (15.10.2026). */
    frame::IconButton m_browse{
        frame::IconButton::IconE::SEARCH,
        "Browse the presets: by name, group, favourites, recently played; hold a row's triangle to listen"
    };
    juce::Label m_star;   ///< "*" while the knobs have moved from the preset
    std::vector<std::pair<int, float>>
        m_loadedKnobs;   ///< the knobs the preset put (store id, real value): the star compares
    std::vector<std::pair<eph::ModuleE, int>> m_groups;      ///< the module instances shown, in order
    int                                       m_instances;   ///< how many instances the modules have
    juce::ComboBox                            m_instance;    ///< the instance selector (above one instance)
    /// The factory presets of the page's synth (Presets.h): a list in groups, and a step back and forth.
    eph::ModuleE      m_presetModule = eph::ModuleE::COUNT;
    int               m_presetCount = 0;    ///< how many factory presets the page's synth has
    int               m_presetIndex = -1;   ///< the preset chosen, -1 none
    juce::ComboBox    m_preset;             ///< the presets in their groups, then the user's
    juce::TextButton  m_prev{ "<" };        ///< the preset before
    juce::TextButton  m_next{ ">" };        ///< the preset after
    frame::IconButton m_save{ frame::IconButton::IconE::SAVE,
                              "Save the sound as a preset of your own" };   ///< saves the page as a user preset: a disk
    std::vector<eph::SoundPreset>      m_user;         ///< the user's presets of the synth (ids from 5001 in the list)
    std::unique_ptr<juce::AlertWindow> m_nameDialog;   ///< the user preset's name dialog while it is open
    juce::ToggleButton m_allRows{ "All rows" };        ///< on a page with instances: a preset goes to every one
    juce::Label
        m_composed;     ///< the preset the composer chose for this piece (compose.pick_sounds), where the piece now is
    int m_shown = -2;   ///< the preset m_composed shows (-1 none)
    void timerCallback() override;   ///< follows the piece: the composer's preset of the moment
    // An effect page (no factory presets): the user's presets of the whole page, every module of it, by full key.
    bool m_pagePresets = false;   ///< an effect page: the user presets are the whole page's
    std::vector<std::pair<juce::String, juce::String>> m_pageUser;   ///< name and text
    /**
     * @brief The folder of this page's user presets.
     * @return the folder
     */
    juce::File pageFolder() const;
    /** @brief Fills the list with the page's user presets. */
    void fillPagePresets();
    juce::OwnedArray<juce::Component> m_controls;   ///< the knobs, menus, switches and preset bars
    juce::OwnedArray<juce::Label>     m_labels;     ///< the names above them
    /** @brief A control of the page: its name above it, large or not (EditorTheme.h, layoutOf). */
    struct Cell
    {
        int                  m_control = -1;     ///< index into m_controls and m_labels
        bool                 m_big = false;      ///< a large encoder
        bool                 m_narrow = false;   ///< a narrow menu (short entries: an LFO's shape and sync)
        int                  m_kind = 0;         ///< 0 a knob, 1 a menu, 2 a switch, 3 a slot of the modulation matrix
        juce::Rectangle<int> m_bounds;           ///< where it sits on the page
    };
    /** @brief A titled group of cells, drawn as a box. */
    struct Box
    {
        juce::String         m_title;    ///< the group's title
        juce::Colour         m_colour;   ///< the module family's colour
        std::vector<Cell>    m_cells;    ///< its controls
        juce::Rectangle<int> m_bounds;   ///< where the box sits on the page
    };
    std::vector<Box> m_boxes;   ///< the groups, in order
    /**
     * @brief A page taller than its window shows its groups in sections, one at a time (01.10.2026, frame::planSections):
     *        the sound and the modulation apart, each cut where the window ends.
     */
    frame::SectionSwitch    m_section{ ephui::skin() };
    bool                    m_split = false;           ///< more than one section
    std::vector<int>        m_sectionOf;               ///< per box its section
    const std::vector<int> *m_measuring = nullptr;     ///< while planning: the boxes being measured
    int                     m_available = 0;           ///< the height of the window (setAvailableHeight)
    int                     m_plannedWidth = -1;       ///< the width the sections were planned for, -1 never
    int                     m_plannedAvailable = -1;   ///< the height the sections were planned for, -1 never
    /**
     * the box is in the section shown (or being measured)
     * @param[in] box  the box's index
     * @return true when it is in the section shown
     */
    bool shown( size_t box ) const;
    /**
     * the sections of a page @p width wide in m_available
     * @param[in] width  the page's width
     */
    void plan( int width );
    void applySection();   ///< the controls of the section shown, the others hidden
    void refit();          ///< laid out again, and the scrolling page around it measures again
    /**
     * height of the instance and preset bar
     * @return the height of the instance bar
     */
    int top() const;
    /**
     * @brief Places the boxes and their cells in @p area (@p apply: move the components too); returns the height used.
     * @param[in] area   where the boxes go
     * @param[in] apply  true to move the components too
     * @return the height used
     */
    int layoutBoxes( juce::Rectangle<int> area, bool apply );
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>>   m_sliders;   ///< the knobs on their parameters
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> m_combos;    ///< the menus on their parameters
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>>   m_buttons;   ///< the switches on their parameters
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
class ArrangeView final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    /**
     * shows @p p's score
     * @param[in,out] p  the processor
     */
    explicit ArrangeView( EphemerisProcessor &p );
    /**
     * the picture of the window, the playhead over it
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override;
    /**
     * jumps there (zoomed: on release, if it was no drag)
     * @param[in] e  the mouse event
     */
    void mouseDown( const juce::MouseEvent &e ) override;
    /**
     * moves a zoomed view along
     * @param[in] e  the mouse event
     */
    void mouseDrag( const juce::MouseEvent &e ) override;
    /**
     * the jump of a zoomed view
     * @param[in] e  the mouse event
     */
    void mouseUp( const juce::MouseEvent &e ) override;
    /**
     * the whole length again
     * @param[in] e  the mouse event
     */
    void mouseDoubleClick( const juce::MouseEvent &e ) override;
    /**
     * @brief Zooms around the pointer; the wheel sideways, or with Shift, moves along.
     * @param[in] e      the mouse event
     * @param[in] wheel  the wheel's movement
     */
    void mouseWheelMove( const juce::MouseEvent &e, const juce::MouseWheelDetails &wheel ) override;
    /**
     * a trackpad's pinch zooms
     * @param[in] e      the mouse event
     * @param[in] scale  the pinch's factor
     */
    void mouseMagnify( const juce::MouseEvent &e, float scale ) override;
    /**
     * @brief Shows the beats @p from to @p to once a score is there (the pictures' EPH_SHOT_ZOOM).
     * @param[in] from  the first beat to show
     * @param[in] to    the last beat to show
     */
    void zoomTo( double from, double to ) { m_wanted = { from, to }; }

private:
    /** @brief A note as the zoomed lanes draw it: where it begins and ends (beats), how loud. */
    struct Hit
    {
        float m_from;       ///< where it begins, beats
        float m_to;         ///< where it ends, beats
        float m_velocity;   ///< how loud, 0..1
    };
    static constexpr int    Lanes = 6;          ///< rows, lead, tape keys, strings, drone, drums
    static constexpr double Narrowest = 16.0;   ///< the least the view shows, in beats (four bars)
    void timerCallback() override;              ///< pages on with the playhead, repaints
    void rebuild();                             ///< the lanes of a new score
    /**
     * the window, kept inside the length (all of it: not zoomed)
     * @param[in] from  the first beat
     * @param[in] span  how many beats
     */
    void show( double from, double span );
    /**
     * the window times @p factor, the beat under @p x staying put
     * @param[in] x       the pointer's position
     * @param[in] factor  the zoom factor
     */
    void zoomAround( float x, double factor );
    /**
     * the beats shown
     * @param[out] from  receives the first beat shown
     * @param[out] to    receives the last beat shown
     */
    void window( double &from, double &to ) const;
    /**
     * the beats shown; false while it is the whole length
     * @param[out] from  receives the first beat shown
     * @param[out] to    receives the last beat shown
     * @return false while the whole length is shown
     */
    bool zoomed( double &from, double &to ) const;
    /**
     * the beat under @p x
     * @param[in] x  the position in pixels
     * @return the beat there
     */
    double beatAt( float x ) const;
    /**
     * all but the playhead
     * @param[in,out] g     the graphics context
     * @param[in]     w     the width in pixels
     * @param[in]     h     the height in pixels
     * @param[in]     from  the first beat shown
     * @param[in]     to    the last beat shown
     * @param[in]     zoom  true for the zoomed view
     */
    void render( juce::Graphics &g, float w, float h, double from, double to, bool zoom );
    EphemerisProcessor       &m_proc;                 ///< the processor: what plays, where it is
    int                       m_version = -1;         ///< the score the lanes were built from
    double                    m_beats = 0.0;          ///< its length
    std::vector<eph::Marker>  m_markers;              ///< its sections
    eph::TempoMap             m_tempo;                ///< its tempo (the ruler's minutes)
    int                       m_bins = 0;             ///< the lanes' columns across the score: one a beat
    std::vector<float>        m_lanes;                ///< Lanes x m_bins activity, 0..1
    std::vector<Hit>          m_hits[Lanes];          ///< the notes per lane, in the order they begin
    double                    m_longest = 0.0;        ///< the longest note (how far before a window its notes begin)
    double                    m_from = 0.0;           ///< the first beat of the window
    double                    m_span = 0.0;           ///< its length in beats (0: the whole length)
    double                    m_lastPos = -1.0;       ///< the playhead at the last tick
    double                    m_lastFrom = 0.0;       ///< the window's first beat at the last tick
    double                    m_lastTo = 0.0;         ///< the window's last beat at the last tick
    float                     m_downX = 0.0f;         ///< where a press began
    double                    m_downFrom = 0.0;       ///< the window's beginning then
    bool                      m_dragged = false;      ///< the press has moved the view: no jump on release
    std::pair<double, double> m_wanted{ 0.0, 0.0 };   ///< zoomTo's window, until a score takes it
    juce::Image               m_cache;                ///< the picture of the window
    double                    m_cachedFrom = -1.0;    ///< the first beat m_cache was drawn for
    double                    m_cachedTo = -1.0;      ///< the last beat m_cache was drawn for
    int                       m_cachedVersion = -1;   ///< the score m_cache was drawn from, -1 none
};

/** @brief A parameter page in a viewport: it scrolls when its groups need more height than the tab gives. */
class ScrollingPage final : public juce::Component
{
public:
    /**
     * takes the page over
     * @param[in] page  the page, taken over
     */
    explicit ScrollingPage( std::unique_ptr<ParamPage> page );
    void resized() override;   ///< the page as wide as the view, as tall as it needs
    /**
     * @brief Sizes @p page for a view of @p area (the scroll bar taken off where it will show).
     * @param[in,out] page  the page
     * @param[in,out] view  its viewport
     * @param[in]     area  the view's area
     */
    static void fit( ParamPage &page, juce::Viewport &view, juce::Rectangle<int> area );
    /**
     * the page (the help describes it)
     * @return the page
     */
    const ParamPage &page() const { return *m_page; }

private:
    juce::Viewport             m_view;   ///< scrolls the page
    std::unique_ptr<ParamPage> m_page;   ///< the page
};

/**
 * @brief The Arrange tab (01.10.2026, as the siblings have it): the rerolls -- each unit on its own stream (SetFile.h),
 *        so a reroll changes that and nothing else --, the arrangement large, and under it the automation: the moves of
 *        the composed player's two hands on the knobs over the whole piece (EditorGestures.h).
 */
class ArrangePage final : public juce::Component, private juce::Timer
{
public:
    /**
     * @brief The tab for @p p.
     * @param[in,out] p  the processor
     */
    explicit ArrangePage( EphemerisProcessor &p );
    /** @brief The rerolls on top, the view large, the automation under it. */
    void resized() override;
    /**
     * @brief The background and the headings.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override;

private:
    /** @brief Shows what plays under the playhead. */
    void timerCallback() override;
    EphemerisProcessor                &m_proc;      ///< the processor: what plays, the rerolls
    juce::Label                        m_which;     ///< what plays under the playhead (the status line)
    juce::OwnedArray<juce::TextButton> m_rerolls;   ///< a reroll per unit of the piece
    std::unique_ptr<ArrangeView>       m_view;      ///< the arrangement, large
    std::unique_ptr<juce::Component>   m_moves;     ///< the automation (GestureView)
};

/** @brief The Export tab (01.10.2026, as the siblings have it): the files, what each holds, the OSC cues' settings. */
class ExportPage final : public juce::Component, private juce::Timer
{
public:
    /**
     * @brief The tab for @p p.
     * @param[in,out] p  the processor
     */
    explicit ExportPage( EphemerisProcessor &p );
    /** @brief The buttons, the status, the cue settings under them. */
    void resized() override;
    /**
     * @brief The background and what each file holds.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override;
    /**
     * asks for a file, then exports (Ctrl+E: without the stems)
     * @param[in] stems  true to export the stems too
     */
    void exportWith( bool stems );
    void save();   ///< asks for a file, then saves the set (Ctrl+S)
    void load();   ///< asks for a set, then loads it (Ctrl+O)

private:
    /** @brief Shows the export's progress and result. */
    void timerCallback() override;
    EphemerisProcessor                &m_proc;                        ///< the processor: the export, the set file
    juce::TextButton                   m_wav{ "WAV + MIDI" };         ///< exports the WAV and the MIDI
    juce::TextButton                   m_stems{ "... with stems" };   ///< ... and the stems
    frame::IconButton                  m_save{ frame::IconButton::IconE::SAVE,
                                               "Save the set (Ctrl+S)" };   ///< saves the set (Ctrl+S): a disk
    frame::IconButton                  m_load{ frame::IconButton::IconE::OPEN,
                                               "Load a set (Ctrl+O)" };   ///< loads a set (Ctrl+O): a folder
    juce::Label                        m_status;                          ///< the export's progress and result
    std::unique_ptr<ParamPage>         m_cue;                             ///< the OSC cues' settings
    std::unique_ptr<juce::FileChooser> m_chooser;                         ///< the file dialog while it is open
};

/**
 * @brief The editor's body: everything, drawn at the design size and scaled to the window (26.09.2026, as Phosphene's),
 *        so a maximised or full-screen window shows the panel larger rather than emptier.
 */
class EditorBody final : public juce::Component
{
public:
    std::function<void( juce::Graphics & )> m_painter;    ///< draws the background and the logo
    std::function<void()>                   m_onResize;   ///< lays the controls out
    /**
     * @brief Draws through painter.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override
    {
        if ( m_painter ) m_painter( g );
    }
    /** @brief Lays out through onResize. */
    void resized() override
    {
        if ( m_onResize ) m_onResize();
    }
};

/** @brief The editor: the frame's header and keys, the arrange view, the tabs, the help over them. */
class EphemerisEditor final : public juce::AudioProcessorEditor, private juce::Timer, private juce::ChangeListener
{
public:
    /**
     * builds the panel for @p p
     * @param[in,out] p  the processor
     */
    explicit EphemerisEditor( EphemerisProcessor &p );
    ~EphemerisEditor() override;   ///< stops the refresh timer
    /**
     * the background
     * @param[in,out] g  the graphics context
     */
    void paint( juce::Graphics &g ) override;
    void resized() override;   ///< scales the body to the window
    void
    parentHierarchyChanged() override;   ///< the standalone's title bar gets a maximise button, the window its place
    /**
     * the frame's keys (frame::handleKey)
     * @param[in] key  the key
     * @return true when the key was used
     */
    bool keyPressed( const juce::KeyPress &key ) override;
    /**
     * @brief Opens the preset browser on instance @p instance of synth @p m; @p chosen hears the menu id taken (a factory
     *        preset's index + 1, or 5001 + one of the player's).
     * @param[in] m         the synth
     * @param[in] instance  its instance
     * @param[in] chosen    called with the menu id taken
     */
    void browsePresets( eph::ModuleE m, int instance, std::function<void( int )> chosen );
    /**
     * @brief Shows a line in the info line (an Alt-drag's depth).
     * @param[in] text  the line
     */
    void say( const juce::String &text )
    {
        if ( m_info != nullptr ) m_info->showText( text );
    }

private:
    /** @brief Follows the processor: the status, the play button, the rerolls, the update link; the screenshots. */
    void timerCallback() override;
    void changeListenerCallback( juce::ChangeBroadcaster * ) override;   ///< the settings changed (the backdrop)
    void layoutBody();         ///< the top bar, the arrange view, the tabs, at the design scale
    void toggleFullScreen();   ///< the standalone's window full screen and back
    /**
     * whether it is
     * @return true in full screen
     */
    bool fullScreen() const;
    /**
     * the editor sits in the standalone's window
     * @return true in the standalone's window
     */
    bool standalone() const;
    /**
     * the length slider for a piece's minutes or a concert's
     * @param[in] kind  what the slider sets: a piece's minutes or a concert's
     */
    void showLength( int kind );
    /**
     * the help over the tabs (F1)
     * @param[in] on  true to show it
     */
    void showHelp( bool on );
    void showSettings();   ///< the settings menu
    /**
     * the Export tab's page
     * @return the page
     */
    ExportPage *exportPage() const;
    void openPalette();   ///< the command palette (Ctrl+K)
    /**
     * what the palette can do: actions, pages, controls, presets
     * @return the palette's commands
     */
    std::vector<frame::CommandPalette::Command> paletteCommands();
    void startTour();   ///< the first steps, from the first
    /**
     * @brief The info line's words for store id @p id: its name, its value, its range, what moves it, its key.
     * @param[in]  id    the store id
     * @param[out] name  the control's name
     * @param[out] text  its value, range, mover and key
     * @return false when no page has the control
     */
    bool describeControl( int id, juce::String &name, juce::String &text );
    /**
     * @brief Shows the control of store id @p id (its tab, sub-tab, section, row) and lights it; false when no page has
     *        it.
     * @param[in] id  the store id
     * @return false when no page has the control
     */
    bool revealId( int id );
    EphemerisProcessor &m_proc;                       ///< the processor
    frame::LookAndFeel  m_lnf{ ephui::skin() };       ///< first, so it outlives every component that uses it
    juce::TooltipWindow m_tooltips{ nullptr, 700 };   ///< the tooltips, after 700 ms
    EditorBody          m_body;                       ///< everything, at the design size
    frame::Backdrop     m_backdrop;                   ///< the picture behind the panel
    int                 m_headerBottom = 0;           ///< where the header ends (the backdrop is strong above)
    // The update check (UpdateCheck.h): a link where a newer version is out; the settings switch it.
    juce::SharedResourcePointer<UpdateCheck> m_updates;   ///< the update check, shared by every editor
    juce::HyperlinkButton                    m_update;    ///< the link to a newer release
    juce::Label                              m_status;    ///< what plays, the status line
    juce::Label                              m_rerolls;   ///< what was rerolled or locked
    juce::Component                          m_title;     ///< the name's place (drawn by the body)
    frame::IconButton m_undo{ frame::IconButton::IconE::UNDO, "Undo (Ctrl+Z)" };      ///< undo (Ctrl+Z)
    frame::IconButton m_redo{ frame::IconButton::IconE::REDO, "Redo (Ctrl+Y)" };      ///< redo (Ctrl+Y)
    frame::IconButton m_help{ frame::IconButton::IconE::HELP, "Help (F1)" };          ///< the help (F1)
    frame::IconButton m_settings{ frame::IconButton::IconE::SETTINGS, "Settings" };   ///< the settings menu
    /** @brief shown while a headset sends its hands */
    frame::IconButton                m_headsetIcon{ frame::IconButton::IconE::HEADSET,
                                                    "A headset sends its hands: the Perform page shows them" };
    std::unique_ptr<frame::HelpView> m_helpView;            ///< the help, while it is open
    bool                             m_shotHands = false;   ///< EPH_SHOT_HEADSET: the headset's hands kept alive
    juce::Rectangle<float>           m_logo;                ///< where the logo is drawn, left of the title
    juce::ComboBox                   m_style;               ///< compose.style
    juce::ComboBox                   m_key;                 ///< compose.key
    juce::ComboBox                   m_scale;               ///< compose.scale
    /** A piece, a concert or a night set (EphemerisProcessor::chooseKind), and the length of what is chosen. */
    juce::TextButton  m_pieceMode{ "Piece" };
    juce::TextButton  m_concertMode{ "Concert" };   ///< a concert (compose.concert_minutes)
    juce::TextButton  m_nightMode{ "Night set" };   ///< a night set
    juce::Slider      m_length;                     ///< the length: a piece's minutes or a concert's
    juce::Label       m_lengthLabel;                ///< the length's name
    int               m_lengthKind = -1;   ///< what the slider shows (0: compose.piece_minutes, else concert_minutes)
    bool              m_syncing = false;   ///< the slider is set from its parameter, not by a hand
    juce::TextButton  m_compose{ "Compose piece" };   ///< composes a piece, a concert or a night set
    frame::IconButton m_seed{ frame::IconButton::IconE::DICE, "New seed" };   ///< a new seed, then composes: a die
    frame::IconButton m_play{ frame::IconButton::IconE::PLAY,
                              "Play (Space)" };   ///< play and stop: a triangle, a square
    frame::IconButton m_mute{ frame::IconButton::IconE::SPEAKER,
                              "Silence the output" };   ///< mutes the output: a speaker, crossed out when muted
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>>
                          m_combos;                                     ///< the header's menus on their parameters
    ArrangeView           m_arrange;                                    ///< the arrange strip on top
    juce::TabbedComponent m_tabs{ juce::TabbedButtonBar::TabsAtTop };   ///< the pages
    juce::String          m_shotPath;        ///< TOT_SHOT: where the screenshot goes, empty none
    int                   m_shotTicks = 0;   ///< ticks until the screenshot is taken
    /** @brief The command palette's button (Ctrl+K). */
    frame::IconButton                m_search{ frame::IconButton::IconE::SEARCH,
                                               "Search (Ctrl+K): any control, preset or action by a few letters" };
    std::unique_ptr<frame::InfoLine> m_info;   ///< the info line at the bottom: what the control under the mouse is
    frame::CommandPalette            m_palette{ ephui::skin() };    ///< the command palette, over everything while open
    frame::PresetBrowser m_browser{ ephui::skin(), "Ephemeris" };   ///< the preset browser, over everything while open
    frame::Tour          m_tour{ ephui::skin() };                   ///< the first steps
    frame::WindowKeeper  m_window;                                  ///< the standalone's window back where it was left
    int                  m_tourTicks = 0;   ///< ticks until the first start's tour (-1: considered)
};
