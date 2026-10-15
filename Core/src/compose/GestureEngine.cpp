/**
 * @file GestureEngine.cpp
 * @brief The player's hands.
 * @author Rene Weller
 */
#include "eph/compose/GestureEngine.h"
#include <algorithm>
#include <cmath>

namespace eph
{

namespace
{

/**
 * @brief A standard normal draw from @p rng.
 * @param[in,out] rng  the random stream
 * @return the draw
 */
double gaussian( Rng &rng )
{
    return rng.gaussian();
}

}   // namespace

void playHands( Score &score, const std::vector<HandKnob> &knobs, const HandStyle &style,
                const std::function<float( double )> &energy, double start, double end, Rng &rng )
{
    if ( knobs.empty() || end <= start ) return;
    std::vector<float>  value( knobs.size(), 0.0f );        // where each knob was left
    std::vector<double> busyUntil( knobs.size(), start );   // beat until which a hand holds it
    // Each knob starts at its resting centre, set once and silently (a step at the start).
    for ( size_t k = 0; k < knobs.size(); ++k )
    {
        value[k] = std::clamp( knobs[k].m_atRest, knobs[k].m_low, knobs[k].m_high );
        score.m_gestures.push_back( { knobs[k].m_param, start, 0.0, value[k], value[k], GestureShapeE::STEP, 0 } );
    }
    double     freeAt[2] = { start + 2.0, start + 6.0 };   // the second hand joins a little later
    const auto beatsOf = [&]( double beat, double seconds ) { return seconds * score.m_tempo.bpmAt( beat ) / 60.0; };

    for ( int guard = 0; guard < 100000; ++guard )
    {
        const int    h = freeAt[0] <= freeAt[1] ? 0 : 1;
        const double t = freeAt[h];
        if ( t >= end ) break;
        // Choose a knob nobody holds and that may be touched now.
        float total = 0.0f;
        for ( size_t k = 0; k < knobs.size(); ++k )
            if ( busyUntil[k] <= t && knobs[k].m_from <= t ) total += knobs[k].m_weight;
        if ( total <= 0.0f )
        {
            freeAt[h] = t + beatsOf( t, 1.0 );
            continue;
        }
        float  u = rng.uniform() * total;
        size_t pick = 0;
        for ( size_t k = 0; k < knobs.size(); ++k )
        {
            if ( busyUntil[k] > t || knobs[k].m_from > t ) continue;
            pick = k;
            u -= knobs[k].m_weight;
            if ( u <= 0.0f ) break;
        }
        const HandKnob &knob = knobs[pick];

        // How long, and which shape.
        double        seconds;
        GestureShapeE shape = GestureShapeE::MINIMUM_JERK;
        const float   kind = rng.uniform();
        if ( knob.m_throws || kind < style.m_quickChance )
        {
            seconds = 1.0 + rng.uniform();
            shape = GestureShapeE::EASE_OUT;
        }
        else if ( kind < style.m_quickChance + style.m_longChance )
        {
            seconds = style.m_medianSeconds * ( 4.0 + 6.0 * rng.uniform() );
        }
        else
        {
            seconds = style.m_medianSeconds * std::exp( style.m_spread * gaussian( rng ) );
            if ( rng.uniform() < 0.2f ) shape = GestureShapeE::EASE_IN;
        }
        double length = beatsOf( t, seconds );
        length = std::min( length, end - t );
        if ( length < 0.25 ) break;

        // Where to: the energy's centre with scatter; a throw goes up and is caught back after it.
        const float e = std::clamp( energy( t + length ), 0.0f, 1.0f );
        const float centre = knob.m_atRest + e * ( knob.m_atPeak - knob.m_atRest );
        float       target = centre + knob.m_scatter * static_cast<float>( gaussian( rng ) );
        if ( knob.m_throws ) target = std::max( value[pick], centre ) + knob.m_scatter * ( 1.0f + rng.uniform() );
        target = std::clamp( target, knob.m_low, knob.m_high );
        score.m_gestures.push_back(
            { knob.m_param, t, length, value[pick], target, shape, static_cast<uint8_t>( h ) } );
        value[pick] = target;
        double done = t + length;
        if ( knob.m_throws )
        {
            // Caught: back to the centre over two to four seconds, by the same hand.
            const double back = std::min( beatsOf( done, 2.0 + 2.0 * rng.uniform() ), end - done );
            if ( back > 0.25 )
            {
                const float home = std::clamp( centre, knob.m_low, knob.m_high );
                score.m_gestures.push_back( { knob.m_param, done, back, target, home, GestureShapeE::MINIMUM_JERK,
                                              static_cast<uint8_t>( h ) } );
                value[pick] = home;
                done += back;
            }
        }
        busyUntil[pick] = done;
        freeAt[h] = done + beatsOf( done, style.m_restSeconds * std::exp( 0.6 * gaussian( rng ) ) );
    }
}

}   // namespace eph
