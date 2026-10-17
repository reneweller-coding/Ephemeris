/**
 * @file RenderKit.cpp
 * @brief The render app's files, renderer, queue, command line, self-test and application (RenderKit.h).
 * @author Rene Weller
 */
#include "RenderKit.h"

#include "Mp3Writer.h"
#include "RenderWindow.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace renderkit
{

namespace
{

constexpr int Block = 4096;   ///< samples a call of the rendering

/** @brief Where the samples go: a JUCE writer behind a background thread, or the MP3 writer. */
class Sink
{
public:
    /**
     * @brief Opens the file.
     * @param[in]  format      the format
     * @param[in]  file        the file
     * @param[in]  sampleRate  samples a second
     * @param[out] why         receives why it failed
     * @return true when it is open
     */
    bool open( FormatE format, const juce::File &file, int sampleRate, juce::String &why )
    {
        file.deleteFile();
        file.getParentDirectory().createDirectory();
        if ( format == FormatE::MP3 )
        {
            std::string w;
            m_mp3 = std::make_unique<Mp3Writer>();
            if ( !m_mp3->open( file.getFullPathName().toStdString(), sampleRate, 320, w ) )
            {
                why = w;
                m_mp3.reset();
                return false;
            }
            return true;
        }
        std::unique_ptr<juce::AudioFormat> fmt;
        int                                bits = 24, quality = 0;
        if ( format == FormatE::WAV ) fmt = std::make_unique<juce::WavAudioFormat>();
        if ( format == FormatE::FLAC )
        {
            fmt = std::make_unique<juce::FlacAudioFormat>();
            quality = 5;
        }
        if ( format == FormatE::OGG )
        {
            fmt = std::make_unique<juce::OggVorbisAudioFormat>();
            bits = 16;
            quality = std::max( 0, fmt->getQualityOptions().indexOf( "320 kbps" ) );
        }
        if ( !fmt )
        {
            why = "no such format";
            return false;
        }
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream>( file );
        if ( !static_cast<juce::FileOutputStream *>( stream.get() )->openedOk() )
        {
            why = "cannot write " + file.getFullPathName();
            return false;
        }
        const auto                               options = juce::AudioFormatWriterOptions{}
                                                               .withSampleRate( sampleRate )
                                                               .withNumChannels( 2 )
                                                               .withBitsPerSample( bits )
                                                               .withQualityOptionIndex( quality );
        std::unique_ptr<juce::AudioFormatWriter> writer = fmt->createWriterFor( stream, options );
        if ( !writer )
        {
            why = juce::String( "no " ) + formatName( format ) + " writer for this file";
            return false;
        }
        m_thread.startThread();
        // encoding beside the rendering: eight seconds of room before write() has to wait
        m_writer =
            std::make_unique<juce::AudioFormatWriter::ThreadedWriter>( writer.release(), m_thread, 8 * sampleRate );
        return true;
    }
    /**
     * @brief Appends samples.
     * @param[in] L  left
     * @param[in] R  right
     * @param[in] n  samples
     * @return false when the file refused them
     */
    bool write( const float *L, const float *R, int n )
    {
        if ( m_mp3 ) return m_mp3->write( L, R, n );
        const float *channels[2] = { L, R };
        for ( int tries = 0; !m_writer->write( channels, n ); ++tries )
        {   // the encoder behind: give it a moment
            if ( tries > 20000 ) return false;
            juce::Thread::sleep( 1 );
        }
        return true;
    }
    /**
     * @brief Finishes the file.
     * @return true when it was finished cleanly
     */
    bool close()
    {
        bool ok = true;
        if ( m_mp3 ) ok = m_mp3->close();
        m_mp3.reset();
        m_writer.reset();   // flushes what is left and closes the file
        m_thread.stopThread( 10000 );
        return ok;
    }

private:
    std::unique_ptr<Mp3Writer>                               m_mp3;   ///< the MP3 writer, or null
    juce::TimeSliceThread                                    m_thread{ "render app writer" };   ///< encodes
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> m_writer;   ///< the JUCE writer behind it, or null
};

/**
 * @brief The command line's value after a flag.
 * @param[in] args  the arguments
 * @param[in] flag  the flag
 * @param[in] dflt  when it is not there
 * @return the value
 */
juce::String argument( const juce::StringArray &args, const juce::String &flag, const juce::String &dflt )
{
    const int i = args.indexOf( flag );
    return i >= 0 && i + 1 < args.size() ? args[i + 1] : dflt;
}

/**
 * @brief The request of the panel's first state: the dice of the command line, every option at its default, or as
 *        the command line names it (--KEY VALUE: a menu's entry by its text or its number, a toggle by on/off/1/0).
 * @param[in] source  the instrument
 * @param[in] args    the command line
 * @return the request
 */
Request requestFrom( const Source &source, const juce::StringArray &args )
{
    Request r;
    r.m_seed = static_cast<std::uint64_t>( argument( args, "--seed", "1" ).getLargeIntValue() );
    r.m_limit = argument( args, "--seconds", "0" ).getDoubleValue();
    for ( const Option &op : source.options() )
    {
        int                value = op.m_default;
        const juce::String v = argument( args, "--" + op.m_key, {} );
        if ( v.isNotEmpty() )
        {
            if ( op.m_choices.isEmpty() )
                value = v == "1" || v.equalsIgnoreCase( "on" ) || v.equalsIgnoreCase( "true" ) ||
                        v.equalsIgnoreCase( "yes" );
            else
            {
                const int k = op.m_choices.indexOf( v, true );
                value = k >= 0 ? k : std::clamp( v.getIntValue(), 0, op.m_choices.size() - 1 );
            }
        }
        r.m_options[op.m_key] = value;
    }
    return r;
}

/**
 * @brief The format a command line names.
 * @param[in] args  the command line
 * @return it (WAV when it names none)
 */
FormatE formatFrom( const juce::StringArray &args )
{
    const juce::String f = argument( args, "--format", "wav" ).toLowerCase();
    return f == "flac" ? FormatE::FLAC : f == "ogg" ? FormatE::OGG : f == "mp3" ? FormatE::MP3 : FormatE::WAV;
}

/**
 * @brief --list: every row, its kind and the length of its rendering.
 * @param[in,out] source  the instrument
 * @param[in]     args    the command line (the dice, the options)
 * @return 0
 */
int listCommand( Source &source, const juce::StringArray &args )
{
    const Request r = requestFrom( source, args );
    for ( const Item &it : source.list() )
        std::printf( "%-48s %-14s %10s\n", it.m_name.toRawUTF8(), it.m_kind.toRawUTF8(),
                     clockText( source.length( it, r ) ).toRawUTF8() );
    return 0;
}

/**
 * @brief --catalog: what the instrument offers, as one JSON document for the programs that drive it (Heliacal,
 *        17.10.2026): the product, the columns, the options with their choices and defaults, and every row with its
 *        kind, its cells, whether the dice change it, the options that apply to it and the length of its rendering
 *        for the dice and the options of the command line. The seed is text: a 64-bit number is no JSON number.
 * @param[in,out] source  the instrument
 * @param[in]     args    the command line (the dice, the options)
 * @return 0
 */
int catalogCommand( Source &source, const juce::StringArray &args )
{
    const Request r = requestFrom( source, args );
    auto         *root = new juce::DynamicObject();
    root->setProperty( "product", source.product() );
    root->setProperty( "title", source.title() );
    root->setProperty( "noun", source.noun() );
    root->setProperty( "seed", juce::String( static_cast<juce::int64>( r.m_seed ) ) );
    juce::Array<juce::var> columns;
    for ( const Column &c : source.columns() )
    {
        auto *o = new juce::DynamicObject();
        o->setProperty( "name", c.m_name );
        o->setProperty( "right", c.m_right );
        columns.add( juce::var( o ) );
    }
    root->setProperty( "columns", columns );
    const std::vector<Option> options = source.options();
    juce::Array<juce::var>    ops;
    for ( const Option &op : options )
    {
        auto                  *o = new juce::DynamicObject();
        juce::Array<juce::var> choices;
        for ( const juce::String &c : op.m_choices ) choices.add( c );
        o->setProperty( "key", op.m_key );
        o->setProperty( "label", op.m_label );
        o->setProperty( "tooltip", op.m_tooltip );
        o->setProperty( "choices", choices );
        o->setProperty( "default", op.m_default );
        o->setProperty( "length", op.m_length );
        ops.add( juce::var( o ) );
    }
    root->setProperty( "options", ops );
    juce::Array<juce::var> items;
    for ( const Item &it : source.list() )
    {
        auto                  *o = new juce::DynamicObject();
        juce::Array<juce::var> cells, applies;
        for ( const juce::String &c : it.m_cells ) cells.add( c );
        for ( const Option &op : options )
            if ( source.applies( it, op.m_key ) ) applies.add( op.m_key );
        o->setProperty( "name", it.m_name );
        o->setProperty( "kind", it.m_kind );
        o->setProperty( "cells", cells );
        o->setProperty( "detail", it.m_detail );
        o->setProperty( "dice", it.m_dice );
        o->setProperty( "applies", applies );
        o->setProperty( "length", source.length( it, r ) );
        items.add( juce::var( o ) );
    }
    root->setProperty( "items", items );
    std::fputs( juce::JSON::toString( juce::var( root ), true ).toRawUTF8(), stdout );
    std::fputs( "\n", stdout );
    std::fflush( stdout );
    return 0;
}

/**
 * @brief --render: one row without the window, its progress printed.
 * @param[in,out] source  the instrument
 * @param[in]     args    the command line
 * @return 0 when it was written
 */
int renderCommand( Source &source, const juce::StringArray &args )
{
    const juce::String name = argument( args, "--render", {} );
    for ( const Item &item : source.list() )
    {
        if ( item.m_name != name ) continue;
        Job job;
        job.m_item = item;
        job.m_request = requestFrom( source, args );
        job.m_format = formatFrom( args );
        if ( !formatAvailable( job.m_format ) )
        {
            std::printf( "%s is not written on this system\n", formatName( job.m_format ) );
            return 1;
        }
        const juce::String out = argument( args, "--out", {} );
        if ( out.isNotEmpty() )
            job.m_file = juce::File::getCurrentWorkingDirectory().getChildFile( out );
        else
            job.m_folder = juce::File::getCurrentWorkingDirectory();
        std::printf( "%s: %s\n", name.toRawUTF8(), clockText( source.length( item, job.m_request ) ).toRawUTF8() );
        std::fflush( stdout );
        Renderer     r;
        juce::File   file;
        juce::String why;
        // --progress: the seconds rendered, the seconds in all and the speed, twice a second, for a program that
        // drives this one (Heliacal) -- read where the renderer asks whether to stop, between two blocks.
        const bool progress = args.contains( "--progress" );
        auto       last = std::chrono::steady_clock::now();
        const auto ask = [&]()
        {
            const auto t = std::chrono::steady_clock::now();
            if ( progress && t - last > std::chrono::milliseconds( 500 ) )
            {
                last = t;
                std::printf( "progress %.1f %.1f %.2f\n", r.m_done.load(), r.m_total.load(), r.m_speed.load() );
                std::fflush( stdout );
            }
            return false;
        };
        // --cues, --chapters: the time line beside the sound (CueSheet), for Kaleidoscope and the video's chapters
        const juce::String cues = argument( args, "--cues", {} ), chapters = argument( args, "--chapters", {} );
        CueSheet           sheet;
        const bool         ok =
            r.render( source, job, ask, file, why, cues.isNotEmpty() || chapters.isNotEmpty() ? &sheet : nullptr );
        if ( ok && cues.isNotEmpty() )
            sheet.writeCues( juce::File::getCurrentWorkingDirectory().getChildFile( cues ),
                             source.product() + ", " + item.m_name );
        if ( ok && chapters.isNotEmpty() )
            sheet.writeChapters( juce::File::getCurrentWorkingDirectory().getChildFile( chapters ) );
        if ( ok && ( cues.isNotEmpty() || chapters.isNotEmpty() ) )
            std::printf( "time line: %d cues, %d chapters\n", static_cast<int>( sheet.cues() ),
                         static_cast<int>( sheet.chapters() ) );
        if ( ok )
            std::printf( "%s written into %s, %.1fx real time\n", clockText( r.m_total.load() ).toRawUTF8(),
                         file.getFullPathName().toRawUTF8(), r.m_speed.load() );
        else
            std::printf( "failed: %s\n", why.toRawUTF8() );
        return ok ? 0 : 1;
    }
    std::printf( "no %s named %s (--list names them)\n", source.noun().toRawUTF8(), name.toRawUTF8() );
    return 1;
}

/**
 * @brief The self-test's row: the source's, else the first.
 * @param[in,out] source  the instrument
 * @param[out]    item    receives it
 * @return false when there is none
 */
bool testItem( Source &source, Item &item )
{
    const std::vector<Item> items = source.list();
    for ( const Item &it : items )
        if ( it.m_name == source.testItem() )
        {
            item = it;
            return true;
        }
    if ( items.empty() ) return false;
    item = items.front();
    return true;
}

/**
 * @brief A file's length and level, read back.
 * @param[in]  formats  the readers
 * @param[in]  file     the file
 * @param[out] seconds  receives its length
 * @param[out] buffer   receives its samples (null: not wanted)
 * @return its RMS level, dBFS (-200 when it cannot be read)
 */
double readBack( juce::AudioFormatManager &formats, const juce::File &file, double &seconds,
                 juce::AudioBuffer<float> *buffer )
{
    seconds = 0.0;
    std::unique_ptr<juce::AudioFormatReader> reader{ formats.createReaderFor( file ) };
    if ( !reader ) return -200.0;
    seconds = static_cast<double>( reader->lengthInSamples ) / reader->sampleRate;
    juce::AudioBuffer<float> buf( 2, static_cast<int>( reader->lengthInSamples ) );
    reader->read( &buf, 0, buf.getNumSamples(), 0, true, true );
    const double rms =
        0.5 * ( buf.getRMSLevel( 0, 0, buf.getNumSamples() ) + buf.getRMSLevel( 1, 0, buf.getNumSamples() ) );
    if ( buffer ) *buffer = std::move( buf );
    return 20.0 * std::log10( std::max( rms, 1e-10 ) );
}

/**
 * @brief Every format: the test row rendered once as the window renders it (into a WAV), read back, its length and
 *        level checked; then the same samples written in every other format, each read back and checked.
 * @param[in,out] source   the instrument
 * @param[in]     seconds  how long the file
 * @return the number of formats that failed
 */
int formatsTest( Source &source, double seconds )
{
    Item item;
    if ( !testItem( source, item ) )
    {
        std::printf( "  FAIL  nothing to render\n" );
        return 1;
    }
    const juce::File dir =
        juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( source.product() + "RenderAppTest" );
    dir.deleteRecursively();
    dir.createDirectory();
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::printf( "%s: the test renders %s\n", source.title().toRawUTF8(), item.m_name.toRawUTF8() );
    Job job;
    job.m_item = item;
    job.m_request = requestFrom( source, {} );
    job.m_request.m_limit = seconds;
    job.m_format = FormatE::WAV;
    job.m_file = dir.getChildFile( "test.wav" );
    Renderer                 r;
    juce::File               file;
    juce::String             why;
    const auto               t0 = std::chrono::steady_clock::now();
    const bool               rendered = r.render( source, job, [] { return false; }, file, why );
    const double             took = std::chrono::duration<double>( std::chrono::steady_clock::now() - t0 ).count();
    juce::AudioBuffer<float> samples;
    double                   length = 0.0;
    const double             db = rendered ? readBack( formats, file, length, &samples ) : -200.0;
    int                      failures = 0;
    bool                     ok = rendered && std::abs( length - seconds ) < 0.5 && db > -70.0;
    failures += ok ? 0 : 1;
    std::printf( "  %s  %-26s %5.2f s, %6.1f dBFS, %.1fx real time (set up and rendered in %.1f s)%s%s\n",
                 ok ? "ok  " : "FAIL", formatName( FormatE::WAV ), length, db, r.m_speed.load(), took,
                 why.isEmpty() ? "" : ": ", why.toRawUTF8() );
    for ( int f = 1; ok && f < static_cast<int>( FormatE::COUNT ); ++f )
    {   // the same samples through every other writer
        const FormatE format = static_cast<FormatE>( f );
        if ( !formatAvailable( format ) ) continue;
        const juce::File to = dir.getChildFile( juce::String( "test" ) + formatExtension( format ) );
        Sink             sink;
        juce::String     w;
        bool             written = sink.open( format, to, source.sampleRate(), w );
        for ( int at = 0; written && at < samples.getNumSamples(); at += Block )
        {
            const int n = std::min( Block, samples.getNumSamples() - at );
            written = sink.write( samples.getReadPointer( 0, at ), samples.getReadPointer( 1, at ), n );
        }
        written = sink.close() && written;
        double       len = 0.0;
        const double level = written ? readBack( formats, to, len, nullptr ) : -200.0;
        const bool   good = written && std::abs( len - length ) < 0.1 && std::abs( level - db ) < 1.0;
        failures += good ? 0 : 1;
        std::printf( "  %s  %-26s %5.2f s, %6.1f dBFS%s%s\n", good ? "ok  " : "FAIL", formatName( format ), len, level,
                     w.isEmpty() ? "" : ": ", w.toRawUTF8() );
    }
    dir.deleteRecursively();
    return failures;
}

/** @brief The queue test's tone: a sine, rendered at about eighty times real time (a millisecond's nap a block). */
class ToneRendering : public Rendering
{
public:
    /**
     * @brief A tone of so many seconds.
     * @param[in] seconds  its length
     */
    explicit ToneRendering( double seconds ) : m_seconds( seconds ) {}
    double seconds() const override { return m_seconds; }
    void render( float *left, float *right, int frames ) override
    {
        for ( int i = 0; i < frames; ++i )
        {
            left[i] = right[i] = 0.25f * static_cast<float>( std::sin( m_phase ) );
            m_phase += 2.0 * 3.14159265358979 * 220.0 / 48000.0;
        }
        juce::Thread::sleep( 1 );
    }

private:
    double m_seconds;       ///< the length
    double m_phase = 0.0;   ///< the sine's phase
};

/** @brief The queue test's source: one row, a tone as long as the request's limit. */
class ToneSource : public Source
{
public:
    juce::String product() const override { return "RenderKit"; }
    juce::String title() const override { return "tone"; }
    juce::String subtitle() const override { return {}; }
    juce::String noun() const override { return "tone"; }
    std::vector<Column> columns() const override { return {}; }
    std::vector<Item> list() override { return { Item{ "tone", "Tone", {}, {}, false, 0, {} } }; }
    std::vector<Option> options() const override { return {}; }
    double length( const Item &, const Request &r ) override { return r.m_limit; }
    juce::String describe( const Item &, const Request & ) override { return {}; }
    std::unique_ptr<Rendering> open( const Item &, const Request &r, juce::String & ) override
    {
        return std::make_unique<ToneRendering>( r.m_limit );
    }
};

/**
 * @brief The window's queue as the window drives it, on a tone of the kit's own: two renderings one after the other,
 *        then one cancelled under way (its unfinished file removed).
 * @return 0 when the queue did all three right
 */
int queueTest()
{
    ToneSource       source;
    const Item       item = source.list().front();
    const juce::File dir =
        juce::File::getSpecialLocation( juce::File::tempDirectory ).getChildFile( "RenderKitQueueTest" );
    dir.deleteRecursively();
    dir.createDirectory();
    std::vector<Job> jobs( 2 );
    for ( size_t k = 0; k < jobs.size(); ++k )
    {
        jobs[k].m_item = item;
        jobs[k].m_request.m_limit = 3.0;
        jobs[k].m_file = dir.getChildFile( "q" + juce::String( static_cast<int>( k ) ) + ".wav" );
    }
    RenderQueue  q( source );
    juce::String name, message;
    int          waiting = 0, finished = 0;
    q.add( jobs );
    for ( int t = 0; t < 600 && ( q.status( name, waiting, finished ) || waiting > 0 || finished < 2 ); ++t )
        juce::Thread::sleep( 50 );
    message = q.takeMessage();
    const bool both = finished == 2 && jobs[0].m_file.existsAsFile() && jobs[1].m_file.existsAsFile();
    Job        long_ = jobs[0];
    long_.m_request.m_limit = 600.0;
    long_.m_file = dir.getChildFile( "cancelled.wav" );
    q.add( { long_ } );
    for ( int t = 0; t < 600 && ( q.m_renderer.m_total.load() < 100.0 || q.m_renderer.m_done.load() < 1.0 ); ++t )
        juce::Thread::sleep( 25 );   // until the long one is a second in
    q.cancelAll();
    for ( int t = 0; t < 400 && q.status( name, waiting, finished ); ++t ) juce::Thread::sleep( 25 );
    const juce::String last = q.takeMessage();
    const bool         cancelled = !q.status( name, waiting, finished ) && last.endsWith( "cancelled" ) &&
                                   !long_.m_file.existsAsFile() && finished == 2;
    std::printf( "  %s  queue: two after each other (%s), one cancelled under way (%s)\n",
                 both && cancelled ? "ok  " : "FAIL", message.toRawUTF8(), last.toRawUTF8() );
    dir.deleteRecursively();
    return both && cancelled ? 0 : 1;
}

/** @brief The window. */
class AppWindow : public juce::DocumentWindow
{
public:
    /**
     * @brief The window for an instrument.
     * @param[in,out] source  the instrument
     */
    explicit AppWindow( Source &source )
        : juce::DocumentWindow( source.title(), source.palette().m_back, juce::DocumentWindow::allButtons )
    {
        setUsingNativeTitleBar( true );
        setContentOwned( new RenderWindow( source ), true );
        setResizable( true, true );
        setResizeLimits( 960, 680, 4000, 3000 );
        centreWithSize( getWidth(), getHeight() );
        setVisible( true );
    }
    /** @brief The close button quits. */
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

/** @brief The application: the window, or a command line's work. */
class RenderApplication : public juce::JUCEApplication
{
public:
    /** @brief Its name. @return the instrument's title */
    const juce::String getApplicationName() override { return source().title(); }
    /** @brief Its version. @return the instrument's */
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    /**
     * @brief Several at once: a command line's rendering beside an open window (else it would hand its arguments to
     *        the window and quit).
     * @return true
     */
    bool moreThanOneInstanceAllowed() override { return true; }
    /**
     * @brief Opens the window, or does what the command line asks and quits.
     * @param[in] commandLine  the arguments
     */
    void initialise( const juce::String &commandLine ) override
    {
        juce::StringArray args = juce::StringArray::fromTokens( commandLine, true );
        for ( juce::String &a : args ) a = a.unquoted();   // a name with spaces comes in its quotes
        if ( args.contains( "--selftest" ) )
        {
            const double seconds = argument( args, "--selftest", "6" ).getDoubleValue();
            const int    formats = formatsTest( source(), seconds > 0.0 ? seconds : 6.0 );
            const int    queue = queueTest();
            std::printf( formats + queue == 0 ? "render app passed\n" : "render app: %d FAILED\n", formats + queue );
            setApplicationReturnValue( formats + queue == 0 ? 0 : 1 );
            quit();
            return;
        }
        if ( args.contains( "--list" ) )
        {
            setApplicationReturnValue( listCommand( source(), args ) );
            quit();
            return;
        }
        if ( args.contains( "--catalog" ) )
        {
            setApplicationReturnValue( catalogCommand( source(), args ) );
            quit();
            return;
        }
        if ( args.contains( "--render" ) )
        {
            setApplicationReturnValue( renderCommand( source(), args ) );
            quit();
            return;
        }
        if ( args.contains( "--shot" ) )
        {   // the window as a picture, a row chosen (the manual's)
            RenderWindow w( source() );
            w.choose( argument( args, "--choose", {} ) );
            w.settle();
            const juce::Image shot = w.createComponentSnapshot( w.getLocalBounds() );
            juce::File        out =
                juce::File::getCurrentWorkingDirectory().getChildFile( argument( args, "--shot", "app.png" ) );
            out.deleteFile();
            juce::FileOutputStream s( out );
            juce::PNGImageFormat   png;
            setApplicationReturnValue( s.openedOk() && png.writeImageToStream( shot, s ) ? 0 : 1 );
            quit();
            return;
        }
        m_window = std::make_unique<AppWindow>( source() );
    }
    /** @brief Closes the window. */
    void shutdown() override
    {
        m_window.reset();
        m_source.reset();
    }
    /** @brief Quits. */
    void systemRequestedQuit() override { quit(); }

private:
    /** @brief The instrument, made the first time it is asked for. @return it */
    Source &source()
    {
        if ( !m_source ) m_source = makeSource();
        return *m_source;
    }
    std::unique_ptr<Source>    m_source;   ///< the instrument
    std::unique_ptr<AppWindow> m_window;   ///< the window, when there is one
};

}   // namespace

const char *formatName( FormatE f )
{
    switch ( f )
    {
    case FormatE::WAV: return "WAV (24 bit)";
    case FormatE::FLAC: return "FLAC (24 bit, lossless)";
    case FormatE::OGG: return "Ogg Vorbis (320 kbit/s)";
    case FormatE::MP3: return "MP3 (320 kbit/s)";
    default: return "?";
    }
}

const char *formatExtension( FormatE f )
{
    switch ( f )
    {
    case FormatE::WAV: return ".wav";
    case FormatE::FLAC: return ".flac";
    case FormatE::OGG: return ".ogg";
    case FormatE::MP3: return ".mp3";
    default: return ".wav";
    }
}

bool formatAvailable( FormatE f )
{
    return f != FormatE::COUNT && ( f != FormatE::MP3 || Mp3Writer::available() );
}

juce::String clockText( double seconds )
{
    const long long s = static_cast<long long>( std::llround( std::max( seconds, 0.0 ) ) );
    const long long h = s / 3600, m = ( s / 60 ) % 60, r = s % 60;
    return h > 0 ? juce::String::formatted( "%lld:%02lld:%02lld", h, m, r )
                 : juce::String::formatted( "%lld:%02lld", m, r );
}

std::uint64_t seedFor( const Item &item, const Request &r )
{
    std::uint64_t h = 1469598103934665603ull;
    for ( const char *c = item.m_name.toRawUTF8(); *c != 0; ++c )
        h = ( h ^ static_cast<unsigned char>( *c ) ) * 1099511628211ull;
    const std::uint64_t s = ( r.m_seed * 0x9E3779B97F4A7C15ull ) ^ h;
    return s != 0 ? s : 1;
}

juce::String fileNameFor( const juce::String &name, double seconds, FormatE format )
{
    return juce::File::createLegalFileName( name + " (" + clockText( seconds ).replace( ":", "-" ) + ")" ) +
           formatExtension( format );
}

// ---- the renderer

juce::String Renderer::now() const
{
    const std::lock_guard<std::mutex> hold( m_lock );
    return m_now;
}

bool Renderer::render( Source &source, const Job &job, const std::function<bool()> &cancelled, juce::File &file,
                       juce::String &why, CueSheet *sheet )
{
    m_done.store( 0.0 );
    m_total.store( 0.0 );
    m_speed.store( 0.0 );
    m_setup.store( true );
    {
        const std::lock_guard<std::mutex> hold( m_lock );
        m_now.clear();
    }
    std::unique_ptr<Rendering> rendering = source.open( job.m_item, job.m_request, why );
    m_setup.store( false );
    if ( !rendering )
    {
        if ( why.isEmpty() ) why = "it cannot be rendered";
        return false;
    }
    const double whole = rendering->seconds();
    const double total = job.m_request.m_limit > 0.0 ? std::min( job.m_request.m_limit, whole ) : whole;
    if ( total <= 0.0 )
    {
        why = "it lasts no time";
        return false;
    }
    m_total.store( total );
    if ( sheet != nullptr )
    {   // the time line as composed, cut where the rendering ends
        rendering->timeline( *sheet );
        sheet->crop( total );
    }
    file = job.m_file != juce::File()
               ? job.m_file
               : job.m_folder.getChildFile( fileNameFor( job.m_item.m_name, total, job.m_format ) )
                     .getNonexistentSibling( false );
    const double rate = source.sampleRate();
    Sink         sink;
    if ( !sink.open( job.m_format, file, source.sampleRate(), why ) ) return false;
    const long long frames = static_cast<long long>( std::llround( total * rate ) );
    const long long fadeFrames =
        static_cast<long long>( std::max( 0.0, std::min( rendering->fadeSeconds(), total * 0.5 ) ) * rate );
    std::vector<float> L( Block ), R( Block );
    const auto         t0 = std::chrono::steady_clock::now();
    auto               told = t0;
    long long          done = 0;
    bool               ok = true;
    while ( done < frames )
    {
        if ( cancelled() )
        {
            why = "cancelled";
            ok = false;
            break;
        }
        const int n = static_cast<int>( std::min<long long>( Block, frames - done ) );
        rendering->render( L.data(), R.data(), n );
        for ( int i = 0; i < n; ++i )
        {   // nothing that is not a number reaches a file
            if ( !std::isfinite( L[static_cast<size_t>( i )] ) ) L[static_cast<size_t>( i )] = 0.0f;
            if ( !std::isfinite( R[static_cast<size_t>( i )] ) ) R[static_cast<size_t>( i )] = 0.0f;
        }
        if ( done + n > frames - fadeFrames )
            for ( int i = 0; i < n; ++i )
            {   // the end faded out on a raised cosine
                const double u = std::clamp( static_cast<double>( frames - ( done + i ) ) /
                                                 static_cast<double>( std::max<long long>( fadeFrames, 1 ) ),
                                             0.0, 1.0 );
                const float  g = static_cast<float>( 0.5 - 0.5 * std::cos( 3.14159265358979 * u ) );
                L[static_cast<size_t>( i )] *= g;
                R[static_cast<size_t>( i )] *= g;
            }
        if ( !sink.write( L.data(), R.data(), n ) )
        {
            why = "the file could not be written (is the disk full?)";
            ok = false;
            break;
        }
        done += n;
        const auto   t = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>( t - t0 ).count();
        m_done.store( static_cast<double>( done ) / rate );
        m_speed.store( elapsed > 0.0 ? static_cast<double>( done ) / rate / elapsed : 0.0 );
        if ( std::chrono::duration<double>( t - told ).count() > 0.25 )
        {   // what plays now, four times a second
            told = t;
            const juce::String                now = rendering->now();
            const std::lock_guard<std::mutex> hold( m_lock );
            m_now = now;
        }
    }
    if ( !sink.close() && ok )
    {
        why = "the file could not be finished";
        ok = false;
    }
    if ( ok )
    {   // read back: JUCE's writers keep a failed write to themselves (a full disk, a FAT32 stick past 4 GB), so a file
        // cut short is found by its length (MP3's encoder pads its end: never shorter, never much)
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        const std::unique_ptr<juce::AudioFormatReader> reader{ formats.createReaderFor( file ) };
        if ( !reader || reader->lengthInSamples + 1 < frames )
        {
            why = "the file came out shorter than rendered (is the disk full?)";
            ok = false;
        }
    }
    return ok;
}

// ---- the time line

void CueSheet::section( double seconds, const juce::String &kind, double energy, bool drop )
{
    m_cues.push_back( { seconds, 1, "/phos/section", { kind, juce::String( std::clamp( energy, 0.0, 1.0 ), 2 ) } } );
    if ( drop ) m_cues.push_back( { seconds, 4, "/phos/drop", {} } );
}

void CueSheet::key( double seconds, const juce::String &key )
{
    m_cues.push_back( { seconds, 0, "/phos/key", { key } } );
}

void CueSheet::grid( const std::function<double( double )> &secondsAtBeat, double beats, int beatsPerBar )
{
    const int n = static_cast<int>( std::floor( beats ) );
    for ( int b = 0; b < n; ++b )
    {
        const double t = secondsAtBeat( static_cast<double>( b ) );
        if ( b % std::max( 1, beatsPerBar ) == 0 )
            m_cues.push_back( { t, 2, "/phos/bar", { juce::String( b / std::max( 1, beatsPerBar ) ) } } );
        m_cues.push_back( { t, 3, "/phos/beat", { juce::String( b ) } } );
    }
}

void CueSheet::chapter( double seconds, const juce::String &title )
{
    m_chapters.push_back( { seconds, title } );
}

void CueSheet::crop( double seconds )
{
    m_cues.erase(
        std::remove_if( m_cues.begin(), m_cues.end(), [seconds]( const Cue &c ) { return c.m_seconds >= seconds; } ),
        m_cues.end() );
    m_chapters.erase( std::remove_if( m_chapters.begin(), m_chapters.end(),
                                      [seconds]( const auto &c ) { return c.first >= seconds; } ),
                      m_chapters.end() );
}

bool CueSheet::writeCues( const juce::File &file, const juce::String &title ) const
{
    std::vector<Cue> sorted = m_cues;
    std::stable_sort( sorted.begin(), sorted.end(), []( const Cue &a, const Cue &b )
                      { return a.m_seconds != b.m_seconds ? a.m_seconds < b.m_seconds : a.m_order < b.m_order; } );
    juce::String text = "# " + title + ": score cues for KaleidoscopeEnhanced -k (seconds, address, arguments)\n";
    for ( const Cue &c : sorted )
    {
        text << juce::String( std::max( 0.0, c.m_seconds ), 3 ) << "\t" << c.m_address;
        for ( const juce::String &a : c.m_args ) text << "\t" << a;
        text << "\n";
    }
    return file.replaceWithText( text, false, false, "\n" );
}

bool CueSheet::writeChapters( const juce::File &file ) const
{
    std::vector<std::pair<double, juce::String>> sorted = m_chapters;
    std::stable_sort( sorted.begin(), sorted.end(), []( const auto &a, const auto &b ) { return a.first < b.first; } );
    juce::String text;
    for ( const auto &[at, title] : sorted )
        text << juce::String( std::max( 0.0, at ), 3 ) << "\t" << title.replace( "\t", " " ).replace( "\n", " " )
             << "\n";
    return file.replaceWithText( text, false, false, "\n" );
}

// ---- the queue

RenderQueue::RenderQueue( Source &source ) : juce::Thread( "render app", 64u << 20 ), m_source( source ) {}

RenderQueue::~RenderQueue()
{
    cancelAll();
    signalThreadShouldExit();
    m_wake.signal();
    stopThread( 60000 );
}

void RenderQueue::add( const std::vector<Job> &jobs )
{
    {
        const std::lock_guard<std::mutex> hold( m_lock );
        for ( const Job &j : jobs ) m_jobs.push_back( j );
    }
    if ( !isThreadRunning() ) startThread();   // once: it then waits for work until the window closes
    m_wake.signal();
}

void RenderQueue::cancelAll()
{
    const std::lock_guard<std::mutex> hold( m_lock );
    m_jobs.clear();
    m_cancel.store( true );   // the one under way (a job taken later clears it, under the same lock)
}

bool RenderQueue::status( juce::String &name, int &waiting, int &finished )
{
    const std::lock_guard<std::mutex> hold( m_lock );
    name = m_current;
    waiting = static_cast<int>( m_jobs.size() );
    finished = m_finished;
    return m_current.isNotEmpty();
}

juce::String RenderQueue::takeMessage()
{
    const std::lock_guard<std::mutex> hold( m_lock );
    juce::String                      m = m_message;
    m_message.clear();
    return m;
}

void RenderQueue::run()
{
    while ( !threadShouldExit() )
    {
        Job  job;
        bool taken = false;
        {
            const std::lock_guard<std::mutex> hold( m_lock );
            if ( m_jobs.empty() )
                m_current.clear();
            else
            {
                job = m_jobs.front();
                m_jobs.erase( m_jobs.begin() );
                m_current = job.m_item.m_name;
                m_cancel.store( false );
                taken = true;
            }
        }
        if ( !taken )
        {   // nothing to do: wait for add() (or the window closing)
            m_wake.wait( 500 );
            continue;
        }
        juce::File   file;
        juce::String why;
        const bool   ok =
            m_renderer.render( m_source, job, [this] { return m_cancel.load() || threadShouldExit(); }, file, why );
        const std::lock_guard<std::mutex> hold( m_lock );
        if ( ok )
        {
            ++m_finished;
            m_message = "Written: " + file.getFileName() + " (" + clockText( m_renderer.m_total.load() ) + ", " +
                        juce::String( m_renderer.m_speed.load(), 1 ) + "x real time)";
        }
        else
        {
            m_message = job.m_item.m_name + ": " + why;
            if ( why == "cancelled" && file != juce::File() )
                file.deleteFile();   // a part of a rendering is no rendering of it
        }
        m_current.clear();
    }
}

}   // namespace renderkit

START_JUCE_APPLICATION( renderkit::RenderApplication )
