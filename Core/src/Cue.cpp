/**
 * @file Cue.cpp
 * @brief Score cues for a visualiser (Cue.h).
 * @author Rene Weller
 */
#include "eph/Cue.h"
#include "eph/Dsp.h"
#include "eph/Params.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#if defined( _WIN32 )
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

namespace eph
{

namespace
{

const char *const RootNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};   ///< the keys' names

/**
 * @brief The English name of a section, from the composer's German marker ("Aufbau 2" -> "BUILD").
 * @param[in] marker  the composer's marker
 * @return the section's English name
 */
const char *sectionOf( const std::string &marker )
{
    struct Name
    {
        const char *m_de;
        const char *m_en;
    };
    static const Name names[] = { { "Atmo", "ATMOSPHERE" }, { "Einsatz", "ENTRY" },   { "Aufbau", "BUILD" },
                                  { "Lead", "LEAD" },       { "Hoehepunkt", "PEAK" }, { "Abbau", "BREAKDOWN" },
                                  { "Bruecke", "BRIDGE" },  { "Ausklang", "CODA" } };
    // A concert's pieces carry "Stueck N: " before the section.
    const size_t      colon = marker.find( ": " );
    const std::string m = colon == std::string::npos ? marker : marker.substr( colon + 2 );
    for ( const Name &n : names )
        if ( m.rfind( n.m_de, 0 ) == 0 ) return n.m_en;
    return nullptr;
}

/**
 * @brief Copies @p src into @p dst, cut at 15 characters and terminated.
 * @param[out] dst  receives the text
 * @param[in]  src  the text
 */
void copyText( char ( &dst )[16], const char *src )
{
    std::strncpy( dst, src, sizeof( dst ) - 1 );
    dst[sizeof( dst ) - 1] = 0;
}

/**
 * @brief Appends @p v big-endian (OSC).
 * @param[in,out] out  the bytes, appended to
 * @param[in]     v    the value
 */
void put32( std::vector<uint8_t> &out, uint32_t v )
{
    for ( int s = 24; s >= 0; s -= 8 ) out.push_back( static_cast<uint8_t>( v >> s ) );
}

/**
 * @brief Appends @p s as an OSC string: terminated and padded to four bytes.
 * @param[in,out] out  the bytes, appended to
 * @param[in]     s    the string
 */
void putString( std::vector<uint8_t> &out, const char *s )
{
    const size_t n = std::strlen( s );
    out.insert( out.end(), s, s + n );
    for ( size_t pad = 4 - n % 4; pad > 0; --pad ) out.push_back( 0 );   // at least one zero, then to four bytes
}

}   // namespace

