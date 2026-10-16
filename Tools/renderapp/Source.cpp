/**
 * @file Source.cpp
 * @brief Ephemeris Sets: the render app's source for Ephemeris -- its five styles, each composed into a piece or a
 *        concert with the dice of the moment, and the sets the player saved (.ephset files in Documents/Ephemeris, its
 *        subfolders too), each played back exactly: its seed, its lengths, its rerolls, its knobs.
 *
 * A rendering is what eph_render writes for the same seed and knobs: composed (composePiece, or composeConcert),
 * levelled (levelScore), the engine prepared at 48 kHz with blocks of 512 and loaded (the composer's sounds onto the
 * knobs, unless a saved set holds them), twenty seconds after the end for the rooms, faded in over two seconds and out
 * over the last ten (exportFade). The lengths in the table are the composed ones; the levelling renders the loudest
 * part of every piece first.
 * @author Rene Weller
 */
#include "RenderKit.h"
#include "eph/Engine.h"
#include "eph/Leveler.h"
#include "eph/Loudness.h"
#include "eph/Params.h"
#include "eph/SetFile.h"
#include "eph/compose/Composer.h"
#include "eph/compose/Style.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

namespace
{

constexpr double Rate = 48000.0;      ///< the files' rate
constexpr int    EngineBlock = 512;   ///< the engine's block (eph_render's)
constexpr double Tail = 20.0;         ///< s after the end, for the rooms (eph_render's)
constexpr int    Singers = 6;         ///< the tape choir's singers per key (eph_render's)
/** @brief The panel's "What": a piece, or a concert of so many minutes. */
const double ConcertMinutes[] = { 0.0, 30.0, 60.0, 90.0, 120.0, 180.0 };
/** @brief The panel's piece lengths, minutes (compose.piece_minutes). */
const double PieceMinutes[] = { 10.0, 16.0, 20.0, 25.0, 30.0, 40.0 };

/** @brief A saved set as the table shows it. */
struct Saved
{
    juce::File    m_file;            ///< the .ephset
    std::string   m_style;           ///< its style
    double        m_concert = 0.0;   ///< its concert length, minutes (0: a piece)
    std::uint64_t m_seed = 1;        ///< its seed
};

/** @brief What a row is: a style, or a saved set. */
struct Row
{
    int   m_style = -1;   ///< the style (StyleE), -1 for a saved set
    Saved m_saved;        ///< the saved set
};

/** @brief The knobs and the rest a composition needs. */
struct Setup
{
    std::uint64_t m_seed = 1;             ///< the seed
    double        m_minutes = 16.0;       ///< a piece's length
    double        m_concert = 0.0;        ///< a concert's length (0: a piece)
    eph::Curation m_curation;             ///< the rerolls
    bool          m_soundsKept = false;   ///< the knobs hold the composer's sounds (a saved set)
};

/**
 * @brief The knobs of a row: a style with the panel's lengths, or a saved set's own.
 * @param[in,out] p     the knobs (fresh)
 * @param[in]     row   the row
 * @param[in]     r     the request
 * @param[in]     seed  the dice for a style (renderkit::seedFor)
 * @param[out]    out   receives the seed, the lengths, the rerolls
 * @param[out]    why   receives why a saved set cannot be read
 * @return false when it cannot be set up
 */
bool knobsOf( eph::ParamStore &p, const Row &row, const renderkit::Request &r, std::uint64_t seed, Setup &out,
              juce::String &why )
{
    using namespace eph;
    if ( row.m_style >= 0 )
    {
        p.set( p.id( ModuleE::COMPOSE, 0, compose::STYLE ), static_cast<float>( row.m_style ) );
        out.m_seed = seed;
        out.m_minutes = PieceMinutes[std::clamp( r.option( "piece", 1 ), 0, 5 )];
        out.m_concert = ConcertMinutes[std::clamp( r.option( "what" ), 0, 5 )];
        return true;
    }
    SetFile     sf;
    std::string error;
    if ( !loadSet( row.m_saved.m_file.getFullPathName().toRawUTF8(), sf, p, &error ) )
    {
        why = error;
        return false;
    }
    out.m_seed = sf.m_seed;
    out.m_minutes = sf.m_minutes > 0.0 ? sf.m_minutes : p.get( p.id( ModuleE::COMPOSE, 0, compose::PIECE_MINUTES ) );
    out.m_concert = sf.m_concert > 0.0 ? sf.m_concert : p.get( p.id( ModuleE::COMPOSE, 0, compose::CONCERT_MINUTES ) );
    out.m_curation = sf.m_curation;
    out.m_soundsKept = sf.m_soundsInParams;
    return true;
}

/**
 * @brief Composes what the setup asks for, as eph_render does: a concert when its length is above 0, else a piece.
 * @param[in] p  the knobs
 * @param[in] s  the setup
 * @return the score
 */
eph::Score compose( const eph::ParamStore &p, const Setup &s )
{
    return s.m_concert > 0.0 ? eph::composeConcert( p, s.m_seed, s.m_concert, &s.m_curation )
                             : eph::composePiece( p, s.m_seed, s.m_minutes, 0, &s.m_curation );
}

/** @brief A piece or a concert being rendered: the engine offline, levelled first, faded at both ends. */
class EphemerisRendering : public renderkit::Rendering
{
public:
    /**
     * @brief Composes, levels and loads.
     * @param[in]  row   the row
     * @param[in]  r     the request
     * @param[in]  seed  the dice for a style (renderkit::seedFor)
     * @param[out] why   receives why it cannot be rendered
     */
    EphemerisRendering( const Row &row, const renderkit::Request &r, std::uint64_t seed, juce::String &why )
    {
        using namespace eph;
        m_engine = std::make_unique<Engine>();
        m_engine->setTapeSingers( Singers );
        ParamStore &p = m_engine->params();
        Setup       s;
        if ( !knobsOf( p, row, r, seed, s, why ) )
        {
            m_engine.reset();
            return;
        }
        Score score = compose( p, s );
        levelScore( score, p );   // every piece as loud as its style means (Leveler.h), as the plugin does
        m_engine->prepare( Rate, EngineBlock );
        m_engine->load( score, !s.m_soundsKept );
        m_seconds = m_engine->lengthSeconds() + Tail;
        m_frames = static_cast<std::int64_t>( std::llround( m_seconds * Rate ) );
        for ( const Marker &m : score.m_markers )
            m_marks.push_back( { score.m_tempo.secondsAt( m.m_beat ), juce::String( m.m_text ) } );
    }
    /** @brief Whether it could be set up. @return true when it renders */
    bool ok() const { return m_engine != nullptr; }
    double seconds() const override { return m_seconds; }
    void render( float *left, float *right, int frames ) override
    {
        for ( int done = 0; done < frames; )
        {   // in the engine's own blocks, as eph_render renders
            const int n = std::min( EngineBlock, frames - done );
            m_engine->process( left + done, right + done, n );
            done += n;
        }
        for ( int i = 0; i < frames; ++i )
        {   // eph_render's export fade: in over two seconds, out over the last ten
            const float g = eph::exportFade( m_done + i, m_frames, Rate );
            left[i] *= g;
            right[i] *= g;
        }
        m_done += frames;
        const std::lock_guard<std::mutex> hold( m_lock );
        m_time = static_cast<double>( m_done ) / Rate;
    }
    juce::String now() const override
    {
        const std::lock_guard<std::mutex> hold( m_lock );
        juce::String                      s;
        for ( const auto &[at, what] : m_marks )
            if ( at <= m_time ) s = what;
        return s;
    }

private:
    std::unique_ptr<eph::Engine>                 m_engine;          ///< the instrument offline
    double                                       m_seconds = 0.0;   ///< the length
    std::int64_t                                 m_frames = 0;      ///< the length in samples
    std::int64_t                                 m_done = 0;        ///< samples rendered
    std::vector<std::pair<double, juce::String>> m_marks;           ///< s -> the marker that begins there
    mutable std::mutex                           m_lock;            ///< guards m_time
    double                                       m_time = 0.0;      ///< s rendered
};

/** @brief Ephemeris' styles and saved sets for the render app. */
class EphemerisSource : public renderkit::Source
{
public:
    juce::String product() const override { return "Ephemeris"; }
    juce::String title() const override { return "Ephemeris Sets"; }
    juce::String subtitle() const override { return "Render pieces and concerts into files, faster than real time"; }
    juce::String noun() const override { return "set"; }
    renderkit::Palette palette() const override
    {
        renderkit::Palette c;
        c.m_back = juce::Colour( 0xff0b0e14 );
        c.m_panel = juce::Colour( 0xff1a2029 );
        c.m_row = juce::Colour( 0xff141920 );
        c.m_raised = juce::Colour( 0xff222a36 );
        c.m_edge = juce::Colour( 0xff2c3544 );
        c.m_chosen = juce::Colour( 0xff3d3424 );
        c.m_text = juce::Colour( 0xffe8dfcc );
        c.m_dim = juce::Colour( 0xff8e949f );
        c.m_accent = juce::Colour( 0xffe8a948 );
        return c;
    }
    std::vector<renderkit::Column> columns() const override
    {
        return { { "Kind", 90, false }, { "Style", 110, false }, { "Tempo", 110, true } };
    }
    std::vector<renderkit::Item> list() override
    {
        using namespace eph;
        std::vector<renderkit::Item> out;
        auto                         add = [&out, this]( renderkit::Item &&item, Row &&row )
        {
            const std::lock_guard<std::mutex> hold( m_lock );
            item.m_index = static_cast<int>( m_store.size() );
            m_store.push_back( std::make_shared<const Row>( std::move( row ) ) );
            out.push_back( std::move( item ) );
        };
        for ( int s = 0; s < static_cast<int>( StyleE::COUNT ); ++s )
        {
            const StyleProfile &prof = styleProfile( static_cast<StyleE>( s ) );
            renderkit::Item     item;
            item.m_name = StyleNames[s];
            item.m_kind = "Style";
            item.m_cells.add( "Style" );
            item.m_cells.add( StyleNames[s] );
            item.m_cells.add( juce::String( prof.m_bpmLow, 0 ) + " .. " + juce::String( prof.m_bpmHigh, 0 ) + " BPM" );
            item.m_keys = { std::nan( "" ), std::nan( "" ), static_cast<double>( prof.m_bpmLow ) };
            Row row;
            row.m_style = s;
            add( std::move( item ), std::move( row ) );
        }
        const juce::File docs =
            juce::File::getSpecialLocation( juce::File::userDocumentsDirectory ).getChildFile( "Ephemeris" );
        juce::Array<juce::File> files = docs.findChildFiles( juce::File::findFiles, true, "*.ephset" );
        files.sort();
        for ( const juce::File &f : files )
        {
            ParamStore  p;
            SetFile     sf;
            std::string error;
            if ( !loadSet( f.getFullPathName().toRawUTF8(), sf, p, &error ) ) continue;
            Row row;
            row.m_saved.m_file = f;
            row.m_saved.m_style = StyleNames[std::clamp( p.getInt( p.id( ModuleE::COMPOSE, 0, compose::STYLE ) ), 0,
                                                         static_cast<int>( StyleE::COUNT ) - 1 )];
            row.m_saved.m_concert =
                sf.m_concert > 0.0 ? sf.m_concert : p.get( p.id( ModuleE::COMPOSE, 0, compose::CONCERT_MINUTES ) );
            row.m_saved.m_seed = sf.m_seed;
            renderkit::Item item;
            item.m_name = f.getFileNameWithoutExtension();
            item.m_kind = "Yours";
            item.m_dice = false;
            item.m_cells.add( row.m_saved.m_concert > 0.0 ? "Your concert" : "Your piece" );
            item.m_cells.add( juce::String( row.m_saved.m_style ) );
            item.m_cells.add( {} );
            item.m_keys = { std::nan( "" ), std::nan( "" ), std::nan( "" ) };
            item.m_detail = f.getRelativePathFrom( docs );
            add( std::move( item ), std::move( row ) );
        }
        return out;
    }
    std::vector<renderkit::Option> options() const override
    {
        return { { "what",
                   "What",
                   "A single piece, or a concert: pieces one after another, the style wandering where the knobs say.",
                   { "A piece", "Concert, 30 min", "Concert, 1 hour", "Concert, 90 min", "Concert, 2 hours",
                     "Concert, 3 hours" },
                   0,
                   true },
                 { "piece",
                   "Piece",
                   "A single piece's length (compose.piece_minutes); a concert's pieces take their own.",
                   { "10 min", "16 min", "20 min", "25 min", "30 min", "40 min" },
                   1,
                   true } };
    }
    bool applies( const renderkit::Item &item, const juce::String &key ) const override
    {
        juce::ignoreUnused( key );
        return row( item )->m_style >= 0;   // a saved set brings its own
    }
    double length( const renderkit::Item &item, const renderkit::Request &r ) override
    {
        eph::ParamStore p;
        Setup           s;
        juce::String    why;
        if ( !knobsOf( p, *row( item ), r, renderkit::seedFor( item, r ), s, why ) ) return 0.0;
        const eph::Score score = compose( p, s );
        return score.m_tempo.secondsAt( score.m_lengthBeats ) + Tail;
    }
    juce::String describe( const renderkit::Item &item, const renderkit::Request &r ) override
    {
        const std::shared_ptr<const Row> rw = row( item );
        if ( rw->m_style < 0 )
        {
            const Saved &s = rw->m_saved;
            return juce::String( "Your " ) +
                   ( s.m_concert > 0.0 ? "concert of " + juce::String( s.m_concert, 0 ) + " min"
                                       : juce::String( "piece" ) ) +
                   ", " + juce::String( s.m_style ) + "\nseed " + juce::String( static_cast<juce::int64>( s.m_seed ) ) +
                   ", played as you saved it";
        }
        const eph::StyleProfile &prof = eph::styleProfile( static_cast<eph::StyleE>( rw->m_style ) );
        const int                what = std::clamp( r.option( "what" ), 0, 5 );
        juce::String s = juce::String( eph::StyleNames[rw->m_style] ) + ", " + juce::String( prof.m_bpmLow, 0 ) +
                         " .. " + juce::String( prof.m_bpmHigh, 0 ) + " BPM\n";
        if ( what == 0 )
            s << "A piece of about " << juce::String( PieceMinutes[std::clamp( r.option( "piece", 1 ), 0, 5 )], 0 )
              << " minutes, twenty seconds of its rooms after it";
        else
            s << "A concert of " << juce::String( ConcertMinutes[what], 0 ) << " minutes, from "
              << eph::StyleNames[rw->m_style];
        return s;
    }
    std::unique_ptr<renderkit::Rendering> open( const renderkit::Item &item, const renderkit::Request &r,
                                                juce::String &why ) override
    {
        auto rendering = std::make_unique<EphemerisRendering>( *row( item ), r, renderkit::seedFor( item, r ), why );
        if ( !rendering->ok() ) return nullptr;
        return rendering;
    }
    juce::String testItem() const override { return "Cosmic"; }

private:
    /**
     * @brief A row's style or saved set.
     * @param[in] item  the row
     * @return it (shared: a rendering may outlive a Refresh)
     */
    std::shared_ptr<const Row> row( const renderkit::Item &item ) const
    {
        const std::lock_guard<std::mutex> hold( m_lock );
        return m_store[static_cast<size_t>( item.m_index )];
    }
    mutable std::mutex                      m_lock;    ///< guards m_store
    std::vector<std::shared_ptr<const Row>> m_store;   ///< every row listed (only ever added to)
};

}   // namespace

std::unique_ptr<renderkit::Source> renderkit::makeSource()
{
    return std::make_unique<EphemerisSource>();
}
