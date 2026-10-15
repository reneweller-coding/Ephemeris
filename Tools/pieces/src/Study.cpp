/**
 * @file Study.cpp
 * @brief The study of Phase 1.
 * @author Rene Weller
 */
#include "eph/Study.h"
#include "eph/Rack.h"
#include <algorithm>
#include <cmath>

namespace eph
{

Score buildStudy( const ParamStore &p, uint64_t seed, double minutes )
{
    const double bpm = p.get( p.id( ModuleE::COMPOSE, 0, compose::BPM ) );
    Score        s;
    s.clear( bpm );
    s.m_seed = seed;
    s.m_keyRoot = p.getInt( p.id( ModuleE::COMPOSE, 0, compose::KEY ) );
    const int bars = std::max( 32, static_cast<int>( std::lround( minutes * bpm / BeatsPerBar ) ) );
    s.m_lengthBeats = bars * BeatsPerBar;
    // A fraction of the piece, on a bar line, in beats.
    auto at = [&]( double f ) { return std::floor( f * bars + 0.5 ) * BeatsPerBar; };
    auto span = [&]( double f0, double f1 ) { return std::max( 4.0, at( f1 ) - at( f0 ) ); };

    const int cutoff1 = p.id( ModuleE::VOICE, 0, voice::CUTOFF );
    const int decay1 = p.id( ModuleE::VOICE, 0, voice::DECAY );
    const int reso1 = p.id( ModuleE::VOICE, 0, voice::RESONANCE );
    const int cutoff2 = p.id( ModuleE::VOICE, 1, voice::CUTOFF );
    const int feedback = p.id( ModuleE::ECHO, 0, echo::FEEDBACK );
    using GT = GestureShapeE;
    auto hand = [&]( int param, double f0, double f1, float from, float to, GT shape, int h )
    { s.m_gestures.push_back( { param, at( f0 ), span( f0, f1 ), from, to, shape, static_cast<uint8_t>( h ) } ); };

    // Hand 0 on the bass row's filter, hand 1 on whatever joins it.
    hand( cutoff1, 0.00, 0.03, -0.30f, -0.30f, GT::STEP, 0 );          // closed at the start
    hand( cutoff1, 0.03, 0.25, -0.30f, 0.10f, GT::MINIMUM_JERK, 0 );   // the long opening
    s.m_rack.push_back( { at( 0.11 ), 1, RackOpE::START, 0 } );
    hand( cutoff2, 0.00, 0.11, -0.40f, -0.40f, GT::STEP, 1 );
    hand( cutoff2, 0.11, 0.27, -0.40f, 0.00f, GT::MINIMUM_JERK, 1 );   // the counter row surfaces
    hand( decay1, 0.30, 0.38, 0.00f, 0.25f, GT::MINIMUM_JERK, 1 );     // staccato into legato ...
    hand( decay1, 0.41, 0.46, 0.25f, 0.00f, GT::MINIMUM_JERK, 1 );     // ... and back
    hand( feedback, 0.43, 0.44, 0.00f, 0.35f, GT::EASE_OUT, 0 );       // an echo thrown ...
    hand( feedback, 0.45, 0.48, 0.35f, 0.00f, GT::MINIMUM_JERK, 0 );   // ... and caught
    hand( cutoff1, 0.49, 0.59, 0.10f, -0.08f, GT::MINIMUM_JERK, 0 );   // the dip before the peak
    hand( cutoff1, 0.60, 0.70, -0.08f, 0.22f, GT::MINIMUM_JERK, 0 );   // the peak
    hand( reso1, 0.60, 0.70, 0.00f, 0.20f, GT::MINIMUM_JERK, 1 );
    hand( reso1, 0.76, 0.84, 0.20f, 0.00f, GT::MINIMUM_JERK, 1 );
    hand( cutoff1, 0.84, 0.97, 0.22f, -0.35f, GT::MINIMUM_JERK, 0 );   // the close
    hand( cutoff2, 0.86, 0.95, 0.00f, -0.45f, GT::MINIMUM_JERK, 1 );
    s.m_rack.push_back( { at( 0.95 ), 1, RackOpE::STOP, 0 } );

    // The transposer: a pedal first, then i - i - bVI - bVII every eight bars, home for the close.
    const int cycle[4] = { 0, 0, -4, -2 };
    int       k = 0;
    for ( double b = at( 0.22 ); b < at( 0.84 ); b += 8.0 * BeatsPerBar, ++k )
        s.m_rack.push_back( { b, -1, RackOpE::TRANSPOSE, cycle[k % 4] } );
    s.m_rack.push_back( { at( 0.84 ), -1, RackOpE::TRANSPOSE, 0 } );

    s.m_markers = { { 0.0, "Einsatz" },
                    { at( 0.11 ), "Aufbau" },
                    { at( 0.22 ), "Transposition" },
                    { at( 0.60 ), "Hoehepunkt" },
                    { at( 0.84 ), "Ausklang" } };

    Rack rack;
    rack.setup( p, seed );
    rack.generate( 0, RowRoleE::BASS );
    rack.generate( 1, RowRoleE::COUNTER );
    s.sort();
    rack.run( s, s.m_lengthBeats );
    s.sort();
    return s;
}

}   // namespace eph
