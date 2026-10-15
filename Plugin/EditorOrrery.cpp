/**
 * @file EditorOrrery.cpp
 * @brief The rack page and its orrery (EditorOrrery.h).
 * @author Rene Weller
 */
#include "EditorOrrery.h"
#include "EditorTheme.h"
#include "PluginEditor.h"
#include "eph/Dsp.h"
#include <cmath>

using namespace eph;

namespace
{

const juce::Colour Back = ephui::colour::Bg;       ///< the window: midnight
const juce::Colour Ink = ephui::colour::Ink;       ///< text: parchment
const juce::Colour Dim = ephui::colour::Dim;       ///< names, secondary text
const juce::Colour Faint = ephui::colour::Faint;   ///< tracks, axes, the off state
const juce::Colour Sun = ephui::colour::Amber;     ///< the sun: the accent

/**
 * @brief A colour per row, warm inside, cool outside.
 * @param[in] r  the row
 * @return its colour
 */
juce::Colour rowColour( int r )
{
    static const juce::uint32 c[8] = { 0xffe0a458, 0xffd9825b, 0xffc9a45c, 0xff9fbf6f,
                                       0xff6fb8ae, 0xff6f8fb8, 0xffa58ac2, 0xffc47a9c };
    return juce::Colour( c[r & 7] );
}

const char *const NoteNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};   ///< the notes' names

/**
 * @brief A step length in beats as a note value.
 * @param[in] beats  the step's length in beats
 * @return its note value
 */
juce::String noteValue( double beats )
{
    struct V
    {
        double      m_beats;
        const char *m_name;
    };
    static const V values[] = { { 4.0, "1 bar" }, { 1.0, "1/4" },         { 0.5, "1/8" },   { 1.0 / 3.0, "1/8T" },
                                { 0.25, "1/16" }, { 1.0 / 6.0, "1/16T" }, { 0.125, "1/32" } };
    for ( const V &v : values )
        if ( std::fabs( beats - v.m_beats ) < 1e-6 ) return v.m_name;
    return juce::String( beats, 3 ) + " beats";
}

}   // namespace

// ==================================================================== OrreryView

OrreryView::OrreryView( EphemerisProcessor &p ) : m_proc( p )
{
    startTimerHz( 30 );
}

OrreryView::~OrreryView()
{
    stopTimer();
}

void OrreryView::timerCallback()
{
    if ( m_proc.scoreVersion() != m_version )
    {
        m_version = m_proc.scoreVersion();
        m_proc.copyScore( m_score );
    }
    repaint();
}

std::vector<OrreryView::RowState> OrreryView::rowsAt( double beat ) const
{
    std::vector<RowState> rows( Rows );
    for ( const RowShape &s : m_score.m_rowShapes )
    {
        if ( s.m_from > beat || s.m_row < 0 || s.m_row >= Rows ) continue;
        RowState &r = rows[static_cast<size_t>( s.m_row )];
        r.m_known = true;
        r.m_length = s.m_length;
        r.m_divBeats = s.m_divBeats;
        r.m_transposer = s.m_transposer;
    }
    // Start and stop as Rack::run applies them: a row index below zero means every row.
    for ( const RackEvent &e : m_score.m_rack )
    {
        if ( e.m_beat > beat ) break;
        if ( e.m_op != RackOpE::START && e.m_op != RackOpE::STOP ) continue;
        const int lo = e.m_row < 0 ? 0 : e.m_row, hi = e.m_row < 0 ? Rows - 1 : e.m_row;
        for ( int i = lo; i <= hi && i < Rows; ++i )
        {
            RowState &r = rows[static_cast<size_t>( i )];
            r.m_running = e.m_op == RackOpE::START;
            if ( r.m_running ) r.m_start = e.m_beat;
        }
    }
    for ( RowState &r : rows )
    {
        const double period = r.m_length * r.m_divBeats;
        if ( r.m_known && r.m_running && period > 0.0 )
        {
            const double t = ( beat - r.m_start ) / period;
            r.m_cycle = t - std::floor( t );
        }
    }
    return rows;
}

