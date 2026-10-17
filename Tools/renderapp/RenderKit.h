/**
 * @file RenderKit.h
 * @brief The family's render app: a small program beside an instrument that renders what the instrument plays -- its
 *        journeys, its styles, the sets the player saved -- into WAV, FLAC, Ogg Vorbis or MP3 files, faster than real
 *        time, each shown with its length.
 *
 * The kit is the same in every instrument (copied unchanged; the canonical copy is Empyrean's Tools/renderapp, where it
 * began on 16.10.2026): the window (RenderWindow.h), the queue, the files, the command line, the self-test. What is the
 * instrument's own is a Source (its Source.cpp): what can be rendered (the rows), the choices on the panel (Option),
 * how long a rendering lasts, and the rendering itself (Rendering) -- its engine offline, block after block.
 *
 *     <App>                                          the window
 *     <App> --list                                   every row with the length of its rendering
 *     <App> --catalog [--seed N] [--KEY VALUE]...    the same as one JSON document, the options with it (Heliacal)
 *     <App> --render NAME [--format wav|flac|ogg|mp3] [--out FILE] [--seed N] [--seconds S] [--KEY VALUE]...
 *                 [--progress]                       (--progress: "progress DONE TOTAL SPEED" twice a second)
 *     <App> --selftest [S]                           every format, S seconds each, and the queue (the ctest)
 *     <App> --shot FILE [--choose NAME]              the window as a PNG, a row chosen (the manual's picture)
 *
 * MP3 goes through Windows' own encoder (Media Foundation, Mp3Writer.h); elsewhere the format is not offered.
 * @author Rene Weller
 */
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace renderkit
{

/** @brief The file formats a rendering is written in. */
enum class FormatE : int
{
    WAV = 0,   ///< 24-bit PCM (RF64 beyond 4 GB)
    FLAC,      ///< 24-bit, lossless
    OGG,       ///< Ogg Vorbis, about 320 kbit/s
    MP3,       ///< MPEG layer 3, 320 kbit/s (Windows' own encoder; not offered elsewhere)
    COUNT      ///< number of formats
};

/**
 * @brief A format's name for the menu.
 * @param[in] f  the format
 * @return the name
 */
const char *formatName( FormatE f );

/**
 * @brief A format's file extension, with its dot.
 * @param[in] f  the format
 * @return ".wav", ".flac", ".ogg" or ".mp3"
 */
const char *formatExtension( FormatE f );

/**
 * @brief Whether this computer writes the format (MP3 only on Windows).
 * @param[in] f  the format
 * @return true when it is offered
 */
bool formatAvailable( FormatE f );

/**
 * @brief Seconds as h:mm:ss (or m:ss under an hour).
 * @param[in] seconds  the time
 * @return the text
 */
juce::String clockText( double seconds );

/** @brief The window's colours, the instrument's. */
struct Palette
{
    juce::Colour m_back{ 0xff07090f };     ///< the background
    juce::Colour m_panel{ 0xff111725 };    ///< the panel and the table
    juce::Colour m_row{ 0xff0d121d };      ///< every other row
    juce::Colour m_raised{ 0xff1a2132 };   ///< buttons and menus
    juce::Colour m_edge{ 0xff283149 };     ///< hairlines
    juce::Colour m_chosen{ 0xff24405a };   ///< a chosen row
    juce::Colour m_text{ 0xffe2ecf2 };     ///< text
    juce::Colour m_dim{ 0xff8794a8 };      ///< secondary text
    juce::Colour m_accent{ 0xff7fe0c4 };   ///< the accent
};

/** @brief A column of the table, between the name and the length of the rendering. */
struct Column
{
    juce::String m_name;            ///< its heading
    int          m_width = 80;      ///< its width at the start, pixels
    bool         m_right = false;   ///< right-aligned (numbers, times)
};

/** @brief A row of the table: something to render. */
struct Item
{
    juce::String        m_name;          ///< as the table shows it; --render finds it by this
    juce::String        m_kind;          ///< its kind ("Journey", "Style", "Yours"), searched with the name
    juce::StringArray   m_cells;         ///< the source's columns, in their order
    std::vector<double> m_keys;          ///< per column a number to sort by (NaN: by the text)
    bool                m_dice = true;   ///< the dice change what it plays (a saved set brings its own)
    int                 m_index = 0;     ///< the source's own reference
    juce::String        m_detail;        ///< more words for the search (a set's style, a journey's presets)
};

/** @brief A choice on the panel: a toggle (no choices) or a menu. */
struct Option
{
    juce::String      m_key;              ///< its name in the settings and on the command line
    juce::String      m_label;            ///< its text on the panel
    juce::String      m_tooltip;          ///< what it does
    juce::StringArray m_choices;          ///< the menu's entries; empty for a toggle
    int               m_default = 0;      ///< the entry chosen at first (a toggle: 0 off, 1 on)
    bool              m_length = false;   ///< it changes the lengths (they are worked out again)
};

/** @brief What a rendering is asked for. */
struct Request
{
    std::uint64_t               m_seed = 1;      ///< the dice
    std::map<juce::String, int> m_options;       ///< every option's value: a menu's entry, a toggle's 0 or 1
    double                      m_limit = 0.0;   ///< s: at most this much (0: the whole)

    /**
     * @brief An option's value.
     * @param[in] key   the option
     * @param[in] dflt  when it is not there
     * @return its value
     */
    int option( const juce::String &key, int dflt = 0 ) const
    {
        const auto it = m_options.find( key );
        return it == m_options.end() ? dflt : it->second;
    }
};

/** @brief One rendering under way: the instrument's engine set up offline, handing out its samples block by block. */
class Rendering
{
public:
    virtual ~Rendering() = default;
    /** @brief How long it lasts, exactly. @return seconds */
    virtual double seconds() const = 0;
    /** @brief How long its end is faded out (raised cosine). @return seconds; 0 when it ends by itself */
    virtual double fadeSeconds() const { return 0.0; }
    /**
     * @brief The next samples.
     * @param[out] left    receives the left channel
     * @param[out] right   receives the right channel
     * @param[in]  frames  how many (at most the kit's block of 4096)
     */
    virtual void render( float *left, float *right, int frames ) = 0;
    /** @brief What plays now, for the status line ("step 3 of 24: Velvet Tide"). @return the text, or "" */
    virtual juce::String now() const { return {}; }
};

/**
 * @brief The instrument's side of the app: its rows, its choices, its lengths and its renderings.
 *
 * list() and options() run on the message thread; length() on the kit's length thread, open() on the render thread,
 * describe() on the message thread -- possibly at the same time, so a source keeps no state that they change.
 */
class Source
{
public:
    virtual ~Source() = default;
    /** @brief The instrument's name: its settings folder and its folder in Documents. @return "Empyrean" */
    virtual juce::String product() const = 0;
    /** @brief The window's title. @return "Empyrean Journeys" */
    virtual juce::String title() const = 0;
    /** @brief The line beside the title. @return the text */
    virtual juce::String subtitle() const = 0;
    /** @brief What a row is, for the texts. @return "journey", "set" */
    virtual juce::String noun() const = 0;
    /** @brief The window's colours. @return the instrument's */
    virtual Palette palette() const { return {}; }
    /** @brief The table's columns between the name and the length. @return them */
    virtual std::vector<Column> columns() const = 0;
    /** @brief Every row (again after Refresh: the player's files may have changed). @return them */
    virtual std::vector<Item> list() = 0;
    /** @brief The panel's choices. @return them */
    virtual std::vector<Option> options() const = 0;
    /**
     * @brief Whether a choice means anything for a row (the passes only for a cyclic journey).
     * @param[in] item  the row
     * @param[in] key   the option
     * @return true when it applies
     */
    virtual bool applies( const Item &item, const juce::String &key ) const
    {
        juce::ignoreUnused( item, key );
        return true;
    }
    /**
     * @brief How long a rendering of a row lasts with these dice and choices, exactly (any thread; may take a while).
     * @param[in] item  the row
     * @param[in] r     the request
     * @return seconds; 0 when it cannot be rendered
     */
    virtual double length( const Item &item, const Request &r ) = 0;
    /**
     * @brief The panel's lines about a row (its steps, its tempo -- not its length, which the kit adds).
     * @param[in] item  the row
     * @param[in] r     the request
     * @return the text, lines separated by newlines
     */
    virtual juce::String describe( const Item &item, const Request &r ) = 0;
    /**
     * @brief Sets a rendering up (on the render thread).
     * @param[in]  item  the row
     * @param[in]  r     the request
     * @param[out] why   receives why it cannot be rendered
     * @return the rendering, or null
     */
    virtual std::unique_ptr<Rendering> open( const Item &item, const Request &r, juce::String &why ) = 0;
    /** @brief The rate the files are written at. @return samples a second */
    virtual int sampleRate() const { return 48000; }
    /** @brief The row the self-test renders. @return its name ("": the first) */
    virtual juce::String testItem() const { return {}; }
};

/**
 * @brief The instrument's source (defined in its Source.cpp).
 * @return a new one
 */
std::unique_ptr<Source> makeSource();

/** @brief One rendering asked for: which row, how, into which file. */
struct Job
{
    Item       m_item;                    ///< the row
    Request    m_request;                 ///< the dice, the choices, the limit
    FormatE    m_format = FormatE::WAV;   ///< the file's format
    juce::File m_file;                    ///< where it goes; none: m_folder, named when its length is known
    juce::File m_folder;                  ///< the folder for a file named after the row and the length
};

/** @brief Renders jobs, one at a time, and tells how far it is (any thread may read the counters). */
class Renderer
{
public:
    /**
     * @brief Renders a job into its file, as fast as the computer allows. Blocks until it is done or cancelled.
     * @param[in,out] source     the instrument
     * @param[in]     job        the rendering
     * @param[in]     cancelled  asked between blocks: true stops it (the file is closed as far as it went)
     * @param[out]    file       receives the file written (named after the row and the length when the job names none)
     * @param[out]    why        receives why it failed ("cancelled" when it was)
     * @return true when the whole was written
     */
    bool render( Source &source, const Job &job, const std::function<bool()> &cancelled, juce::File &file,
                 juce::String &why );
    /** @brief What plays now (the rendering's own words). @return the text */
    juce::String now() const;

    std::atomic<double> m_done{ 0.0 };      ///< s rendered of the job under way
    std::atomic<double> m_total{ 0.0 };     ///< s it will last
    std::atomic<double> m_speed{ 0.0 };     ///< how many times faster than real time, so far
    std::atomic<bool>   m_setup{ false };   ///< the rendering is being set up (composing, levelling, loading)

private:
    mutable std::mutex m_lock;   ///< guards m_now
    juce::String       m_now;    ///< what plays now
};

/** @brief The renderings asked for, done one after another on a thread of their own. */
class RenderQueue : public juce::Thread
{
public:
    /**
     * @brief A queue for an instrument.
     * @param[in,out] source  the instrument (outlives the queue)
     */
    explicit RenderQueue( Source &source );
    /** @brief Stops what is under way and waits for the thread. */
    ~RenderQueue() override;
    /**
     * @brief Adds renderings (and starts the thread the first time).
     * @param[in] jobs  the renderings
     */
    void add( const std::vector<Job> &jobs );
    /** @brief Cancels the rendering under way and those still waiting. */
    void cancelAll();
    /**
     * @brief What the queue is doing, for the window.
     * @param[out] name      receives the row under way ("" when none)
     * @param[out] waiting   receives how many wait after it
     * @param[out] finished  receives how many were written so far
     * @return true while one is under way
     */
    bool status( juce::String &name, int &waiting, int &finished );
    /**
     * @brief The last message (a file written, a failure), once.
     * @return it, or "" when there is none new
     */
    juce::String takeMessage();
    Renderer m_renderer;   ///< the rendering under way: its progress

private:
    /** @brief Renders the queue, one after another; waits for work in between. */
    void run() override;
    Source             &m_source;            ///< the instrument
    std::mutex          m_lock;              ///< guards the queue and the messages
    std::vector<Job>    m_jobs;              ///< waiting, the next first
    juce::String        m_current;           ///< the row under way
    juce::String        m_message;           ///< the last message
    int                 m_finished = 0;      ///< written so far
    std::atomic<bool>   m_cancel{ false };   ///< the one under way stops
    juce::WaitableEvent m_wake;              ///< work was added (or the queue closes)
};

/**
 * @brief The seed a row is rendered with: the dice and its name together (FNV-1a), so each row draws its own and the same
 *        dice give every row the same rendering again.
 * @param[in] item  the row
 * @param[in] r     the request (its dice)
 * @return the seed, never 0
 */
std::uint64_t seedFor( const Item &item, const Request &r );

/**
 * @brief A file's name for a rendering: the row's name and the length, legal on every system ("Lullabies (6-11-58)").
 * @param[in] name     the row's name
 * @param[in] seconds  the length
 * @param[in] format   the format
 * @return the name with its extension
 */
juce::String fileNameFor( const juce::String &name, double seconds, FormatE format );

}   // namespace renderkit
