/**
 * @file Jam.cpp
 * @brief The family jam's bus (Jam.h): the OSC it speaks, its thread, and the state a follower reads.
 * @author Rene Weller
 */
#include "Jam.h"
#include <cmath>
#include <cstring>

namespace frame
{

namespace
{

/** @brief The section kinds' names, in the order of JamSection. */
const char *const SectionNames[] = { "Intro", "Groove", "Build", "Drop", "Break", "Outro" };

/** @brief An instrument's name of a section, and what it is in the jam. */
struct SectionName
{
    const char *m_name;      ///< the instrument's name (a prefix where @c prefix is set)
    JamSectionE m_section;   ///< the kind it is
    float       m_energy;    ///< its energy
    bool        m_drop;      ///< whether it lands as a drop
    bool        m_prefix;    ///< @c name is a prefix ("Block 3")
};

/** @brief Every name the family's instruments give their sections (their Cue.h), and the jam's own. */
const SectionName Names[] = {
    { "Intro", JamSectionE::INTRO, 0.30f, false, false },     { "Groove", JamSectionE::GROOVE, 0.55f, false, false },
    { "Build", JamSectionE::BUILD, 0.60f, false, false },     { "Drop", JamSectionE::DROP, 0.90f, true, false },
    { "Break", JamSectionE::BREAK, 0.35f, false, false },     { "Outro", JamSectionE::OUTRO, 0.30f, false, false },
    { "Block", JamSectionE::GROOVE, 0.60f, false, true },     { "Reduction", JamSectionE::BREAK, 0.35f, false, false },
    { "Return", JamSectionE::DROP, 0.90f, true, false },      { "Breakdown", JamSectionE::BREAK, 0.30f, false, false },
    { "Main drop", JamSectionE::DROP, 1.00f, true, false },   { "ATMOSPHERE", JamSectionE::INTRO, 0.20f, false, false },
    { "ENTRY", JamSectionE::GROOVE, 0.45f, false, false },    { "BUILD", JamSectionE::BUILD, 0.60f, false, false },
    { "LEAD", JamSectionE::DROP, 0.75f, false, false },       { "PEAK", JamSectionE::DROP, 0.95f, true, false },
    { "BREAKDOWN", JamSectionE::BREAK, 0.35f, false, false }, { "BRIDGE", JamSectionE::GROOVE, 0.50f, false, false },
    { "CODA", JamSectionE::OUTRO, 0.30f, false, false },      { "Pdb", JamSectionE::BUILD, 0.70f, false, false },
    { "PDB", JamSectionE::BUILD, 0.70f, false, false },       { "Cut", JamSectionE::BREAK, 0.25f, false, false },
};

/**
 * @brief Appends an OSC string to @p b: the text, its terminator, nulls to the next multiple of four.
 * @param[in,out] b  the message, appended to
 * @param[in]     s  the text
 */
void putString( juce::MemoryOutputStream &b, const char *s )
{
    const size_t n = std::strlen( s ) + 1;
    b.write( s, n );
    for ( size_t pad = ( 4 - ( n & 3 ) ) & 3; pad > 0; --pad ) b.writeByte( 0 );
}

/**
 * @brief Appends a big-endian int32.
 * @param[in,out] b  the message, appended to
 * @param[in]     v  the value
 */
void putInt( juce::MemoryOutputStream &b, int32_t v )
{
    b.writeIntBigEndian( v );
}

/**
 * @brief Appends a big-endian float32.
 * @param[in,out] b  the message, appended to
 * @param[in]     v  the value
 */
void putFloat( juce::MemoryOutputStream &b, float v )
{
    int32_t i;
    std::memcpy( &i, &v, 4 );
    b.writeIntBigEndian( i );
}

/**
 * @brief Reads an OSC string at @p pos into @p out (cap bytes); false when it is unterminated or runs past @p size.
 * @param[in]     d     the datagram
 * @param[in]     size  its length in bytes
 * @param[in,out] pos   the read position, moved past the string
 * @param[out]    out   receives the text
 * @param[in]     cap   the room in out, in bytes
 * @return false when the string is unterminated or runs past the end
 */
bool getString( const char *d, int size, int &pos, char *out, int cap )
{
    int end = pos;
    while ( end < size && d[end] != 0 ) ++end;
    if ( end >= size ) return false;
    const int len = std::min( end - pos, cap - 1 );
    std::memcpy( out, d + pos, static_cast<size_t>( len ) );
    out[len] = 0;
    pos = ( end + 4 ) & ~3;
    return pos <= size;
}

/**
 * @brief Reads a big-endian int32 at @p pos; false past @p size.
 * @param[in]     d     the datagram
 * @param[in]     size  its length in bytes
 * @param[in,out] pos   the read position, moved past the value
 * @param[out]    v     receives the value
 * @return false past the end
 */
bool getInt( const char *d, int size, int &pos, int32_t &v )
{
    if ( pos + 4 > size ) return false;
    const auto *u = reinterpret_cast<const unsigned char *>( d + pos );
    v = static_cast<int32_t>( ( static_cast<uint32_t>( u[0] ) << 24 ) | ( static_cast<uint32_t>( u[1] ) << 16 ) |
                              ( static_cast<uint32_t>( u[2] ) << 8 ) | static_cast<uint32_t>( u[3] ) );
    pos += 4;
    return true;
}

/**
 * @brief Reads a big-endian float32 at @p pos; false past @p size.
 * @param[in]     d     the datagram
 * @param[in]     size  its length in bytes
 * @param[in,out] pos   the read position, moved past the value
 * @param[out]    v     receives the value
 * @return false past the end
 */
bool getFloat( const char *d, int size, int &pos, float &v )
{
    int32_t i;
    if ( !getInt( d, size, pos, i ) ) return false;
    std::memcpy( &v, &i, 4 );
    return true;
}

constexpr int64_t LeaderTimeoutMs =
    4000;   ///< a leader not heard for this long is gone: the follower plays its own way

}   // namespace

const char *jamSectionName( JamSectionE s )
{
    return SectionNames[static_cast<int>( s ) % 6];
}

JamSectionE jamSectionOf( const char *name, float &energy, bool &drop )
{
    for ( const SectionName &n : Names )
        if ( n.m_prefix ? std::strncmp( name, n.m_name, std::strlen( n.m_name ) ) == 0
                        : std::strcmp( name, n.m_name ) == 0 )
        {
            energy = n.m_energy;
            drop = n.m_drop;
            return n.m_section;
        }
    energy = 0.5f;
    drop = false;
    return JamSectionE::GROOVE;
}

JamBus::JamBus( const juce::String &app )
    : juce::Thread( app + " jam" ), m_app( app ),
      m_id( juce::String::toHexString( juce::Random::getSystemRandom().nextInt() ) )
{
}

JamBus::~JamBus()
{
    stopThread( 2000 );
}

void JamBus::setRole( Settings::JamRoleE r )
{
    const int want = static_cast<int>( r );
    if ( want == m_role.load( std::memory_order_relaxed ) ) return;
    stopThread( 2000 );
    m_role.store( want, std::memory_order_relaxed );
    m_leaderRoot.store( -1, std::memory_order_relaxed );
    m_leaderSeenMs.store( 0, std::memory_order_relaxed );
    {
        const juce::ScopedLock g( m_lock );
        m_leaderName = m_leaderId = m_leaderMode = {};
        m_peers.clear();
    }
    if ( r != Settings::JamRoleE::OFF ) startThread();
}

bool JamBus::open()
{
    m_socket = std::make_unique<juce::DatagramSocket>( false );
    m_socket->setEnablePortReuse( true );   // every instrument on this machine listens on the same port
    if ( !m_socket->bindToPort( Port ) || !m_socket->joinMulticast( Group ) )
    {
        m_socket.reset();
        return false;
    }
    m_socket->setMulticastLoopbackEnabled( true );   // and the others on this machine hear it too
    return true;
}

void JamBus::send( const juce::MemoryBlock &bytes )
{
    if ( m_socket != nullptr ) m_socket->write( Group, Port, bytes.getData(), static_cast<int>( bytes.getSize() ) );
}

void JamBus::postSection( JamSectionE s, float energy, bool drop, int64_t bar )
{
    Out o;
    o.m_kind = 0;
    o.m_section = static_cast<uint8_t>( s );
    o.m_energy = energy;
    o.m_drop = drop;
    o.m_bar = bar;
    m_out.push( o );
}

void JamBus::postKey( int root, const char *mode )
{
    Out o;
    o.m_kind = 1;
    o.m_root = static_cast<int8_t>( ( ( root % 12 ) + 12 ) % 12 );
    std::strncpy( o.m_mode, mode != nullptr ? mode : "", sizeof( o.m_mode ) - 1 );
    m_out.push( o );
}

void JamBus::run()
{
    while ( !threadShouldExit() && !open() ) wait( 1000 );   // the port may be in use for a moment: try again
    int64_t lastHello = 0, lastRepeat = 0;
    char    buf[1024];
    while ( !threadShouldExit() )
    {
        const int64_t now = juce::Time::currentTimeMillis();
        const bool    leading = role() == Settings::JamRoleE::LEAD;
        // What the audio thread posted: a leader sends it, anyone else lets it go.
        Out o;
        while ( m_out.pop( o ) )
        {
            if ( !leading ) continue;
            juce::MemoryOutputStream b;
            if ( o.m_kind == 1 )
            {
                putString( b, "/family/key" );
                putString( b, ",sis" );
                putString( b, m_id.toRawUTF8() );
                putInt( b, o.m_root );
                putString( b, o.m_mode );
                m_lastKeyRoot = o.m_root;
                std::memcpy( m_lastKeyMode, o.m_mode, sizeof( m_lastKeyMode ) );
            }
            else
            {
                putString( b, "/family/section" );
                putString( b, ",ssfii" );
                putString( b, m_id.toRawUTF8() );
                putString( b, SectionNames[o.m_section % 6] );
                putFloat( b, o.m_energy );
                putInt( b, o.m_drop ? 1 : 0 );
                putInt( b, static_cast<int32_t>( o.m_bar ) );
                m_lastSection = o;
                m_haveSection = true;
            }
            send( b.getMemoryBlock() );
        }
        if ( now - lastHello >= 1000 )
        {   // who is here, and as what
            lastHello = now;
            juce::MemoryOutputStream b;
            putString( b, "/family/hello" );
            putString( b, ",ssi" );
            putString( b, m_app.toRawUTF8() );
            putString( b, m_id.toRawUTF8() );
            putInt( b, m_role.load( std::memory_order_relaxed ) );
            send( b.getMemoryBlock() );
        }
        if ( leading && now - lastRepeat >= 2000 )
        {   // the key and the section once more, for a follower who joins late
            lastRepeat = now;
            if ( m_lastKeyRoot >= 0 )
            {
                juce::MemoryOutputStream b;
                putString( b, "/family/key" );
                putString( b, ",sis" );
                putString( b, m_id.toRawUTF8() );
                putInt( b, m_lastKeyRoot );
                putString( b, m_lastKeyMode );
                send( b.getMemoryBlock() );
            }
            if ( m_haveSection )
            {
                juce::MemoryOutputStream b;
                putString( b, "/family/section" );
                putString( b, ",ssfii" );
                putString( b, m_id.toRawUTF8() );
                putString( b, SectionNames[m_lastSection.m_section % 6] );
                putFloat( b, m_lastSection.m_energy );
                putInt( b, 0 );
                putInt( b, -1 );   // the state, not a new drop: at once
                send( b.getMemoryBlock() );
            }
        }
        if ( m_socket != nullptr && m_socket->waitUntilReady( true, 20 ) == 1 )
        {
            juce::String from;
            int          port = 0;
            const int    n = m_socket->read( buf, sizeof( buf ), false, from, port );
            if ( n > 0 ) receive( buf, n );
        }
        else if ( m_socket == nullptr )
        {
            wait( 20 );
        }
    }
    m_socket.reset();
}

void JamBus::receive( const char *d, int size )
{
    int  pos = 0;
    char address[32], tags[16], id[24];
    if ( size <= 0 || ( size & 3 ) || !getString( d, size, pos, address, sizeof( address ) ) ||
         !getString( d, size, pos, tags, sizeof( tags ) ) )
        return;
    const int64_t now = juce::Time::currentTimeMillis();
    if ( std::strcmp( address, "/family/hello" ) == 0 && std::strcmp( tags, ",ssi" ) == 0 )
    {
        char    app[32];
        int32_t role = 0;
        if ( !getString( d, size, pos, app, sizeof( app ) ) || !getString( d, size, pos, id, sizeof( id ) ) ||
             !getInt( d, size, pos, role ) )
            return;
        if ( m_id == id ) return;   // our own, back over the loopback
        const juce::ScopedLock g( m_lock );
        m_peers[juce::String( id )] = { role, now };
        if ( role == static_cast<int>( Settings::JamRoleE::LEAD ) &&
             ( m_leaderId.isEmpty() || m_leaderId == id || leaderGone( now ) ) )
        {
            m_leaderId = id;
            m_leaderName = app;
            m_leaderSeenMs.store( now, std::memory_order_relaxed );
        }
        return;
    }
    if ( role() != Settings::JamRoleE::FOLLOW ) return;   // keys and sections are for followers
    if ( std::strcmp( address, "/family/key" ) == 0 && std::strcmp( tags, ",sis" ) == 0 )
    {
        int32_t root = -1;
        char    mode[32];
        if ( !getString( d, size, pos, id, sizeof( id ) ) || !getInt( d, size, pos, root ) ||
             !getString( d, size, pos, mode, sizeof( mode ) ) )
            return;
        if ( m_id == id ) return;
        {
            const juce::ScopedLock g( m_lock );
            if ( m_leaderId.isNotEmpty() && m_leaderId != id && !leaderGone( now ) ) return;   // one leader at a time
            m_leaderId = id;
            m_leaderMode = mode;
        }
        m_leaderRoot.store( ( ( root % 12 ) + 12 ) % 12, std::memory_order_relaxed );
        m_leaderSeenMs.store( now, std::memory_order_relaxed );
        return;
    }
    if ( std::strcmp( address, "/family/section" ) == 0 && std::strcmp( tags, ",ssfii" ) == 0 )
    {
        char    kind[16];
        float   energy = 0.5f;
        int32_t drop = 0, bar = -1;
        if ( !getString( d, size, pos, id, sizeof( id ) ) || !getString( d, size, pos, kind, sizeof( kind ) ) ||
             !getFloat( d, size, pos, energy ) || !getInt( d, size, pos, drop ) || !getInt( d, size, pos, bar ) )
            return;
        if ( m_id == id ) return;
        {
            const juce::ScopedLock g( m_lock );
            if ( m_leaderId.isNotEmpty() && m_leaderId != id && !leaderGone( now ) ) return;
            m_leaderId = id;
        }
        float e = 0.5f;
        bool  dr = false;
        In    in;
        in.m_section = static_cast<uint8_t>( jamSectionOf( kind, e, dr ) );
        in.m_energy = juce::jlimit( 0.0f, 1.0f, energy );
        in.m_drop = drop != 0;
        in.m_bar = bar;
        m_in.push( in );
        m_leaderSeenMs.store( now, std::memory_order_relaxed );
    }
}

bool JamBus::leaderGone( int64_t now ) const
{
    return now - m_leaderSeenMs.load( std::memory_order_relaxed ) >= LeaderTimeoutMs;
}

JamState JamBus::stateAt( double sharedBeat )
{
    if ( role() != Settings::JamRoleE::FOLLOW ) return {};
    In in;
    while ( m_in.pop( in ) )
    {
        if ( m_pendingCount < static_cast<int>( m_pending.size() ) )
            m_pending[static_cast<size_t>( m_pendingCount++ )] = in;
        else
            m_pending[m_pending.size() - 1] = in;   // a flood: the newest wins
    }
    const int64_t bar = sharedBeat >= 0.0 ? static_cast<int64_t>( std::floor( sharedBeat / 4.0 + 1.0e-6 ) ) : -1;
    // Whatever is due -- its bar has come, or it has none, or there is no shared timeline -- in the order it came.
    int keep = 0;
    for ( int i = 0; i < m_pendingCount; ++i )
    {
        const In &p = m_pending[static_cast<size_t>( i )];
        if ( p.m_bar < 0 || bar < 0 || p.m_bar <= bar )
        {
            const auto s = static_cast<JamSectionE>( p.m_section );
            m_state.m_energy = p.m_energy;
            if ( s == JamSectionE::BREAK )
                m_state.m_rhythmOut = true;
            else if ( p.m_drop || s == JamSectionE::DROP || s == JamSectionE::GROOVE || s == JamSectionE::BUILD )
                m_state.m_rhythmOut = false;
        }
        else
        {
            m_pending[static_cast<size_t>( keep++ )] = p;
        }
    }
    m_pendingCount = keep;
    const bool alive =
        juce::Time::currentTimeMillis() - m_leaderSeenMs.load( std::memory_order_relaxed ) < LeaderTimeoutMs;
    if ( !alive )
        m_state = {};   // the leader is gone: the follower plays its own way again
    else
        m_state.m_root = m_leaderRoot.load( std::memory_order_relaxed );
    return m_state;
}

juce::String JamBus::leaderMode() const
{
    const juce::ScopedLock g( m_lock );
    return m_leaderMode;
}

juce::String JamBus::status() const
{
    const Settings::JamRoleE r = role();
    if ( r == Settings::JamRoleE::OFF ) return "off";
    const juce::ScopedLock g( m_lock );
    const int64_t          now = juce::Time::currentTimeMillis();
    int                    followers = 0, others = 0;
    for ( const auto &p : m_peers )
        if ( now - p.second.second < LeaderTimeoutMs )
        {
            ++others;
            if ( p.second.first == static_cast<int>( Settings::JamRoleE::FOLLOW ) ) ++followers;
        }
    if ( r == Settings::JamRoleE::LEAD )
        return others == 0 ? juce::String( "leading; nobody else heard" )
                           : "leading; " + juce::String( followers ) + " following";
    const bool alive = now - m_leaderSeenMs.load( std::memory_order_relaxed ) < LeaderTimeoutMs;
    if ( !alive )
        return others == 0 ? juce::String( "following; nobody heard" ) : juce::String( "following; no leader heard" );
    const int                root = m_leaderRoot.load( std::memory_order_relaxed );
    static const char *const Roots[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return "following " + m_leaderName +
           ( root >= 0 ? " (" + juce::String( Roots[root] ) + " " + m_leaderMode + ")" : juce::String() );
}

}   // namespace frame
