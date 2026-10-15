/**
 * @file EditorStyle.cpp
 * @brief The style page (EditorStyle.h).
 * @author Rene Weller
 */
#include "EditorStyle.h"
#include "EditorTheme.h"
#include "PluginEditor.h"
#include "eph/compose/Style.h"
#include <functional>

using namespace eph;

namespace
{
const juce::Colour Back = ephui::colour::Panel;     ///< a page (the orrery shows faintly through it)
const juce::Colour Ink = ephui::colour::Ink;        ///< text: parchment
const juce::Colour Dim = ephui::colour::Dim;        ///< names, secondary text
const juce::Colour Faint = ephui::colour::Edge;     ///< hairlines
const juce::Colour Accent = ephui::colour::Amber;   ///< the sun: the accent

/**
 * @brief @p x (0..1) as a percentage.
 * @param[in] x  the share, 0..1
 * @return the percentage
 */
juce::String percent( float x )
{
    return juce::String( juce::roundToInt( 100.0f * x ) ) + " %";
}
/**
 * @brief "@p a - @p b" with @p decimals decimals (one number where they are equal).
 * @param[in] a         the low end
 * @param[in] b         the high end
 * @param[in] decimals  the decimals shown
 * @return the range as text
 */
juce::String range( double a, double b, int decimals = 0 )
{
    return a == b ? juce::String( a, decimals ) : juce::String( a, decimals ) + " - " + juce::String( b, decimals );
}
/**
 * @brief The name of row division @p d.
 * @param[in] d  the row division
 * @return its name
 */
juce::String division( RowDivisionE d )
{
    switch ( d )
    {
    case RowDivisionE::QUARTER: return "1/4";
    case RowDivisionE::EIGHTH: return "1/8";
    case RowDivisionE::EIGHTH_T: return "1/8T";
    case RowDivisionE::SIXTEENTH: return "1/16";
    case RowDivisionE::SIXTEENTH_T: return "1/16T";
    case RowDivisionE::THIRTY_SECOND: return "1/32";
    case RowDivisionE::BAR1: return "1 bar";
    case RowDivisionE::BARS2: return "2 bars";
    case RowDivisionE::BARS4: return "4 bars";
    default: return "?";
    }
}

/** @brief One line of the table: its label and how a profile says it. */
struct Line
{
    const char                                         *m_label;   ///< the line's label
    std::function<juce::String( const StyleProfile & )> m_value;   ///< how a profile says it
};

/**
 * @brief The lines of the table, built on first use.
 * @return the table's lines
 */
const std::vector<Line> &lines()
{
    static const std::vector<Line> l = {
        { "Tempo (BPM)", []( const StyleProfile &p ) { return range( p.m_bpmLow, p.m_bpmHigh ); } },
        { "Piece in a concert (min)",
          []( const StyleProfile &p ) { return range( p.m_minutesLow, p.m_minutesHigh ); } },
        { "Sequence phases", []( const StyleProfile &p ) { return range( p.m_phasesLow, p.m_phasesHigh ); } },
        { "Atmosphere before / coda after",
          []( const StyleProfile &p ) { return percent( p.m_introShare ) + " / " + percent( p.m_codaShare ); } },
        { "New tempo / new key in a phase",
          []( const StyleProfile &p ) { return percent( p.m_newTempoChance ) + " / " + percent( p.m_newKeyChance ); } },
        { "Rows at the peak", []( const StyleProfile &p ) { return juce::String( p.m_peakRows ); } },
        { "Counter row lengths",
          []( const StyleProfile &p )
          {
              juce::String s;
              for ( int n : p.m_counterLengths ) s << n << " ";
              return s.trim();
          } },
        { "Mutation per cycle", []( const StyleProfile &p ) { return percent( p.m_mutation ); } },
        { "Transposer", []( const StyleProfile &p )
          { return juce::String( p.m_transposerLength ) + " x " + division( p.m_transposerDivision ); } },
        { "Tape keys", []( const StyleProfile &p ) { return percent( p.m_tapeChance ); } },
        { "String machine", []( const StyleProfile &p ) { return percent( p.m_stringsChance ); } },
        { "Lead section", []( const StyleProfile &p ) { return percent( p.m_leadChance ); } },
        { "Bleeps", []( const StyleProfile &p ) { return percent( p.m_bleepChance ); } },
        { "Drums", []( const StyleProfile &p ) { return percent( p.m_drumsChance ); } },
        { "Granular cloud", []( const StyleProfile &p ) { return percent( p.m_grainChance ); } },
        { "Tape set",
          []( const StyleProfile &p ) { return juce::String( TapeSetNames[static_cast<int>( p.m_tape )] ); } },
        { "Lead density", []( const StyleProfile &p ) { return juce::String( p.m_leadIntensity, 2 ); } },
        { "Moves: length / rest",
          []( const StyleProfile &p )
          {
              return juce::String( p.m_hands.m_medianSeconds, 0 ) + " s / " +
                     juce::String( p.m_hands.m_restSeconds, 0 ) + " s";
          } },
        { "Darkness", []( const StyleProfile &p ) { return juce::String( p.m_darkness, 2 ); } },
        { "Hall", []( const StyleProfile &p ) { return juce::String( p.m_hallSeconds, 1 ) + " s"; } },
        { "Level", []( const StyleProfile &p ) { return juce::String( p.m_levelDb, 1 ) + " dB"; } },
    };
    return l;
}
}   // namespace

