/**
 * @file EditorGestures.cpp
 * @brief The gestures page (EditorGestures.h).
 * @author Rene Weller
 */
#include "EditorGestures.h"
#include "EditorTheme.h"
#include <algorithm>
#include <cmath>
#include <map>

using namespace eph;

namespace
{
const juce::Colour Back = ephui::colour::Bg;          ///< the window: midnight
const juce::Colour Ink = ephui::colour::Ink;          ///< text: parchment
const juce::Colour Dim = ephui::colour::Dim;          ///< names, secondary text
const juce::Colour Faint = ephui::colour::Edge;       ///< hairlines
const juce::Colour Playhead = ephui::colour::Amber;   ///< the sun: the accent
const juce::Colour Hand[2] = { ephui::familyColour( ephui::Family::SOURCE ),
                               ephui::familyColour( ephui::Family::MOTION ) };   ///< left hand warm, right hand cool
constexpr float    NameWidth = 150.0f;                                           ///< the lanes' names' column, px
constexpr float    HandsHeight = 22.0f;                                          ///< the hands' strip at the top, px
}   // namespace

GestureView::GestureView( EphemerisProcessor &p ) : m_proc( p )
{
    startTimerHz( 10 );
}

GestureView::~GestureView()
{
    stopTimer();
}

void GestureView::timerCallback()
{
    if ( m_proc.scoreVersion() != m_version )
    {
        m_version = m_proc.scoreVersion();
        m_proc.copyScore( m_score );
        rebuild();
    }
    repaint();
}

void GestureView::resized()
{
    rebuild();
}

float GestureView::xOf( double beat ) const
{
    const double len = std::max( 1.0, m_score.m_lengthBeats );
    return m_area.getX() + static_cast<float>( beat / len ) * m_area.getWidth();
}

void GestureView::rebuild()
{
    m_area =
        getLocalBounds().toFloat().reduced( 8.0f ).withTrimmedLeft( NameWidth ).withTrimmedTop( HandsHeight + 6.0f );
    std::map<int, Lane> byParam;
    for ( const Gesture &g : m_score.m_gestures )
    {
        if ( g.m_param < 0 ) continue;
        Lane &l = byParam[g.m_param];
        if ( l.m_gestures.empty() )
        {
            l.m_param = g.m_param;
            l.m_hand = g.m_hand & 1;
        }
        l.m_gestures.push_back( g );
    }
    m_lanes.clear();
    const ParamStore &s = m_proc.store();
    for ( auto &[param, lane] : byParam )
    {
        if ( param < s.count() ) lane.m_name = juce::String( s.key( param ) );
        std::stable_sort( lane.m_gestures.begin(), lane.m_gestures.end(),
                          []( const Gesture &a, const Gesture &b ) { return a.m_beat < b.m_beat; } );
        m_lanes.push_back( std::move( lane ) );
    }
    // The curves at this size: the offset the engine plays at each pixel (the latest gesture that has started).
    m_curves.assign( m_lanes.size(), juce::Path() );
    if ( m_lanes.empty() || m_area.getWidth() < 10.0f ) return;
    const float laneH = m_area.getHeight() / static_cast<float>( m_lanes.size() );
    const int   steps = static_cast<int>( m_area.getWidth() );
    for ( size_t k = 0; k < m_lanes.size(); ++k )
    {
        const Lane &l = m_lanes[k];
        const float mid = m_area.getY() + laneH * ( static_cast<float>( k ) + 0.5f ), half = laneH * 0.42f;
        size_t      cur = 0;
        bool        started = false;
        for ( int i = 0; i <= steps; ++i )
        {
            const double beat = m_score.m_lengthBeats * i / steps;
            while ( cur + 1 < l.m_gestures.size() && l.m_gestures[cur + 1].m_beat <= beat ) ++cur;
            const float v = l.m_gestures[cur].m_beat <= beat ? gestureValue( l.m_gestures[cur], beat ) : 0.0f;
            const float x = m_area.getX() + static_cast<float>( i ), y = mid - half * std::clamp( v, -1.0f, 1.0f );
            if ( !started )
            {
                m_curves[k].startNewSubPath( x, y );
                started = true;
            }
            else
                m_curves[k].lineTo( x, y );
        }
    }
}

