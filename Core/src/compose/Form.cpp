/**
 * @file Form.cpp
 * @brief The grammar of a piece.
 * @author Rene Weller
 */
#include "eph/compose/Form.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace eph
{

const char *sectionName( SectionTypeE t )
{
    switch ( t )
    {
    case SectionTypeE::ATMO: return "Atmo";
    case SectionTypeE::ENTRY: return "Einsatz";
    case SectionTypeE::BUILD: return "Aufbau";
    case SectionTypeE::LEAD: return "Lead";
    case SectionTypeE::PEAK: return "Hoehepunkt";
    case SectionTypeE::BREAKDOWN: return "Abbau";
    case SectionTypeE::BRIDGE: return "Bruecke";
    case SectionTypeE::CODA: return "Ausklang";
    }
    return "?";
}

float PieceForm::energy( double beat ) const
{
    for ( const Section &s : m_sections )
    {
        if ( beat < s.m_beat + s.m_length || &s == &m_sections.back() )
        {
            const double t = std::clamp( ( beat - s.m_beat ) / std::max( 1.0, s.m_length ), 0.0, 1.0 );
            return static_cast<float>( s.m_e0 + t * ( s.m_e1 - s.m_e0 ) );
        }
    }
    return 0.0f;
}

const Section *PieceForm::find( SectionTypeE type, int phase ) const
{
    for ( const Section &s : m_sections )
        if ( s.m_type == type && s.m_phase == phase ) return &s;
    return nullptr;
}

namespace
{

/**
 * @brief The fewest bars a section of type @p t has.
 * @param[in] t  the section type
 * @return its fewest bars
 */
int minBars( SectionTypeE t )
{
    switch ( t )
    {
    case SectionTypeE::LEAD:
    case SectionTypeE::PEAK: return 8;
    default: return 4;
    }
}

}   // namespace

PieceForm drawForm( const StyleProfile &prof, double minutes, double bpm, Rng &rng )
{
    PieceForm f;
    const int phases = prof.m_phasesLow + rng.below( prof.m_phasesHigh - prof.m_phasesLow + 1 );
    // Tempo and key of every phase.
    for ( int p = 0; p < phases; ++p )
    {
        double b = bpm;
        int    key = 0;
        if ( p > 0 )
        {
            if ( rng.uniform() < prof.m_newTempoChance )
                b = std::clamp( bpm * ( 0.88 + 0.24 * static_cast<double>( rng.uniform() ) ),
                                static_cast<double>( prof.m_bpmLow ), static_cast<double>( prof.m_bpmHigh ) );
            if ( rng.uniform() < prof.m_newKeyChance )
            {
                // Up a fourth, down a fourth, the relative, a tone down; and the style guide's shifts by a third (3.7):
                // a minor third down, a major third up.
                const int moves[6] = { 5, -5, 3, -2, -3, 4 };
                key = moves[rng.below( 6 )];
            }
        }
        f.m_phaseBpm.push_back( std::round( b * 10.0 ) / 10.0 );
        f.m_phaseKey.push_back( key );
    }

    // Seconds: intro, coda, bridges, the rest to the phases.
    const double total = std::max( 4.0, minutes ) * 60.0;
    // (A night set's piece has them in bars, so that it overlaps its neighbours by whole bars.)
    const double intro =
        prof.m_introBars > 0 ? prof.m_introBars * BeatsPerBar * 60.0 / f.m_phaseBpm.front() : prof.m_introShare * total;
    const double coda =
        prof.m_codaBars > 0 ? prof.m_codaBars * BeatsPerBar * 60.0 / f.m_phaseBpm.back() : prof.m_codaShare * total;
    const double        bridge = 0.06 * total;
    const double        phaseTotal = std::max( 60.0, total - intro - coda - bridge * ( phases - 1 ) );
    std::vector<double> phaseSec;
    double              sum = 0.0;
    for ( int p = 0; p < phases; ++p )
    {
        phaseSec.push_back( 0.8 + 0.4 * static_cast<double>( rng.uniform() ) );
        sum += phaseSec.back();
    }
    for ( double &x : phaseSec ) x *= phaseTotal / sum;

    auto add = [&]( SectionTypeE t, int phase, int index, double seconds, double tempo, float e0, float e1 )
    {
        Section s;
        s.m_type = t;
        s.m_phase = phase;
        s.m_index = index;
        s.m_beat = f.m_lengthBeats;
        const int bars =
            std::max( minBars( t ), static_cast<int>( std::lround( seconds * tempo / 60.0 / BeatsPerBar ) ) );
        s.m_length = bars * BeatsPerBar;
        s.m_e0 = e0;
        s.m_e1 = e1;
        f.m_sections.push_back( s );
        f.m_lengthBeats += s.m_length;
    };

    add( SectionTypeE::ATMO, 0, 0, intro, f.m_phaseBpm[0], 0.05f, 0.15f );
    for ( int p = 0; p < phases; ++p )
    {
        const double tempo = f.m_phaseBpm[static_cast<size_t>( p )];
        const bool   last = p == phases - 1;
        const int    builds = 1 + rng.below( 3 );
        const bool   lead = rng.uniform() < prof.m_leadChance;
        // Weights of the sections inside the phase.
        std::vector<std::pair<SectionTypeE, double>> parts = { { SectionTypeE::ENTRY, 0.10 } };
        for ( int b = 0; b < builds; ++b ) parts.push_back( { SectionTypeE::BUILD, 0.18 } );
        if ( lead ) parts.push_back( { SectionTypeE::LEAD, 0.16 } );
        parts.push_back( { SectionTypeE::PEAK, 0.14 } );
        if ( !last ) parts.push_back( { SectionTypeE::BREAKDOWN, 0.08 } );
        double w = 0.0;
        for ( const auto &x : parts ) w += x.second;
        int b = 0;
        for ( const auto &x : parts )
        {
            const double sec = phaseSec[static_cast<size_t>( p )] * x.second / w;
            switch ( x.first )
            {
            case SectionTypeE::ENTRY:
                add( x.first, p, 0,
                     p == 0 && prof.m_entryBars > 0 ? prof.m_entryBars * BeatsPerBar * 60.0 / tempo : sec, tempo, 0.2f,
                     0.3f );
                break;
            case SectionTypeE::BUILD:
            {
                const float a = 0.3f + 0.45f * static_cast<float>( b ) / builds,
                            c = 0.3f + 0.45f * static_cast<float>( b + 1 ) / builds;
                add( x.first, p, b, sec, tempo, a, c );
                ++b;
                break;
            }
            case SectionTypeE::LEAD: add( x.first, p, 0, sec, tempo, 0.75f, 0.85f ); break;
            case SectionTypeE::PEAK: add( x.first, p, 0, sec, tempo, 0.95f, 1.0f ); break;
            case SectionTypeE::BREAKDOWN: add( x.first, p, 0, sec, tempo, 0.5f, 0.35f ); break;
            default: break;
            }
        }
        if ( !last )
            add( SectionTypeE::BRIDGE, p + 1, 0, bridge, f.m_phaseBpm[static_cast<size_t>( p + 1 )], 0.25f, 0.2f );
    }
    add( SectionTypeE::CODA, phases - 1, 0, coda, f.m_phaseBpm.back(), 0.3f, 0.0f );
    return f;
}

int snapToConjunctions( PieceForm &form, double bassBeats, const std::vector<double> &rowBeats )
{
    // Cycles as whole 48ths of a beat: every step length a row can have (down to a sixteenth triplet) is one.
    auto ticks = []( double beats ) { return static_cast<int64_t>( std::llround( beats * 48.0 ) ); };
    int  moved = 0;
    for ( size_t i = 0; i + 1 < form.m_sections.size(); ++i )
    {
        Section &s = form.m_sections[i];
        Section &next = form.m_sections[i + 1];
        if ( s.m_type != SectionTypeE::BUILD || s.m_index >= static_cast<int>( rowBeats.size() ) ) continue;
        const int64_t a = ticks( bassBeats ), b = ticks( rowBeats[static_cast<size_t>( s.m_index )] );
        if ( a <= 0 || b <= 0 ) continue;
        const double period = static_cast<double>( std::lcm( a, b ) ) / 48.0;
        const double end = s.m_beat + s.m_length;
        double       best = -1.0;
        for ( double k = std::max( 1.0, std::floor( s.m_length / period ) ); k <= std::ceil( s.m_length / period );
              k += 1.0 )
        {
            const double cand = s.m_beat + k * period;
            if ( best < 0.0 || std::fabs( cand - end ) < std::fabs( best - end ) ) best = cand;
        }
        if ( best < 0.0 || std::fabs( best - end ) < 1e-9 ) continue;
        const double onBar = best / BeatsPerBar;
        if ( std::fabs( onBar - std::round( onBar ) ) > 1e-9 ) continue;   // a section starts on a bar
        if ( std::fabs( best - end ) > std::max( 2.0 * BeatsPerBar, 0.25 * s.m_length ) ) continue;
        const double len = best - s.m_beat, nextLen = next.m_beat + next.m_length - best;
        if ( len < minBars( s.m_type ) * BeatsPerBar || nextLen < minBars( next.m_type ) * BeatsPerBar ) continue;
        s.m_length = len;
        next.m_beat = best;
        next.m_length = nextLen;
        ++moved;
    }
    return moved;
}

}   // namespace eph