StylePage::StylePage( EphemerisProcessor &p, std::unique_ptr<ParamPage> custom )
    : m_proc( p ), m_custom( std::move( custom ) )
{
    m_view.setViewedComponent( m_custom.get(), false );
    m_view.setScrollBarsShown( true, false );
    addAndMakeVisible( m_view );
    m_copy.onClick = [this] { copyStyle(); };
    addAndMakeVisible( m_copy );
    startTimerHz( 4 );
    timerCallback();
}

void StylePage::resized()
{
    auto r = getLocalBounds();
    m_table = r.removeFromLeft( r.getWidth() * 3 / 5 );
    auto right = r.reduced( 8, 6 );
    m_copy.setBounds( right.removeFromTop( 28 ).withWidth( std::min( right.getWidth(), 260 ) ) );
    right.removeFromTop( 6 );
    m_view.setBounds( right );
    m_custom->setAvailableHeight( right.getHeight() );   // its sections are cut to it (01.10.2026)
    const int w = right.getWidth() - m_view.getScrollBarThickness();
    m_custom->setSize( w, std::max( right.getHeight(), m_custom->heightFor( w ) ) );
}

void StylePage::copyStyle()
{
    // Through the host parameters, so the controls, the host and the saved state all see the new values.
    ParamStore scratch;
    scratch.copyValuesFrom( m_proc.store() );
    copyToCustom( styleProfile( static_cast<StyleE>( std::max( 0, m_style ) ) ), scratch );
    for ( int index = custom::BPM_LOW; index < custom::COUNT; ++index )
    {
        const int id = scratch.id( ModuleE::CUSTOM, 0, index );
        if ( StoreParameter *sp = m_proc.parameter( id ) )
        {
            sp->beginChangeGesture();
            sp->setValueNotifyingHost( scratch.toNormalised( id, scratch.get( id ) ) );
            sp->endChangeGesture();
        }
    }
}

StylePage::~StylePage()
{
    stopTimer();
}

void StylePage::timerCallback()
{
    const ParamStore &s = m_proc.store();
    const int         style = s.getInt( s.id( ModuleE::COMPOSE, 0, compose::STYLE ) );
    float             hash = 0.0f;
    for ( int index = 0; index < custom::COUNT; ++index )
        hash += s.get( s.id( ModuleE::CUSTOM, 0, index ) ) * static_cast<float>( index + 1 );
    if ( style != m_style || hash != m_customHash )
    {
        m_style = style;
        m_customHash = hash;
        m_copy.setButtonText( "Copy " + juce::String( StyleNames[std::max( 0, style )] ) + " into Custom" );
        repaint();
    }
}

