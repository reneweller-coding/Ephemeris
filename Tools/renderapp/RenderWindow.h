/**
 * @file RenderWindow.h
 * @brief The render app's window: the rows in a table (the instrument's columns and the length of each rendering), a
 *        search over them, and what to render them into -- the format, the instrument's choices, the dice, the folder
 *        -- with the rendering's progress, speed and time left. Several chosen are rendered one after another.
 *
 * The lengths are worked out on a thread of their own (LengthWorker): a journey's in a moment, a composed set's in a
 * fraction of a second each; the table shows them as they come.
 * @author Rene Weller
 */
#pragma once

#include "RenderKit.h"

namespace renderkit
{

/** @brief Works out the length of every row for the dice and choices of the moment, on a thread of its own. */
class LengthWorker : public juce::Thread
{
public:
    /**
     * @brief A worker for an instrument.
     * @param[in,out] source  the instrument (outlives the worker)
     */
    explicit LengthWorker( Source &source );
    /** @brief Stops the pass under way and waits for the thread. */
    ~LengthWorker() override;
    /**
     * @brief Begins a new pass over the rows (the old one is dropped).
     * @param[in] items    every row
     * @param[in] request  the dice and the choices
     */
    void ask( const std::vector<Item> &items, const Request &request );
    /**
     * @brief Works out this row before the others (the one chosen).
     * @param[in] index  the row in the items asked for, -1: none
     */
    void prefer( int index );
    /**
     * @brief A row's length, when it is known.
     * @param[in]  index  the row in the items asked for
     * @param[out] known  receives whether it is known yet
     * @return seconds (0 when not known, or when it cannot be rendered)
     */
    double get( int index, bool &known );
    /** @brief How many lengths came in since the window last looked (it repaints then). @return the count */
    int takeNews();

private:
    /** @brief Works the pass out, the preferred row first; waits for the next one. */
    void run() override;
    Source             &m_source;           ///< the instrument
    std::mutex          m_lock;             ///< guards everything below
    std::vector<Item>   m_items;            ///< the rows of this pass
    Request             m_request;          ///< its dice and choices
    std::vector<double> m_lengths;          ///< per row, seconds
    std::vector<char>   m_known;            ///< per row, worked out yet
    int                 m_generation = 0;   ///< counts the passes: a result of an old one is dropped
    int                 m_prefer = -1;      ///< the row to do first
    int                 m_news = 0;         ///< results since takeNews()
    juce::WaitableEvent m_wake;             ///< a pass was asked for (or the worker closes)
};

/** @brief The window's content. */
class RenderWindow : public juce::Component, private juce::TableListBoxModel, private juce::Timer
{
public:
    /**
     * @brief The window for an instrument.
     * @param[in,out] source  the instrument (outlives the window)
     */
    explicit RenderWindow( Source &source );
    /** @brief Keeps the settings. */
    ~RenderWindow() override;
    /**
     * @brief Paints the background, the subtitle and the panel's box.
     * @param[in,out] g  the graphics context
     */
    void paint( juce::Graphics &g ) override;
    /** @brief Lays out the table and the panel beside it. */
    void resized() override;
    /**
     * @brief Chooses a row in the table (the manual's picture).
     * @param[in] name  its name
     * @return false when the table has none of that name
     */
    bool choose( const juce::String &name );
    /** @brief Brings every length in before a picture is taken (it waits for the length thread). */
    void settle();

private:
    /** @brief The table's columns beyond the source's. */
    enum ColumnE
    {
        NAME = 1,     ///< the row's name
        LENGTH = 2,   ///< the length of its rendering
        FIRST = 3     ///< the source's first column
    };
    /** @brief A choice on the panel, built from the source's Option. */
    struct Control
    {
        Option                              m_option;   ///< what it is
        std::unique_ptr<juce::Label>        m_label;    ///< a menu's label (null for a toggle)
        std::unique_ptr<juce::ComboBox>     m_menu;     ///< the menu (null for a toggle)
        std::unique_ptr<juce::ToggleButton> m_toggle;   ///< the toggle (null for a menu)
    };
    /** @brief Reads the rows from the source again (the player's files may have changed) and the table with them. */
    void reload();
    /** @brief The rows: those the search lets through, in the order asked for. */
    void refilter();
    /** @brief The request the panel describes: the dice, every choice. @return it */
    Request request() const;
    /** @brief Asks the length worker for every row again (the dice or a choice changed). */
    void relength();
    /** @brief The panel's text for the rows chosen: the first one's lines and the length of this rendering. */
    void updateChosen();
    /** @brief Queues every row chosen. */
    void renderChosen();
    /**
     * @brief The rows the table has chosen.
     * @return their indices into the items, in the table's order
     */
    std::vector<int> chosen() const;
    /** @brief Progress, speed, time left, and the lengths as they come, ten times a second. */
    void timerCallback() override;
    /** @brief The rows. @return their count */
    int getNumRows() override;
    /**
     * @brief A row's background.
     * @param[in,out] g         the graphics context
     * @param[in]     row       the row
     * @param[in]     w         its width
     * @param[in]     h         its height
     * @param[in]     selected  it is chosen
     */
    void paintRowBackground( juce::Graphics &g, int row, int w, int h, bool selected ) override;
    /**
     * @brief A cell.
     * @param[in,out] g         the graphics context
     * @param[in]     row       the row
     * @param[in]     column    the column (ColumnE, then the source's)
     * @param[in]     w         its width
     * @param[in]     h         its height
     * @param[in]     selected  its row is chosen
     */
    void paintCell( juce::Graphics &g, int row, int column, int w, int h, bool selected ) override;
    /**
     * @brief The order changed (a click on a heading).
     * @param[in] column   the column sorted by
     * @param[in] forward  ascending
     */
    void sortOrderChanged( int column, bool forward ) override;
    /**
     * @brief The choice changed.
     * @param[in] row  the last row clicked
     */
    void selectedRowsChanged( int row ) override;
    /**
     * @brief A double click renders the row.
     * @param[in] row     the row
     * @param[in] column  the column
     * @param[in] e       the mouse event
     */
    void cellDoubleClicked( int row, int column, const juce::MouseEvent &e ) override;

