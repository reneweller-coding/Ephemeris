/**
 * @file Midi.cpp
 * @brief Standard MIDI File writer.
 * @author Rene Weller
 */
#include "eph/Midi.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace eph
{

namespace
{

/** @brief One event of a track before it is serialised: absolute tick, order for equal ticks, bytes. */
struct Ev
{
    int64_t              m_tick;    ///< when, in ticks
    int                  m_order;   ///< at equal ticks: meta first, note-offs before note-ons
    std::vector<uint8_t> m_bytes;   ///< the status and data bytes
};

/**
 * @brief Beat @p beat in ticks (MidiPpq per beat).
 * @param[in] beat  the position in beats
 * @return the position in ticks
 */
int64_t toTick( double beat )
{
    return static_cast<int64_t>( std::llround( beat * MidiPpq ) );
}

/**
 * @brief Appends @p v as a variable-length quantity.
 * @param[in,out] out  the bytes, appended to
 * @param[in]     v    the value
 */
void putVarLen( std::vector<uint8_t> &out, uint32_t v )
{
    uint8_t buf[5];
    int     n = 0;
    buf[n++] = static_cast<uint8_t>( v & 0x7F );
    while ( ( v >>= 7 ) != 0 ) buf[n++] = static_cast<uint8_t>( 0x80 | ( v & 0x7F ) );
    while ( n > 0 ) out.push_back( buf[--n] );
}

/**
 * @brief Appends @p v big-endian.
 * @param[in,out] out  the bytes, appended to
 * @param[in]     v    the value
 */
void put32( std::vector<uint8_t> &out, uint32_t v )
{
    for ( int s = 24; s >= 0; s -= 8 ) out.push_back( static_cast<uint8_t>( ( v >> s ) & 0xFF ) );
}

/**
 * @brief A meta event of @p type with @p data at @p tick.
 * @param[in] tick  the position in ticks
 * @param[in] type  the meta event's type
 * @param[in] data  its data
 * @return the event
 */
Ev meta( int64_t tick, uint8_t type, const std::vector<uint8_t> &data )
{
    Ev e{ tick, 0, { 0xFF, type } };
    putVarLen( e.m_bytes, static_cast<uint32_t>( data.size() ) );
    e.m_bytes.insert( e.m_bytes.end(), data.begin(), data.end() );
    return e;
}

/**
 * @brief A text meta event of @p type (a marker, a track name) at @p tick.
 * @param[in] tick  the position in ticks
 * @param[in] type  the meta event's type
 * @param[in] s     its text
 * @return the event
 */
Ev metaText( int64_t tick, uint8_t type, const std::string &s )
{
    return meta( tick, type, std::vector<uint8_t>( s.begin(), s.end() ) );
}

/**
 * @brief A tempo meta event of @p bpm at @p tick.
 * @param[in] tick  the position in ticks
 * @param[in] bpm   the tempo
 * @return the event
 */
Ev tempoEvent( int64_t tick, double bpm )
{
    const uint32_t us = static_cast<uint32_t>( std::llround( 60.0e6 / bpm ) );
    return meta( tick, 0x51,
                 { static_cast<uint8_t>( us >> 16 ), static_cast<uint8_t>( us >> 8 ), static_cast<uint8_t>( us ) } );
}

/**
 * @brief Sharps (+) or flats (-) of the minor key on @p root (A minor = 0).
 * @param[in] root  the key's root, 0 .. 11
 * @return its sharps (positive) or flats (negative)
 */
int minorKeySharps( int root )
{
    // Relative major is three semitones up; the circle of fifths from C major.
    static const int MajorSharps[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };
    return MajorSharps[( ( root % 12 ) + 12 + 3 ) % 12];
}

/**
 * @brief Sorts @p evs and appends them to @p file as a track chunk (delta times, end of track).
 * @param[in,out] file  the file's bytes, appended to
 * @param[in,out] evs   the track's events, sorted here
 */
void appendTrack( std::vector<uint8_t> &file, std::vector<Ev> &evs )
{
    std::stable_sort( evs.begin(), evs.end(), []( const Ev &a, const Ev &b )
                      { return a.m_tick != b.m_tick ? a.m_tick < b.m_tick : a.m_order < b.m_order; } );
    std::vector<uint8_t> body;
    int64_t              last = 0;
    for ( const Ev &e : evs )
    {
        putVarLen( body, static_cast<uint32_t>( e.m_tick - last ) );
        body.insert( body.end(), e.m_bytes.begin(), e.m_bytes.end() );
        last = e.m_tick;
    }
    body.insert( body.end(), { 0x00, 0xFF, 0x2F, 0x00 } );
    file.insert( file.end(), { 'M', 'T', 'r', 'k' } );
    put32( file, static_cast<uint32_t>( body.size() ) );
    file.insert( file.end(), body.begin(), body.end() );
}

/**
 * @brief Where a knob's gestures go in the file: a part (or none: the controls track) and a CC.
 * @param[in]  p     the parameters
 * @param[in]  id    the knob
 * @param[out] part  receives the part, -1 for the controls track
 * @param[out] cc    receives the controller
 * @return false when the knob has no controller
 */
bool ccFor( const ParamStore &p, int id, int &part, int &cc )
{
    const std::string &key = p.key( id );
    const size_t       dot = key.find( '.' );
    const std::string  mod = key.substr( 0, dot ), name = key.substr( dot + 1 );
    auto voiceCc = [&]() { return name == "cutoff" ? 74 : name == "resonance" ? 71 : name == "decay" ? 75 : -1; };
    if ( mod.rfind( "voice", 0 ) == 0 )
    {
        part = static_cast<int>( PartE::ROW1 ) + std::atoi( mod.c_str() + 5 ) - 1;
        cc = voiceCc();
        return cc >= 0;
    }
    if ( mod == "lead" )
    {
        part = static_cast<int>( PartE::LEAD );
        cc = voiceCc();
        return cc >= 0;
    }
    if ( mod == "drone" )
    {
        part = static_cast<int>( PartE::DRONE );
        cc = voiceCc();
        return cc >= 0;
    }
    static const struct
    {
        const char *m_key;
        int         m_cc;
    } Controls[] = {
        { "echo.feedback", 12 }, { "echo.time", 13 },        { "reverb.decay", 14 }, { "tape.set", 15 },
        { "atmos.wind", 20 },    { "atmos.wind_tone", 21 },  { "atmos.sweeps", 22 }, { "atmos.bleeps", 23 },
        { "spring.return", 24 }, { "strings.ensemble", 25 }, { "master.level", 7 },
    };
    for ( const auto &c : Controls )
        if ( key == c.m_key )
        {
            part = -1;
            cc = c.m_cc;
            return true;
        }
    return false;
}

}   // namespace

int midiChannelOf( PartE part )
{
    if ( part == PartE::DRUMS ) return 9;
    const int c = static_cast<int>( part ) % 15;
    return c >= 9 ? c + 1 : c;   // every part but the drums keeps off channel 10
}

std::vector<uint8_t> encodeMidi( const Score &score, const char *title, const ParamStore *params )
{
    // The gestures as controller events, per part (index NumParts: the controls track).
    std::vector<std::vector<Ev>> cc( NumParts + 1 );
    if ( params != nullptr )
    {
        // Per knob, in time order, the value written last, so an unchanged value is not repeated.
        std::vector<int> last( static_cast<size_t>( params->count() ), -1 );
        for ( const Gesture &g : score.m_gestures )
        {
            int part = -1, number = -1;
            if ( g.m_param < 0 || g.m_param >= params->count() || !ccFor( *params, g.m_param, part, number ) ) continue;
            const size_t  track = part < 0 ? static_cast<size_t>( NumParts ) : static_cast<size_t>( part );
            const uint8_t ch = static_cast<uint8_t>( part < 0 ? 15 : midiChannelOf( static_cast<PartE>( part ) ) );
            const float   knob = params->toNormalised( g.m_param, params->get( g.m_param ) );
            const double  step = 0.125;   // a thirty-second note
            const double  end = g.m_beat + std::max( 0.0, g.m_length );
            for ( double b = g.m_beat;; b += step )
            {
                const double at = std::min( b, end );
                const float  v =
                    std::clamp( knob + gestureValue( g, at + ( g.m_length <= 0.0 ? 1.0 : 0.0 ) ), 0.0f, 1.0f );
                const int value = static_cast<int>( std::lround( v * 127.0f ) );
                if ( value != last[static_cast<size_t>( g.m_param )] )
                {
                    cc[track].push_back( Ev{ toTick( at ),
                                             1,
                                             { static_cast<uint8_t>( 0xB0 | ch ), static_cast<uint8_t>( number ),
                                               static_cast<uint8_t>( value ) } } );
                    last[static_cast<size_t>( g.m_param )] = value;
                }
                if ( at >= end ) break;
            }
        }
    }
    // Which parts have notes or controllers: one track each, in part order.
    bool used[NumParts] = {};
    for ( const NoteEvent &n : score.m_notes ) used[static_cast<int>( n.m_part )] = true;
    for ( int i = 0; i < NumParts; ++i ) used[i] = used[i] || !cc[static_cast<size_t>( i )].empty();
    const bool controls = !cc[NumParts].empty();
    int        tracks = 1 + ( controls ? 1 : 0 );
    for ( bool u : used ) tracks += u ? 1 : 0;

    std::vector<uint8_t> file = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1 };
    file.push_back( static_cast<uint8_t>( tracks >> 8 ) );
    file.push_back( static_cast<uint8_t>( tracks & 0xFF ) );
    file.push_back( static_cast<uint8_t>( MidiPpq >> 8 ) );
    file.push_back( static_cast<uint8_t>( MidiPpq & 0xFF ) );

    // Conductor track.
    std::vector<Ev> cond;
    cond.push_back( metaText( 0, 0x03, title != nullptr ? title : "Ephemeris" ) );
    cond.push_back( meta( 0, 0x58, { 4, 2, 24, 8 } ) );
    cond.push_back(
        meta( 0, 0x59, { static_cast<uint8_t>( static_cast<int8_t>( minorKeySharps( score.m_keyRoot ) ) ), 1 } ) );
    const std::vector<TempoPoint> &pts = score.m_tempo.points();
    const double                   end = std::max( score.m_lengthBeats, pts.back().m_beat );
    for ( size_t i = 0; i < pts.size(); ++i )
    {
        const TempoPoint &p = pts[i];
        if ( p.m_rampToNext && i + 1 < pts.size() )
        {
            // One event per beat of the ramp, each exact for the beat it starts.
            const double b1 = pts[i + 1].m_beat;
            for ( double b = p.m_beat; b < b1; b += 1.0 )
            {
                const double bn = std::min( b + 1.0, b1 );
                const double sec = score.m_tempo.secondsAt( bn ) - score.m_tempo.secondsAt( b );
                cond.push_back( tempoEvent( toTick( b ), 60.0 * ( bn - b ) / sec ) );
            }
        }
        else
        {
            cond.push_back( tempoEvent( toTick( p.m_beat ), p.m_bpm ) );
        }
    }
    for ( const Marker &m : score.m_markers ) cond.push_back( metaText( toTick( m.m_beat ), 0x06, m.m_text ) );
    Ev eot = meta( toTick( end ), 0x01, {} );   // an empty text event holds the conductor track to the end
    cond.push_back( eot );
    appendTrack( file, cond );

    // One track per part with notes.
    for ( int pi = 0; pi < NumParts; ++pi )
    {
        if ( !used[pi] ) continue;
        const PartE     part = static_cast<PartE>( pi );
        const uint8_t   ch = static_cast<uint8_t>( midiChannelOf( part ) );
        std::vector<Ev> evs;
        evs.push_back( metaText( 0, 0x03, PartNames[pi] ) );
        evs.insert( evs.end(), cc[static_cast<size_t>( pi )].begin(), cc[static_cast<size_t>( pi )].end() );
        for ( const NoteEvent &n : score.m_notes )
        {
            if ( n.m_part != part ) continue;
            const int     pitch = std::clamp( n.m_pitch, 0, 127 );
            const int     vel = std::clamp( static_cast<int>( std::lround( n.m_velocity * 127.0f ) ), 1, 127 );
            const int64_t on = toTick( n.m_beat );
            const int64_t off = std::max( on + 1, toTick( n.m_beat + n.m_length ) );
            evs.push_back( Ev{
                on,
                2,
                { static_cast<uint8_t>( 0x90 | ch ), static_cast<uint8_t>( pitch ), static_cast<uint8_t>( vel ) } } );
            evs.push_back( Ev{ off, 1, { static_cast<uint8_t>( 0x80 | ch ), static_cast<uint8_t>( pitch ), 0 } } );
        }
        appendTrack( file, evs );
    }
    if ( controls )
    {
        std::vector<Ev> evs = cc[NumParts];
        evs.push_back( metaText( 0, 0x03, "controls" ) );
        appendTrack( file, evs );
    }
    return file;
}

bool writeMidiFile( const Score &score, const char *path, const char *title, const ParamStore *params )
{
    const std::vector<uint8_t> bytes = encodeMidi( score, title, params );
    FILE                      *f = std::fopen( path, "wb" );
    if ( f == nullptr ) return false;
    const bool ok = std::fwrite( bytes.data(), 1, bytes.size(), f ) == bytes.size();
    return std::fclose( f ) == 0 && ok;
}

}   // namespace eph