std::vector<CueMark> cueMarksOf( const Score &score )
{
    std::vector<CueMark> marks;
    // Sections: every marker the composer wrote; the phase counts the entries.
    int phase = 0;
    for ( const Marker &m : score.m_markers )
    {
        const char *name = sectionOf( m.m_text );
        if ( name == nullptr ) continue;
        if ( std::strcmp( name, "ENTRY" ) == 0 ) ++phase;
        CueMark c;
        c.m_beat = m.m_beat;
        c.m_kind = CueKindE::PHASE;
        c.m_a = std::max( 1, phase );
        copyText( c.m_text, name );
        marks.push_back( c );
    }
    // Keys: where the root the rows play on changes.
    int last = -1;
    for ( const auto &shift : score.m_rootShifts )
    {
        const int root = pitchClass( score.m_keyRoot + shift.second );
        if ( root == last ) continue;
        last = root;
        CueMark c;
        c.m_beat = shift.first;
        c.m_kind = CueKindE::KEY;
        copyText( c.m_text, RootNames[root] );
        marks.push_back( c );
    }
    // Conjunctions: every step (a 48th of a beat is fine enough for every division) where two or more running
    // rows that play notes stand on their first step together.
    struct Row
    {
        bool   m_known = false, m_transposer = false, m_running = false;
        double m_period = 4.0, m_start = 0.0;
    };
    Row                   rows[Rows];
    size_t                shape = 0, ev = 0;
    std::vector<RowShape> shapes = score.m_rowShapes;
    std::stable_sort( shapes.begin(), shapes.end(),
                      []( const RowShape &a, const RowShape &b ) { return a.m_from < b.m_from; } );
    std::vector<RackEvent> rack;
    for ( const RackEvent &e : score.m_rack )
        if ( e.m_op == RackOpE::START || e.m_op == RackOpE::STOP ) rack.push_back( e );
    std::stable_sort( rack.begin(), rack.end(),
                      []( const RackEvent &a, const RackEvent &b ) { return a.m_beat < b.m_beat; } );
    const int64_t steps = static_cast<int64_t>( std::ceil( score.m_lengthBeats * 48.0 ) );
    for ( int64_t t = 0; t < steps; t += 3 )
    {   // sixteenth triplets and all coarser divisions land on multiples of 1/16
        const double beat = static_cast<double>( t ) / 48.0;
        while ( shape < shapes.size() && shapes[shape].m_from <= beat + 1e-9 )
        {
            const RowShape &r = shapes[shape++];
            if ( r.m_row < 0 || r.m_row >= Rows ) continue;
            rows[r.m_row].m_known = true;
            rows[r.m_row].m_transposer = r.m_transposer;
            rows[r.m_row].m_period = r.m_length * r.m_divBeats;
        }
        while ( ev < rack.size() && rack[ev].m_beat <= beat + 1e-9 )
        {
            const RackEvent &e = rack[ev++];
            const int        lo = e.m_row < 0 ? 0 : e.m_row, hi = e.m_row < 0 ? Rows - 1 : e.m_row;
            for ( int i = lo; i <= hi && i < Rows; ++i )
            {
                rows[i].m_running = e.m_op == RackOpE::START;
                if ( rows[i].m_running ) rows[i].m_start = e.m_beat;
            }
        }
        // Rows that start on this step stand on their first step by starting: they do not count as meeting.
        int running = 0, meeting = 0;
        for ( const Row &r : rows )
        {
            if ( !r.m_known || r.m_transposer || !r.m_running || r.m_period <= 0.0 ) continue;
            ++running;
            if ( beat - r.m_start < 1e-9 ) continue;
            const double q = ( beat - r.m_start ) / r.m_period;
            if ( std::fabs( q - std::round( q ) ) < 1e-6 ) ++meeting;
        }
        if ( meeting >= 2 )
        {
            CueMark c;
            c.m_beat = beat;
            c.m_kind = CueKindE::CONJUNCTION;
            c.m_a = meeting;
            c.m_b = static_cast<float>( meeting ) / static_cast<float>( running );
            marks.push_back( c );
        }
    }
    std::stable_sort( marks.begin(), marks.end(),
                      []( const CueMark &x, const CueMark &y ) { return x.m_beat < y.m_beat; } );
    return marks;
}

int CueTap::scan( const std::vector<CueMark> &marks, double from, double to, float bpm, int64_t nowNanos,
                  int64_t leadNanos, int64_t blockNanos, CueRing &ring )
{
    if ( !( to > from ) ) return 0;
    int  lost = 0;
    auto at = [&]( double beat )
    {
        return nowNanos + leadNanos +
               static_cast<int64_t>( static_cast<double>( blockNanos ) * ( beat - from ) / ( to - from ) );
    };
    // A jump backwards (a seek): start again from the first mark at or after the new position.
    if ( m_cursor > 0 && m_cursor <= marks.size() && marks[m_cursor - 1].m_beat >= from ) m_cursor = 0;
    while ( m_cursor < marks.size() && marks[m_cursor].m_beat < from ) ++m_cursor;
    // The beats: every whole beat in the range.
    for ( double b = std::ceil( from ); b < to; b += 1.0 )
    {
        Cue c;
        c.m_dueNanos = at( b );
        c.m_kind = CueKindE::BEAT;
        c.m_a = static_cast<int32_t>( b );
        c.m_b = bpm;
        if ( !ring.push( c ) ) ++lost;
    }
    for ( ; m_cursor < marks.size() && marks[m_cursor].m_beat < to; ++m_cursor )
    {
        const CueMark &m = marks[m_cursor];
        Cue            c;
        c.m_dueNanos = at( m.m_beat );
        c.m_kind = m.m_kind;
        c.m_a = m.m_a;
        c.m_b = m.m_b;
        std::memcpy( c.m_text, m.m_text, sizeof( c.m_text ) );
        if ( !ring.push( c ) ) ++lost;
    }
    return lost;
}