    Source                               &m_source;                 ///< the instrument
    Palette                               m_palette;                ///< its colours
    std::vector<Column>                   m_columns;                ///< its columns
    juce::LookAndFeel_V4                  m_look;                   ///< the colours on every control (outlives them)
    std::vector<Item>                     m_items;                  ///< every row
    std::vector<int>                      m_rows;                   ///< the table's rows: indices into m_items
    std::vector<Control>                  m_controls;               ///< the source's choices
    int                                   m_sortColumn = NAME;      ///< the order's column
    bool                                  m_sortForward = true;     ///< ascending
    std::uint64_t                         m_seed = 1;               ///< the dice
    juce::File                            m_folder;                 ///< where the files go
    std::unique_ptr<juce::PropertiesFile> m_settings;               ///< the window's settings
    RenderQueue                           m_queue;                  ///< the renderings
    LengthWorker                          m_lengths;                ///< the lengths
    double                                m_progress = 0.0;         ///< the progress bar's value
    juce::Label                           m_title;                  ///< the heading
    juce::Label                           m_search;                 ///< the search's label
    juce::TextEditor                      m_filter;                 ///< the search
    juce::TextButton                      m_refresh{ "Refresh" };   ///< reads the player's files again
    juce::TableListBox                    m_table;                  ///< the rows
    juce::Label                           m_chosenName;             ///< the row chosen first
    juce::Label                           m_chosenInfo;             ///< its lines and this rendering's length
    juce::Label                           m_formatLabel;            ///< "Format"
    juce::ComboBox                        m_format;                 ///< the format
    juce::TextButton                      m_dice{ "New dice" };     ///< new dice
    juce::Label                           m_diceNote;               ///< what the dice do
    juce::Label                           m_folderLabel;            ///< "Folder"
    juce::Label                           m_folderText;             ///< the folder
    juce::TextButton                      m_chooseFolder{ "Choose ..." };   ///< another folder
    juce::TextButton                      m_openFolder{ "Open folder" };    ///< the folder in the file manager
    juce::TextButton                      m_render{ "Render" };             ///< renders the rows chosen
    juce::TextButton                      m_cancel{ "Cancel" };             ///< stops the renderings
    juce::ProgressBar                     m_bar{ m_progress };              ///< the progress
    juce::Label                           m_status;                         ///< what it is doing
    std::unique_ptr<juce::FileChooser>    m_chooser;                        ///< the folder dialog while it is open
};

}   // namespace renderkit