double OrreryView::nextConjunction( const std::vector<RowState> &rows, double beat )
{
    // Candidates are the returns of the longest running cycle; every other running row has to be on its
    // first step there too. Rows start on bars and steps are simple fractions of a beat, so a small
    // tolerance is exact enough.
    const RowState *longest = nullptr;
    int             running = 0;
    for ( const RowState &r : rows )
        if ( r.m_known && r.m_running && !r.m_transposer )
        {
            ++running;
            if ( longest == nullptr || r.m_length * r.m_divBeats > longest->m_length * longest->m_divBeats )
                longest = &r;
        }
    if ( longest == nullptr || running < 2 ) return -1.0;
    const double period = longest->m_length * longest->m_divBeats;
    const double first = longest->m_start + std::ceil( ( beat - longest->m_start ) / period + 1e-9 ) * period;
    for ( int k = 0; k < 4000; ++k )
    {
        const double t = first + k * period;
        bool         all = true;
        for ( const RowState &r : rows )
        {
            if ( !r.m_known || !r.m_running || r.m_transposer ) continue;
            const double p = r.m_length * r.m_divBeats;
            const double q = ( t - r.m_start ) / p;
            if ( std::fabs( q - std::round( q ) ) > 1e-6 )
            {
                all = false;
                break;
            }
        }
        if ( all ) return t;
    }
    return -1.0;
}