void StylePage::paint( juce::Graphics &g )
{
    g.fillAll( Back );
    const int          styles = static_cast<int>( StyleE::COUNT );
    const ParamStore  &store = m_proc.store();
    const bool         own = customStyleOn( store );
    const StyleProfile mine = customProfile( store, styleProfile( static_cast<StyleE>( std::max( 0, m_style ) ) ) );
    // Columns: the five styles, then the user's own; the one the composer uses is lit.
    const int   columns = styles + 1, lit = own ? styles : m_style;
    auto        area = m_table.toFloat().reduced( 12.0f, 10.0f );
    const float labelW = 190.0f, colW = ( area.getWidth() - labelW ) / static_cast<float>( columns );
    const auto &rows = lines();
    const float rowH = std::min( 22.0f, ( area.getHeight() - 60.0f ) / static_cast<float>( rows.size() + 1 ) );
    // The chosen style's column.
    if ( lit >= 0 && lit < columns )
    {
        g.setColour( Accent.withAlpha( 0.12f ) );
        g.fillRoundedRectangle( area.getX() + labelW + colW * static_cast<float>( lit ), area.getY(), colW,
                                rowH * static_cast<float>( rows.size() + 1 ) + 4.0f, 4.0f );
    }
    g.setFont( juce::Font( juce::FontOptions( 12.5f, juce::Font::bold ) ) );
    for ( int s = 0; s < columns; ++s )
    {
        g.setColour( s == lit ? Accent : ( s == styles && !own ? Dim : Ink ) );
        g.drawText(
            s < styles ? juce::String( styleProfile( static_cast<StyleE>( s ) ).m_name ) : juce::String( "Custom" ),
            juce::Rectangle<float>( area.getX() + labelW + colW * static_cast<float>( s ), area.getY(), colW, rowH ),
            juce::Justification::centred );
    }
    g.setFont( juce::Font( juce::FontOptions( 12.5f ) ) );
    for ( size_t i = 0; i < rows.size(); ++i )
    {
        const float y = area.getY() + rowH * static_cast<float>( i + 1 );
        if ( i % 2 == 0 )
        {
            g.setColour( Faint.withAlpha( 0.5f ) );
            g.fillRect( area.getX(), y, area.getWidth(), rowH );
        }
        g.setColour( Dim );
        g.drawText( rows[i].m_label, juce::Rectangle<float>( area.getX() + 4.0f, y, labelW - 8.0f, rowH ),
                    juce::Justification::centredLeft );
        for ( int s = 0; s < columns; ++s )
        {
            g.setColour( s == lit ? Ink : ( s == styles && !own ? Dim.withAlpha( 0.7f ) : Ink.withAlpha( 0.75f ) ) );
            g.drawText( rows[i].m_value( s < styles ? styleProfile( static_cast<StyleE>( s ) ) : mine ),
                        juce::Rectangle<float>( area.getX() + labelW + colW * static_cast<float>( s ), y, colW, rowH ),
                        juce::Justification::centred );
        }
    }
    g.setColour( Dim );
    g.setFont( juce::Font( juce::FontOptions( 12.0f ) ) );
    const float y = area.getY() + rowH * static_cast<float>( rows.size() + 1 ) + 14.0f;
    g.drawFittedText(
        "A style is a set of ranges and chances the composer draws from: two pieces of one style share these, "
        "not their notes. Custom is a style of your own: copy a style into it, change what you like, and turn "
        "on Use Custom Style; the counter rows, the transposer, the tape set and the drum patterns still come from the "
        "chosen style (Cosmic and Drift have no drum patterns).",
        juce::Rectangle<float>( area.getX(), y, area.getWidth(), 48.0f ).toNearestInt(), juce::Justification::topLeft,
        3 );
}