std::vector<uint8_t> oscMessage( const char *address, const char *tags, const int32_t *ints, const float *floats,
                                 const char *const *strings )
{
    std::vector<uint8_t> out;
    putString( out, address );
    const std::string t = std::string( "," ) + tags;
    putString( out, t.c_str() );
    int ni = 0, nf = 0, ns = 0;
    for ( const char *p = tags; *p != 0; ++p )
    {
        if ( *p == 'i' )
            put32( out, static_cast<uint32_t>( ints[ni++] ) );
        else if ( *p == 'f' )
        {
            uint32_t u;
            std::memcpy( &u, &floats[nf++], 4 );
            put32( out, u );
        }
        else if ( *p == 's' )
            putString( out, strings[ns++] );
    }
    return out;
}

std::vector<uint8_t> oscOf( const Cue &c )
{
    const char *text = c.m_text;
    switch ( c.m_kind )
    {
    case CueKindE::BEAT: return oscMessage( "/eph/beat", "if", &c.m_a, &c.m_b, nullptr );
    case CueKindE::PHASE: return oscMessage( "/eph/phase", "si", &c.m_a, nullptr, &text );
    case CueKindE::KEY: return oscMessage( "/eph/key", "s", nullptr, nullptr, &text );
    case CueKindE::CONJUNCTION: return oscMessage( "/eph/conjunction", "if", &c.m_a, &c.m_b, nullptr );
    }
    return {};
}

int64_t CueSender::nowNanos()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>( std::chrono::steady_clock::now().time_since_epoch() )
        .count();
}

bool CueSender::start( const std::string &host, int port )
{
    stop();
#if defined( _WIN32 )
    WSADATA wsa;
    if ( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 ) return false;
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons( static_cast<uint16_t>( port ) );
    if ( inet_pton( AF_INET, host.c_str(), &addr.sin_addr ) != 1 )
    {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if ( getaddrinfo( host.c_str(), nullptr, &hints, &res ) != 0 || res == nullptr ) return false;
        addr.sin_addr = reinterpret_cast<sockaddr_in *>( res->ai_addr )->sin_addr;
        freeaddrinfo( res );
    }
    static_assert( sizeof( m_address ) >= sizeof( sockaddr_in ), "room for the address" );
    std::memcpy( m_address, &addr, sizeof( addr ) );
    const auto s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
#if defined( _WIN32 )
    if ( s == INVALID_SOCKET ) return false;
#else
    if ( s < 0 ) return false;
#endif
    m_socket = static_cast<intptr_t>( s );
    m_stop = false;
    m_running = true;
    m_thread = std::thread( [this] { loop(); } );
    return true;
}

void CueSender::stop()
{
    if ( m_thread.joinable() )
    {
        m_stop = true;
        m_thread.join();
    }
    m_running = false;
    if ( m_socket >= 0 )
    {
#if defined( _WIN32 )
        closesocket( static_cast<SOCKET>( m_socket ) );
        WSACleanup();
#else
        close( static_cast<int>( m_socket ) );
#endif
        m_socket = -1;
    }
}

void CueSender::loop()
{
    Cue c;
    while ( !m_stop.load( std::memory_order_acquire ) )
    {
        if ( !m_ring.pop( c ) )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            continue;
        }
        // Hold it back until the listener hears it (a cue that is late already goes at once).
        while ( !m_stop.load( std::memory_order_acquire ) )
        {
            const int64_t wait = c.m_dueNanos - nowNanos();
            if ( wait <= 0 ) break;
            std::this_thread::sleep_for( std::chrono::nanoseconds( std::min<int64_t>( wait, 2000000 ) ) );
        }
        const std::vector<uint8_t> msg = oscOf( c );
        const auto sent = sendto( static_cast<decltype( socket( 0, 0, 0 ) )>( m_socket ),
                                  reinterpret_cast<const char *>( msg.data() ), static_cast<int>( msg.size() ), 0,
                                  reinterpret_cast<const sockaddr *>( m_address ), sizeof( sockaddr_in ) );
        if ( sent < 0 ) m_dropped.fetch_add( 1, std::memory_order_relaxed );
    }
}

}   // namespace eph