void OrreryView::paint( juce::Graphics &g )
{
    const auto area = getLocalBounds().toFloat().reduced( 8.0f );
    g.setColour( Back );
    g.fillRoundedRectangle( getLocalBounds().toFloat().reduced( 2.0f ), 6.0f );
    const double                beat = m_proc.positionBeats();
    const std::vector<RowState> rows = rowsAt( beat );
    const float                 legend = 60.0f;
    const auto                  disc = area.withTrimmedBottom( legend );
    const juce::Point<float>    c = disc.getCentre();
    const float                 outer = 0.5f * juce::jmin( disc.getWidth(), disc.getHeight() ) - 6.0f;
    const float                 sunR = outer * 0.16f;

    // The orbits of the rows that play notes, the bass innermost.
    std::vector<int> order;
    for ( int r = 0; r < Rows; ++r )
        if ( rows[static_cast<size_t>( r )].m_known && !rows[static_cast<size_t>( r )].m_transposer )
            order.push_back( r );
    const int n = juce::jmax( 1, static_cast<int>( order.size() ) );
    int       onFirst = 0, runningRows = 0;
    for ( int k = 0; k < static_cast<int>( order.size() ); ++k )
    {
        const RowState &r = rows[static_cast<size_t>( order[static_cast<size_t>( k )] )];
        const float     radius = sunR + ( outer - sunR ) * ( static_cast<float>( k ) + 1.0f ) / static_cast<float>( n );
        const juce::Colour col = rowColour( order[static_cast<size_t>( k )] );
        g.setColour( r.m_running ? col.withAlpha( 0.35f ) : Faint );
        g.drawEllipse( c.x - radius, c.y - radius, 2.0f * radius, 2.0f * radius, r.m_running ? 1.4f : 1.0f );
        // A tick per step, the first one longer: the top of every orbit.
        for ( int s = 0; s < r.m_length && r.m_length <= 32; ++s )
        {
            const float a =
                juce::MathConstants<float>::twoPi * static_cast<float>( s ) / static_cast<float>( r.m_length ) -
                juce::MathConstants<float>::halfPi;
            const float len = s == 0 ? 6.0f : 2.5f;
            g.setColour( r.m_running ? col.withAlpha( s == 0 ? 0.8f : 0.35f ) : Faint );
            g.drawLine( c.x + ( radius - len ) * std::cos( a ), c.y + ( radius - len ) * std::sin( a ),
                        c.x + ( radius + len ) * std::cos( a ), c.y + ( radius + len ) * std::sin( a ),
                        s == 0 ? 1.6f : 1.0f );
        }
        if ( !r.m_running ) continue;
        ++runningRows;
        const int step = static_cast<int>( r.m_cycle * r.m_length );
        if ( step == 0 ) ++onFirst;
        const float a =
            juce::MathConstants<float>::twoPi * static_cast<float>( r.m_cycle ) - juce::MathConstants<float>::halfPi;
        const juce::Point<float> pl( c.x + radius * std::cos( a ), c.y + radius * std::sin( a ) );
        g.setColour( col.withAlpha( 0.25f ) );
        g.fillEllipse( pl.x - 9.0f, pl.y - 9.0f, 18.0f, 18.0f );
        g.setColour( col );
        g.fillEllipse( pl.x - 5.0f, pl.y - 5.0f, 10.0f, 10.0f );
    }

    // A conjunction: running rows together on their first step, a line of light from the sun upwards.
    if ( runningRows >= 2 && onFirst >= 2 )
    {
        const float strength = static_cast<float>( onFirst ) / static_cast<float>( runningRows );
        g.setGradientFill( juce::ColourGradient( Sun.withAlpha( 0.2f + 0.7f * strength ), c.x, c.y - sunR,
                                                 Sun.withAlpha( 0.0f ), c.x, c.y - outer - 6.0f, false ) );
        g.fillRect( juce::Rectangle<float>( c.x - 2.0f - 2.0f * strength, c.y - outer - 6.0f, 4.0f + 4.0f * strength,
                                            outer + 6.0f - sunR ) );
    }

    // The sun: the root the transposer puts the rows on.
    const RowState *tr = nullptr;
    for ( const RowState &r : rows )
        if ( r.m_known && r.m_transposer ) tr = &r;
    const bool moving = tr != nullptr && tr->m_running;
    g.setColour( Sun.withAlpha( moving ? 0.35f : 0.15f ) );
    g.fillEllipse( c.x - sunR * 1.35f, c.y - sunR * 1.35f, sunR * 2.7f, sunR * 2.7f );
    g.setColour( Sun.withAlpha( moving ? 1.0f : 0.6f ) );
    g.fillEllipse( c.x - sunR, c.y - sunR, 2.0f * sunR, 2.0f * sunR );
    g.setColour( Back );
    g.setFont( juce::Font( juce::FontOptions( sunR * 0.9f, juce::Font::bold ) ) );
    const int root = pitchClass( m_score.m_keyRoot + m_score.rootAt( beat ) );
    g.drawText( NoteNames[root], juce::Rectangle<float>( c.x - sunR, c.y - sunR, 2.0f * sunR, 2.0f * sunR ),
                juce::Justification::centred );

    // The legend: each row's cycle, and when the running rows meet again.
    auto text = area.withTop( area.getBottom() - legend );
    g.setFont( juce::Font( juce::FontOptions( 12.0f ) ) );
    juce::String line;
    for ( int r : order )
    {
        const RowState &s = rows[static_cast<size_t>( r )];
        line << "Row " << ( r + 1 ) << ": " << s.m_length << " x " << noteValue( s.m_divBeats ) << "    ";
    }
    g.setColour( Dim );
    g.drawFittedText( line.trim(), text.removeFromTop( 34.0f ).toNearestInt(), juce::Justification::centred, 2 );
    const double next = nextConjunction( rows, beat );
    const double bpm = m_score.m_tempo.bpmAt( beat );
    juce::String when = "no conjunction ahead while these rows play";
    if ( next > 0.0 )
    {
        const double bars = ( next - beat ) / 4.0;
        const double seconds = ( next - beat ) * 60.0 / juce::jmax( 1.0, bpm );
        when = "all running rows meet again in " + juce::String( bars, 1 ) + " bars (" +
               juce::String( juce::roundToInt( seconds ) ) + " s)";
    }
    g.setColour( onFirst >= 2 ? Sun : Ink );
    g.drawFittedText( when, text.toNearestInt(), juce::Justification::centred, 1 );
}

// ==================================================================== RackPage

RackPage::RackPage( EphemerisProcessor &p, std::unique_ptr<ParamPage> params )
    : m_orrery( p ), m_params( std::move( params ) )
{
    addAndMakeVisible( m_orrery );
    m_view.setViewedComponent( m_params.get(), false );
    m_view.setScrollBarsShown( true, false );
    addAndMakeVisible( m_view );
}

RackPage::~RackPage() = default;

void RackPage::resized()
{
    auto      r = getLocalBounds();
    const int side = juce::jmin( r.getHeight(), r.getWidth() / 2 );
    m_orrery.setBounds( r.removeFromLeft( side ) );
    ScrollingPage::fit( *m_params, m_view, r );
}