void GestureView::paint( juce::Graphics &g )
{
    g.fillAll( Back );
    if ( m_lanes.empty() )
    {
        g.setColour( Dim );
        g.drawText( "No gestures in this piece.", getLocalBounds(), juce::Justification::centred );
        return;
    }
    // The sections behind everything, as faint bands with their names on top.
    g.setFont( juce::Font( juce::FontOptions( 10.5f ) ) );
    for ( size_t i = 0; i < m_score.m_markers.size(); ++i )
    {
        const double b0 = m_score.m_markers[i].m_beat;
        const double b1 = i + 1 < m_score.m_markers.size() ? m_score.m_markers[i + 1].m_beat : m_score.m_lengthBeats;
        const float  x0 = xOf( b0 ), x1 = xOf( b1 );
        if ( i % 2 == 1 )
        {
            g.setColour( Faint.withAlpha( 0.35f ) );
            g.fillRect( x0, m_area.getY(), x1 - x0, m_area.getHeight() );
        }
        g.setColour( Faint.brighter( 0.6f ) );
        g.drawVerticalLine( juce::roundToInt( x0 ), m_area.getY(), m_area.getBottom() );
    }
    // The hands: where each is busy.
    for ( int h = 0; h < 2; ++h )
    {
        const float y = 8.0f + static_cast<float>( h ) * ( HandsHeight * 0.5f );
        g.setColour( Dim );
        g.drawText( h == 0 ? "player, left" : "player, right",
                    juce::Rectangle<float>( 8.0f, y - 2.0f, NameWidth - 12.0f, HandsHeight * 0.5f ),
                    juce::Justification::centredRight );
        g.setColour( Hand[h].withAlpha( 0.8f ) );
        for ( const Gesture &ge : m_score.m_gestures )
            if ( ( ge.m_hand & 1 ) == h && ge.m_length > 0.0 )
                g.fillRect( juce::Rectangle<float>( xOf( ge.m_beat ), y,
                                                    std::max( 1.0f, xOf( ge.m_beat + ge.m_length ) - xOf( ge.m_beat ) ),
                                                    HandsHeight * 0.5f - 3.0f ) );
    }
    // The lanes.
    const float laneH = m_area.getHeight() / static_cast<float>( m_lanes.size() );
    for ( size_t k = 0; k < m_lanes.size(); ++k )
    {
        const float top = m_area.getY() + laneH * static_cast<float>( k ), mid = top + laneH * 0.5f;
        g.setColour( Faint );
        g.drawHorizontalLine( juce::roundToInt( mid ), m_area.getX(), m_area.getRight() );
        g.setColour( Ink );
        g.setFont( juce::Font( juce::FontOptions( std::min( 12.0f, laneH * 0.7f ) ) ) );
        g.drawText( m_lanes[k].m_name, juce::Rectangle<float>( 8.0f, top, NameWidth - 12.0f, laneH ),
                    juce::Justification::centredRight );
        g.setColour( Hand[m_lanes[k].m_hand].withAlpha( 0.9f ) );
        g.strokePath( m_curves[k], juce::PathStrokeType( 1.5f ) );
    }
    // The playhead.
    const float x = xOf( m_proc.positionBeats() );
    g.setColour( Playhead );
    g.drawLine( x, 4.0f, x, m_area.getBottom(), 1.5f );
}

void GestureView::mouseDown( const juce::MouseEvent &e )
{
    if ( m_area.getWidth() <= 0.0f || e.position.x < m_area.getX() ) return;
    m_proc.seekTo( std::clamp( ( e.position.x - m_area.getX() ) / m_area.getWidth(), 0.0f, 1.0f ) *
                   m_score.m_lengthBeats );
}
