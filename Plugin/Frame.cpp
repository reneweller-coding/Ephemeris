/**
 * @file Frame.cpp
 * @brief The frame the generators share (Frame.h).
 * @author Rene Weller
 */
#include "Frame.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <array>
#include <algorithm>
#include <map>

namespace frame
{

namespace
{

/**
 * @brief The skin-less default font at @p height, bold or not (the help and the menus).
 * @param[in] height  the font's height in pixels
 * @param[in] bold    true for bold
 * @return the font
 */
juce::Font font( float height, bool bold = false )
{
    return juce::Font( juce::FontOptions( height, bold ? juce::Font::bold : juce::Font::plain ) );
}

/**
 * @brief The headset ports: each instrument its own, so two can listen side by side.
 * @param[in] app  the instrument's name
 * @return its headset port
 */
int portOf( const juce::String &app )
{
    if ( app == "Noctuary" ) return 9000;   // its OSC port since 2.0, which the Quest app's bridge already sends to
    if ( app == "Phosphene" ) return 9101;
    if ( app == "Ephemeris" ) return 9102;
    if ( app == "Totality" ) return 9103;
    if ( app == "Parhelion" ) return 9104;
    if ( app == "Empyrean" ) return 9105;
    return 9100;
}

}   // namespace

// ================================================================================================ the look and feel

LookAndFeel::LookAndFeel( const Skin &skin ) : m_skin( skin )
{
    const Skin &s = m_skin;
    setColour( juce::ResizableWindow::backgroundColourId, s.m_bg );
    setColour( juce::Label::textColourId, s.m_ink );
    setColour( juce::Slider::textBoxTextColourId, s.m_dim );
    setColour( juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack );
    setColour( juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack );
    setColour( juce::Slider::textBoxHighlightColourId, s.m_accent.withAlpha( 0.35f ) );
    setColour( juce::Slider::rotarySliderFillColourId, s.m_accent );
    setColour( juce::Slider::thumbColourId, s.m_accent );
    setColour( juce::Slider::trackColourId, s.m_accent.withAlpha( 0.6f ) );
    setColour( juce::Slider::backgroundColourId, s.m_faint );
    setColour( juce::ComboBox::backgroundColourId, s.m_raised );
    setColour( juce::ComboBox::outlineColourId, s.m_edge );
    setColour( juce::ComboBox::textColourId, s.m_ink );
    setColour( juce::ComboBox::arrowColourId, s.m_dim );
    setColour( juce::PopupMenu::backgroundColourId, s.m_group );
    setColour( juce::PopupMenu::textColourId, s.m_ink );
    setColour( juce::PopupMenu::highlightedBackgroundColourId, s.m_accent.withAlpha( 0.25f ) );
    setColour( juce::PopupMenu::highlightedTextColourId, s.m_ink );
    setColour( juce::PopupMenu::headerTextColourId, s.m_accent );
    setColour( juce::TextButton::buttonColourId, s.m_raised );
    setColour( juce::TextButton::buttonOnColourId, s.m_accent.withAlpha( 0.35f ) );
    setColour( juce::TextButton::textColourOffId, s.m_ink );
    setColour( juce::TextButton::textColourOnId, s.m_ink );
    setColour( juce::ToggleButton::textColourId, s.m_dim );
    setColour( juce::ToggleButton::tickColourId, s.m_accent );
    setColour( juce::TextEditor::backgroundColourId, s.m_raised );
    setColour( juce::TextEditor::textColourId, s.m_ink );
    setColour( juce::TextEditor::outlineColourId, s.m_edge );
    setColour( juce::TextEditor::focusedOutlineColourId, s.m_accent.withAlpha( 0.6f ) );
    setColour( juce::TextEditor::highlightColourId, s.m_accent.withAlpha( 0.3f ) );
    setColour( juce::CaretComponent::caretColourId, s.m_accent );
    setColour( juce::TabbedComponent::backgroundColourId, juce::Colours::transparentBlack );
    setColour( juce::TabbedComponent::outlineColourId, s.m_edge );
    setColour( juce::ScrollBar::thumbColourId, s.m_faint );
    setColour( juce::ListBox::backgroundColourId, juce::Colours::transparentBlack );
    setColour( juce::ListBox::textColourId, s.m_ink );
    setColour( juce::TooltipWindow::backgroundColourId, s.m_group );
    setColour( juce::TooltipWindow::textColourId, s.m_ink );
    setColour( juce::TooltipWindow::outlineColourId, s.m_edge );
    setColour( juce::AlertWindow::backgroundColourId, s.m_group );
    setColour( juce::AlertWindow::textColourId, s.m_ink );
    setColour( juce::AlertWindow::outlineColourId, s.m_edge );
    setColour( juce::HyperlinkButton::textColourId, s.m_accent );
}

void LookAndFeel::drawRotarySlider( juce::Graphics &g, int x, int y, int width, int height, float pos, float startAngle,
                                    float endAngle, juce::Slider &slider )
{
    // A knob as on an instrument's panel: a dark cap with its pointer, and around it the value as an arc in the colour
    // of its family -- from the top where the parameter spans zero (pan, transposition), from the start otherwise.
    const Skin &s = m_skin;
    const auto  bounds = juce::Rectangle<float>( static_cast<float>( x ), static_cast<float>( y ),
                                                 static_cast<float>( width ), static_cast<float>( height ) );
    const float size = std::min( bounds.getWidth(), bounds.getHeight() );
    const auto  c = bounds.getCentre();
    // The modulations on it (ModulationAtKnob; the slider property "mods"): a knob with arcs is drawn a little smaller,
    // so they fit round it.
    const juce::var    mods = slider.getProperties()["mods"];
    const int          numMods = mods.isArray() ? std::min( 4, mods.size() ) : 0;
    const float        ring = size * 0.5f - 3.0f - ( numMods > 0 ? 2.0f + 2.4f * static_cast<float>( numMods ) : 0.0f );
    const float        track = std::max( 2.5f, size * 0.07f );
    const juce::Colour colour =
        slider.isEnabled() ? slider.findColour( juce::Slider::rotarySliderFillColourId ) : s.m_faint;
    const float angle = startAngle + pos * ( endAngle - startAngle );
    float       from = startAngle;
    if ( slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0 )
        from = startAngle + static_cast<float>( slider.valueToProportionOfLength( 0.0 ) ) * ( endAngle - startAngle );
    const float r = ring - track * 0.5f;
    juce::Path  arc;
    arc.addCentredArc( c.x, c.y, r, r, 0.0f, startAngle, endAngle, true );
    g.setColour( s.m_faint.withAlpha( 0.55f ) );
    g.strokePath( arc, juce::PathStrokeType( track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded ) );
    juce::Path value;
    value.addCentredArc( c.x, c.y, r, r, 0.0f, std::min( from, angle ), std::max( from, angle ), true );
    if ( s.m_glow )
    {
        g.setColour( colour.withAlpha( 0.18f ) );
        g.strokePath(
            value, juce::PathStrokeType( track * 2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded ) );
    }
    g.setColour( colour );
    g.strokePath( value, juce::PathStrokeType( track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded ) );
    // The live ring: where the value plays (the score's automation, a section, a match), outside the track.
    const juce::var live = slider.getProperties()["live"];
    if ( !live.isVoid() )
    {
        const float lp = juce::jlimit( 0.0f, 1.0f, static_cast<float>( static_cast<double>( live ) ) );
        if ( std::abs( lp - pos ) > 0.004f )
        {
            const float la = startAngle + lp * ( endAngle - startAngle ), lr = ring + 1.5f;
            juce::Path  l;
            l.addCentredArc( c.x, c.y, lr, lr, 0.0f, std::min( angle, la ), std::max( angle, la ), true );
            g.setColour( colour.brighter( 0.7f ).withAlpha( 0.95f ) );
            g.strokePath( l,
                          juce::PathStrokeType( 1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded ) );
            g.fillEllipse( c.x + std::sin( la ) * lr - 2.2f, c.y - std::cos( la ) * lr - 2.2f, 4.4f, 4.4f );
        }
    }
    // The modulations: per row the span it can move the knob over, faint, and a dot where it moves it now.
    for ( int k = 0; k < numMods; ++k )
    {
        const juce::var &m = mods[k];
        if ( !m.isArray() || m.size() < 4 ) continue;
        const float        d = static_cast<float>( static_cast<double>( m[0] ) );
        const bool         uni = static_cast<bool>( m[1] );
        const float        src = static_cast<float>( static_cast<double>( m[2] ) );
        const juce::Colour col( static_cast<juce::uint32>( static_cast<juce::int64>( m[3] ) ) );
        const float        lo = juce::jlimit( 0.0f, 1.0f, uni ? std::min( pos, pos + d ) : pos - std::abs( d ) );
        const float        hi = juce::jlimit( 0.0f, 1.0f, uni ? std::max( pos, pos + d ) : pos + std::abs( d ) );
        const float        now = juce::jlimit( 0.0f, 1.0f, pos + d * src );
        const float        rad = ring + 3.6f + 2.4f * static_cast<float>( k );
        juce::Path         span;
        span.addCentredArc( c.x, c.y, rad, rad, 0.0f, startAngle + lo * ( endAngle - startAngle ),
                            startAngle + std::max( hi, lo + 0.002f ) * ( endAngle - startAngle ), true );
        g.setColour( col.withAlpha( slider.isEnabled() ? 0.45f : 0.2f ) );
        g.strokePath( span, juce::PathStrokeType( 1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded ) );
        const float an = startAngle + now * ( endAngle - startAngle );
        g.setColour( col );
        g.fillEllipse( c.x + std::sin( an ) * rad - 2.0f, c.y - std::cos( an ) * rad - 2.0f, 4.0f, 4.0f );
    }
    // The cap, lit a little from above, with a hairline rim.
    const float cap = ring - track - std::max( 2.0f, size * 0.05f );
    g.setGradientFill( juce::ColourGradient( s.m_raised.brighter( 0.25f ), c.x, c.y - cap, s.m_raised.darker( 0.35f ),
                                             c.x, c.y + cap, false ) );
    g.fillEllipse( c.x - cap, c.y - cap, 2.0f * cap, 2.0f * cap );
    g.setColour( s.m_edge.brighter( 0.2f ) );
    g.drawEllipse( c.x - cap, c.y - cap, 2.0f * cap, 2.0f * cap, 1.0f );
    // The pointer; on a skin that shows the value inside the knob, short, so the number stays readable.
    const float sn = std::sin( angle ), co = -std::cos( angle );
    const bool  inside = s.m_valueInKnob && slider.getTextBoxPosition() == juce::Slider::NoTextBox;
    g.setColour( s.m_ink );
    if ( inside )
    {
        g.drawLine( c.x + sn * cap * 0.72f, c.y + co * cap * 0.72f, c.x + sn * cap * 0.98f, c.y + co * cap * 0.98f,
                    std::max( 1.5f, size * 0.035f ) );
        const juce::String v = slider.getTextFromValue( slider.getValue() ).trim();
        g.setColour( s.m_ink.withAlpha( slider.isEnabled() ? 0.92f : 0.4f ) );
        g.setFont( font( juce::jlimit( 8.5f, 15.0f, size * 0.18f ) ) );
        g.drawFittedText(
            v, juce::Rectangle<float>( c.x - cap * 0.82f, c.y - cap * 0.4f, cap * 1.64f, cap * 0.8f ).toNearestInt(),
            juce::Justification::centred, 1, 0.45f );
    }
    else
    {
        g.drawLine( c.x + sn * cap * 0.25f, c.y + co * cap * 0.25f, c.x + sn * cap * 0.85f, c.y + co * cap * 0.85f,
                    std::max( 1.5f, size * 0.035f ) );
    }
}

void LookAndFeel::drawLinearSlider( juce::Graphics &g, int x, int y, int width, int height, float pos, float, float,
                                    juce::Slider::SliderStyle style, juce::Slider &slider )
{
    const Skin     &s = m_skin;
    const juce::var live = slider.getProperties()["live"];
    if ( style == juce::Slider::LinearVertical )
    {
        // A fader: its slot, the level up to the thumb, a cap across it; the live value as a bright tick beside it.
        const float cx = static_cast<float>( x ) + static_cast<float>( width ) * 0.5f;
        const float top = static_cast<float>( y ), bottom = static_cast<float>( y + height );
        g.setColour( slider.findColour( juce::Slider::backgroundColourId ) );
        g.fillRoundedRectangle( cx - 2.5f, top, 5.0f, bottom - top, 2.5f );
        g.setColour( slider.findColour( juce::Slider::trackColourId ) );
        g.fillRoundedRectangle( cx - 2.5f, pos, 5.0f, bottom - pos, 2.5f );
        if ( !live.isVoid() )
        {
            const float lp = juce::jlimit( 0.0f, 1.0f, static_cast<float>( static_cast<double>( live ) ) );
            const float ly = bottom - lp * ( bottom - top );
            if ( std::abs( ly - pos ) > 1.5f )
            {
                g.setColour( slider.findColour( juce::Slider::trackColourId ).brighter( 0.7f ) );
                g.fillRect( cx + 5.0f, ly - 1.0f, 6.0f, 2.0f );
            }
        }
        const float tw = std::min( static_cast<float>( width ) - 4.0f, 26.0f );
        g.setColour( s.m_raised.brighter( 0.3f ) );
        g.fillRoundedRectangle( cx - tw * 0.5f, pos - 5.0f, tw, 10.0f, 2.5f );
        g.setColour( slider.findColour( juce::Slider::thumbColourId ) );
        g.fillRect( cx - tw * 0.5f + 3.0f, pos - 0.75f, tw - 6.0f, 1.5f );
        return;
    }
    if ( style != juce::Slider::LinearHorizontal )
    {
        LookAndFeel_V4::drawLinearSlider( g, x, y, width, height, pos, 0.0f, 0.0f, style, slider );
        return;
    }
    const float cy = static_cast<float>( y ) + static_cast<float>( height ) * 0.5f;
    g.setColour( s.m_faint );
    g.fillRoundedRectangle( static_cast<float>( x ), cy - 2.0f, static_cast<float>( width ), 4.0f, 2.0f );
    g.setColour( slider.findColour( juce::Slider::trackColourId ) );
    // A slider that spans zero (a matrix slot's amount, the master filter) fills from its middle.
    float from = static_cast<float>( x );
    if ( slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0 )
        from = static_cast<float>( x ) +
               static_cast<float>( slider.valueToProportionOfLength( 0.0 ) ) * static_cast<float>( width );
    g.fillRoundedRectangle( std::min( from, pos ), cy - 2.0f, std::fabs( pos - from ), 4.0f, 2.0f );
    if ( !live.isVoid() )
    {
        const float lp = juce::jlimit( 0.0f, 1.0f, static_cast<float>( static_cast<double>( live ) ) );
        const float lx = static_cast<float>( x ) + lp * static_cast<float>( width );
        if ( std::abs( lx - pos ) > 1.5f )
        {
            g.setColour( slider.findColour( juce::Slider::trackColourId ).brighter( 0.7f ) );
            g.fillRect( lx - 1.0f, cy - 8.0f, 2.0f, 5.0f );
        }
    }
    g.setColour( slider.findColour( juce::Slider::thumbColourId ) );
    g.fillEllipse( pos - 6.0f, cy - 6.0f, 12.0f, 12.0f );
}

void LookAndFeel::drawToggleButton( juce::Graphics &g, juce::ToggleButton &b, bool highlighted, bool )
{
    // A small switch with its lamp: lit in the button's colour when on.
    const Skin                  &s = m_skin;
    const auto                   r = b.getLocalBounds().toFloat();
    const float                  h = std::min( 16.0f, r.getHeight() - 4.0f );
    const juce::Rectangle<float> box( r.getX() + 2.0f, r.getCentreY() - h * 0.5f, h * 1.7f, h );
    const bool                   on = b.getToggleState();
    const juce::Colour           colour = b.findColour( juce::ToggleButton::tickColourId );
    g.setColour( on ? colour.withAlpha( 0.35f ) : s.m_raised );
    g.fillRoundedRectangle( box, h * 0.5f );
    g.setColour( highlighted ? s.m_edge.brighter( 0.4f ) : s.m_edge );
    g.drawRoundedRectangle( box, h * 0.5f, 1.0f );
    const float d = h - 6.0f;
    g.setColour( on ? colour : s.m_dim );
    g.fillEllipse( on ? box.getRight() - d - 3.0f : box.getX() + 3.0f, box.getY() + 3.0f, d, d );
    if ( b.getButtonText().isNotEmpty() )
    {
        g.setColour( on ? s.m_ink : s.m_dim );
        g.setFont( font( 13.0f ) );
        g.drawFittedText( b.getButtonText(),
                          box.getRight() + 6.0f > r.getRight() ? r.toNearestInt()
                                                               : r.withLeft( box.getRight() + 6.0f ).toNearestInt(),
                          juce::Justification::centredLeft, 1 );
    }
}

void LookAndFeel::drawComboBox( juce::Graphics &g, int width, int height, bool, int, int, int, int,
                                juce::ComboBox &box )
{
    const Skin &s = m_skin;
    const auto  r =
        juce::Rectangle<float>( 0.0f, 0.0f, static_cast<float>( width ), static_cast<float>( height ) ).reduced( 0.5f );
    g.setColour( box.findColour( juce::ComboBox::backgroundColourId ) );
    g.fillRoundedRectangle( r, s.m_radius );
    g.setColour( box.isMouseOver( true ) ? s.m_edge.brighter( 0.4f )
                                         : box.findColour( juce::ComboBox::outlineColourId ) );
    g.drawRoundedRectangle( r, s.m_radius, 1.0f );
    const float ax = static_cast<float>( width ) - 14.0f, ay = static_cast<float>( height ) * 0.5f;
    juce::Path  arrow;
    arrow.addTriangle( ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f );
    g.setColour( box.findColour( juce::ComboBox::arrowColourId ) );
    g.fillPath( arrow );
}

void LookAndFeel::drawButtonBackground( juce::Graphics &g, juce::Button &b, const juce::Colour &background,
                                        bool highlighted, bool down )
{
    const Skin  &s = m_skin;
    const auto   r = b.getLocalBounds().toFloat().reduced( 0.5f );
    juce::Colour fill = b.getToggleState() ? b.findColour( juce::TextButton::buttonOnColourId ) : background;
    if ( down )
        fill = fill.brighter( 0.15f );
    else if ( highlighted )
        fill = fill.brighter( 0.08f );
    juce::Path p;
    p.addRoundedRectangle(
        r.getX(), r.getY(), r.getWidth(), r.getHeight(), s.m_radius, s.m_radius,
        !b.isConnectedOnLeft() && !b.isConnectedOnTop(), !b.isConnectedOnRight() && !b.isConnectedOnTop(),
        !b.isConnectedOnLeft() && !b.isConnectedOnBottom(), !b.isConnectedOnRight() && !b.isConnectedOnBottom() );
    g.setColour( fill );
    g.fillPath( p );
    g.setColour( highlighted ? s.m_edge.brighter( 0.45f ) : s.m_edge );
    g.strokePath( p, juce::PathStrokeType( 1.0f ) );
}

void LookAndFeel::drawTabButton( juce::TabBarButton &b, juce::Graphics &g, bool isMouseOver, bool )
{
    // Flat tabs: the name, the page in front lit in the accent and underlined.
    const Skin &s = m_skin;
    const auto  r = b.getLocalBounds().toFloat();
    const bool  front = b.isFrontTab();
    if ( front )
    {
        g.setColour( s.m_panel );
        g.fillRect( r );
        g.setColour( s.m_accent );
        g.fillRect( r.getX() + 6.0f, r.getBottom() - 2.5f, r.getWidth() - 12.0f, 2.5f );
    }
    else if ( isMouseOver )
    {
        g.setColour( s.m_group.withAlpha( 0.8f ) );
        g.fillRect( r );
    }
    g.setColour( front ? s.m_accent : ( isMouseOver ? s.m_ink : s.m_dim ) );
    g.setFont( getTabButtonFont( b, r.getHeight() ) );
    g.drawFittedText( b.getButtonText(), b.getLocalBounds().reduced( 4, 0 ), juce::Justification::centred, 1 );
}

void LookAndFeel::drawTabbedButtonBarBackground( juce::TabbedButtonBar &bar, juce::Graphics &g )
{
    g.setColour( m_skin.m_edge );
    g.fillRect( 0, bar.getHeight() - 1, bar.getWidth(), 1 );
}

void LookAndFeel::drawTabAreaBehindFrontButton( juce::TabbedButtonBar &, juce::Graphics &, int, int ) {}

int LookAndFeel::getTabButtonBestWidth( juce::TabBarButton &b, int tabDepth )
{
    return juce::GlyphArrangement::getStringWidthInt( getTabButtonFont( b, static_cast<float>( tabDepth ) ),
                                                      b.getButtonText() ) +
           22;
}

juce::Font LookAndFeel::getTabButtonFont( juce::TabBarButton &, float )
{
    return font( 14.0f );
}
juce::Font LookAndFeel::getComboBoxFont( juce::ComboBox & )
{
    return font( 13.5f );
}
juce::Font LookAndFeel::getTextButtonFont( juce::TextButton &, int )
{
    return font( 13.5f );
}
juce::Font LookAndFeel::getPopupMenuFont()
{
    return font( 14.0f );
}

juce::Label *LookAndFeel::createSliderTextBox( juce::Slider &slider )
{
    auto *l = LookAndFeel_V4::createSliderTextBox( slider );
    l->setFont( font( 12.0f ) );
    l->setColour( juce::Label::outlineColourId, juce::Colours::transparentBlack );
    l->setColour( juce::Label::backgroundColourId, juce::Colours::transparentBlack );
    l->setColour( juce::Label::textColourId, m_skin.m_dim );
    return l;
}

void LookAndFeel::drawPopupMenuBackground( juce::Graphics &g, int width, int height )
{
    g.fillAll( m_skin.m_group );
    g.setColour( m_skin.m_edge );
    g.drawRect( 0, 0, width, height, 1 );
}

void LookAndFeel::drawTooltip( juce::Graphics &g, const juce::String &text, int width, int height )
{
    g.fillAll( m_skin.m_group );
    g.setColour( m_skin.m_edge );
    g.drawRect( 0, 0, width, height, 1 );
    juce::AttributedString a;
    a.setJustification( juce::Justification::centredLeft );
    a.append( text, font( 13.0f ), m_skin.m_ink );
    juce::TextLayout tl;
    tl.createLayoutWithBalancedLineLengths( a, static_cast<float>( width ) - 12.0f );
    tl.draw( g, juce::Rectangle<float>( 6.0f, 4.0f, static_cast<float>( width ) - 12.0f,
                                        static_cast<float>( height ) - 8.0f ) );
}

namespace
{
/**
 * @brief The skin's title font at @p height, with its tracking (the logo's lettering).
 * @param[in] skin    the skin
 * @param[in] height  the font's height in pixels
 * @return the title font
 */
juce::Font titleFont( const Skin &skin, float height )
{
    const int  style = skin.m_titleBold ? juce::Font::bold : juce::Font::plain;
    juce::Font f = skin.m_typeface.isNotEmpty() ? juce::Font( juce::FontOptions( skin.m_typeface, height, style ) )
                                                : font( height, skin.m_titleBold );
    f.setExtraKerningFactor( skin.m_tracking );
    return f;
}
}   // namespace

float titleWidth( const Skin &skin, float height )
{
    return juce::GlyphArrangement::getStringWidth( titleFont( skin, height ), skin.m_name.toUpperCase() ) + 4.0f;
}

void drawTitle( juce::Graphics &g, const Skin &skin, juce::Rectangle<float> r, float height )
{
    const juce::String     t = skin.m_name.toUpperCase();
    const juce::Font       f = titleFont( skin, height );
    juce::GlyphArrangement ga;
    ga.addLineOfText( f, t, r.getX(), r.getCentreY() + height * 0.36f );
    juce::Path p;
    ga.createPath( p );
    if ( skin.m_glow )
    {
        g.setColour( skin.m_accent.withAlpha( 0.22f ) );
        g.strokePath( p, juce::PathStrokeType( height * 0.18f ) );
    }
    g.setColour( skin.m_accent );
    g.fillPath( p );
}

// ======================================================================================================== flat tabs

void FlatTab::paintButton( juce::Graphics &g, bool highlighted, bool )
{
    auto *lf = dynamic_cast<LookAndFeel *>( &getLookAndFeel() );
    if ( lf == nullptr ) return;
    const Skin &s = lf->skin();
    const auto  r = getLocalBounds().toFloat();
    const bool  front = getToggleState();
    if ( front )
    {
        g.setColour( s.m_panel );
        g.fillRect( r );
        g.setColour( s.m_accent );
        g.fillRect( r.getX() + 6.0f, r.getBottom() - 2.5f, r.getWidth() - 12.0f, 2.5f );
    }
    else if ( highlighted )
    {
        g.setColour( s.m_group.withAlpha( 0.8f ) );
        g.fillRect( r );
    }
    g.setColour( front ? s.m_accent : ( highlighted ? s.m_ink : s.m_dim ) );
    g.setFont( font( 14.0f ) );
    g.drawFittedText( getButtonText(), getLocalBounds().reduced( 4, 0 ), juce::Justification::centred, 1 );
}

int FlatTab::bestWidth() const
{
    return juce::GlyphArrangement::getStringWidthInt( font( 14.0f ), getButtonText() ) + 22;
}

// ============================================================================================================ icons

IconButton::IconButton( IconE icon, const juce::String &tooltip ) : juce::Button( tooltip ), m_icon( icon )
{
    setTooltip( tooltip );
    setWantsKeyboardFocus( false );
}

void IconButton::paintButton( juce::Graphics &g, bool highlighted, bool down )
{
    auto &lf = getLookAndFeel();
    lf.drawButtonBackground(
        g, *this,
        findColour( getToggleState() ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId ),
        highlighted && isEnabled(), down );
    const auto  b = getLocalBounds().toFloat();
    const float s =
        std::min( std::min( b.getWidth(), b.getHeight() ) * 0.56f, 20.0f );   // a page's tall button: still a glyph
    const auto         r = b.withSizeKeepingCentre( s, s );
    const juce::Colour ink =
        findColour( juce::TextButton::textColourOffId ).withMultipliedAlpha( isEnabled() ? 1.0f : 0.35f );
    g.setColour( ink );
    const float                w = std::max( 1.4f, s * 0.11f );
    const juce::PathStrokeType stroke( w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded );
    juce::Path                 p;
    switch ( m_icon )
    {
    case IconE::UNDO:
    case IconE::REDO:
    {
        // A curved arrow turning back (undo) or on (redo).
        const bool  back = m_icon == IconE::UNDO;
        const float cx = r.getCentreX(), cy = r.getCentreY() + s * 0.1f, rad = s * 0.36f;
        p.addCentredArc( cx, cy, rad, rad, 0.0f,
                         back ? -juce::MathConstants<float>::halfPi * 1.6f : juce::MathConstants<float>::halfPi * 1.6f,
                         back ? juce::MathConstants<float>::halfPi * 0.9f : -juce::MathConstants<float>::halfPi * 0.9f,
                         true );
        g.strokePath( p, stroke );
        const float ax = cx + ( back ? -1.0f : 1.0f ) * rad * std::sin( juce::MathConstants<float>::halfPi * 1.6f );
        const float ay = cy - rad * std::cos( juce::MathConstants<float>::halfPi * 1.6f );
        juce::Path  head;
        const float k = s * 0.22f, dir = back ? -1.0f : 1.0f;
        head.addTriangle( ax - dir * k * 0.2f, ay - k, ax + dir * k * 0.9f, ay - k * 0.1f, ax - dir * k * 0.5f,
                          ay + k * 0.7f );
        g.fillPath( head );
        break;
    }
    case IconE::HELP:
    {
        g.setFont( font( s * 1.25f, true ) );
        g.drawText( "?", b, juce::Justification::centred );
        break;
    }
    case IconE::SETTINGS:
    {
        // A cog: a ring with eight teeth and a hole.
        const auto  c = r.getCentre();
        const float ro = s * 0.5f, ri = s * 0.36f;
        for ( int i = 0; i < 16; ++i )
        {
            const float a0 = juce::MathConstants<float>::twoPi * ( static_cast<float>( i ) - 0.5f ) / 16.0f;
            const float a1 = juce::MathConstants<float>::twoPi * ( static_cast<float>( i ) + 0.5f ) / 16.0f;
            const float rr = ( i % 2 == 0 ) ? ro : ri;
            if ( i == 0 )
                p.startNewSubPath( c.x + rr * std::sin( a0 ), c.y - rr * std::cos( a0 ) );
            else
                p.lineTo( c.x + rr * std::sin( a0 ), c.y - rr * std::cos( a0 ) );
            p.lineTo( c.x + rr * std::sin( a1 ), c.y - rr * std::cos( a1 ) );
        }
        p.closeSubPath();
        p.addEllipse( c.x - s * 0.15f, c.y - s * 0.15f, s * 0.3f, s * 0.3f );
        p.setUsingNonZeroWinding( false );
        g.fillPath( p );
        break;
    }
    case IconE::THUMB_UP:
    case IconE::THUMB_DOWN:
    {
        // A thumb: the cuff and the hand with the thumb raised; down is the same turned over.
        juce::Path t;
        t.addRoundedRectangle( 0.0f, 0.42f, 0.22f, 0.58f, 0.04f );
        t.startNewSubPath( 0.3f, 0.45f );
        t.lineTo( 0.48f, 0.08f );
        t.quadraticTo( 0.62f, 0.0f, 0.62f, 0.18f );
        t.lineTo( 0.56f, 0.38f );
        t.lineTo( 0.9f, 0.38f );
        t.quadraticTo( 1.0f, 0.4f, 0.98f, 0.52f );
        t.lineTo( 0.9f, 0.92f );
        t.quadraticTo( 0.86f, 1.0f, 0.78f, 1.0f );
        t.lineTo( 0.3f, 1.0f );
        t.closeSubPath();
        juce::AffineTransform at = juce::AffineTransform::scale( s, s );
        if ( m_icon == IconE::THUMB_DOWN ) at = juce::AffineTransform::verticalFlip( 1.0f ).scaled( s, s );
        t.applyTransform( at.translated( r.getX(), r.getY() ) );
        g.fillPath( t );
        break;
    }
    case IconE::HEADSET:
    {
        // A visor: a rounded band with two lenses.
        juce::Path v;
        v.addRoundedRectangle( r.getX(), r.getCentreY() - s * 0.25f, s, s * 0.5f, s * 0.18f );
        g.strokePath( v, stroke );
        g.fillEllipse( r.getX() + s * 0.18f, r.getCentreY() - s * 0.08f, s * 0.2f, s * 0.16f );
        g.fillEllipse( r.getRight() - s * 0.38f, r.getCentreY() - s * 0.08f, s * 0.2f, s * 0.16f );
        break;
    }
    default: paintGlyph( g, r, ink, stroke ); break;
    }
}

void IconButton::paintGlyph( juce::Graphics &g, juce::Rectangle<float> r, juce::Colour ink,
                             const juce::PathStrokeType &stroke ) const
{
    // The glyphs of 03.10.2026, drawn in a unit square and placed on r.
    const float                 s = r.getWidth();
    const juce::AffineTransform at = juce::AffineTransform::scale( s, s ).translated( r.getX(), r.getY() );
    auto                        put = [&at]( juce::Path p )
    {
        p.applyTransform( at );
        return p;
    };
    juce::Path p;
    switch ( m_icon )
    {
    case IconE::PLAY:
        p.addTriangle( 0.2f, 0.06f, 0.2f, 0.94f, 0.94f, 0.5f );
        g.fillPath( put( p ) );
        break;
    case IconE::STOP:
        p.addRoundedRectangle( 0.17f, 0.17f, 0.66f, 0.66f, 0.06f );
        g.fillPath( put( p ) );
        break;
    case IconE::RECORD:
        // Red while it waits; lit (the button's on colour behind it) the disc takes the ink.
        g.setColour( getToggleState() ? ink
                                      : juce::Colour( 0xffe5484d ).withMultipliedAlpha( isEnabled() ? 1.0f : 0.35f ) );
        p.addEllipse( 0.1f, 0.1f, 0.8f, 0.8f );
        g.fillPath( put( p ) );
        break;
    case IconE::SPEAKER:
    case IconE::SPEAKER_OFF:
    {
        // The box and the cone; sounding, two waves; muted, a cross.
        p.startNewSubPath( 0.04f, 0.36f );
        p.lineTo( 0.26f, 0.36f );
        p.lineTo( 0.52f, 0.12f );
        p.lineTo( 0.52f, 0.88f );
        p.lineTo( 0.26f, 0.64f );
        p.lineTo( 0.04f, 0.64f );
        p.closeSubPath();
        g.fillPath( put( p ) );
        juce::Path w;
        if ( m_icon == IconE::SPEAKER )
        {
            for ( float rad : { 0.18f, 0.34f } )
                w.addCentredArc( 0.54f, 0.5f, rad, rad, 0.0f, juce::MathConstants<float>::pi * 0.22f,
                                 juce::MathConstants<float>::pi * 0.78f, true );
        }
        else
        {
            w.startNewSubPath( 0.66f, 0.34f );
            w.lineTo( 0.96f, 0.66f );
            w.startNewSubPath( 0.66f, 0.66f );
            w.lineTo( 0.96f, 0.34f );
        }
        g.strokePath( put( w ), stroke );
        break;
    }
    case IconE::DICE:
    {
        // A die showing five.
        p.addRoundedRectangle( 0.06f, 0.06f, 0.88f, 0.88f, 0.2f );
        g.strokePath( put( p ), stroke );
        juce::Path pips;
        for ( const auto &c : { juce::Point<float>( 0.31f, 0.31f ),
                                { 0.69f, 0.31f },
                                { 0.5f, 0.5f },
                                { 0.31f, 0.69f },
                                { 0.69f, 0.69f } } )
            pips.addEllipse( c.x - 0.085f, c.y - 0.085f, 0.17f, 0.17f );
        g.fillPath( put( pips ) );
        break;
    }
    case IconE::SAVE:
    {
        // A disk: the case with its corner cut, the shutter, the label.
        p.startNewSubPath( 0.08f, 0.08f );
        p.lineTo( 0.72f, 0.08f );
        p.lineTo( 0.92f, 0.28f );
        p.lineTo( 0.92f, 0.92f );
        p.lineTo( 0.08f, 0.92f );
        p.closeSubPath();
        p.addRectangle( 0.26f, 0.58f, 0.48f, 0.34f );
        g.strokePath( put( p ), stroke );
        juce::Path shutter;
        shutter.addRectangle( 0.28f, 0.08f, 0.34f, 0.24f );
        g.fillPath( put( shutter ) );
        break;
    }
    case IconE::OPEN:
        // A folder, its flap open.
        p.startNewSubPath( 0.04f, 0.18f );
        p.lineTo( 0.36f, 0.18f );
        p.lineTo( 0.46f, 0.3f );
        p.lineTo( 0.92f, 0.3f );
        p.lineTo( 0.92f, 0.86f );
        p.lineTo( 0.04f, 0.86f );
        p.closeSubPath();
        p.startNewSubPath( 0.04f, 0.44f );
        p.lineTo( 0.92f, 0.44f );
        g.strokePath( put( p ), stroke );
        break;
    case IconE::MIDI_LEARN:
    {
        // A MIDI socket: the ring, five pins on an arc, the notch at the bottom.
        p.addEllipse( 0.04f, 0.04f, 0.92f, 0.92f );
        g.strokePath( put( p ), stroke );
        juce::Path pins;
        for ( int i = 0; i < 5; ++i )
        {
            const float a = juce::MathConstants<float>::pi * ( static_cast<float>( i ) - 2.0f ) * 0.25f;
            const float x = 0.5f + 0.26f * std::sin( a ), y = 0.54f - 0.26f * std::cos( a );
            pins.addEllipse( x - 0.065f, y - 0.065f, 0.13f, 0.13f );
        }
        pins.addRectangle( 0.42f, 0.8f, 0.16f, 0.12f );
        g.fillPath( put( pins ) );
        break;
    }
    case IconE::SEARCH:
        // A magnifier: the lens and its handle.
        p.addEllipse( 0.08f, 0.08f, 0.56f, 0.56f );
        p.startNewSubPath( 0.6f, 0.6f );
        p.lineTo( 0.92f, 0.92f );
        g.strokePath( put( p ), stroke );
        break;
    case IconE::NIGHT:
    {
        // A crescent: a disc with another taken out of its upper right.
        juce::Path outside;
        outside.addRectangle( -1.0f, -1.0f, 3.0f, 3.0f );
        outside.addEllipse( 0.34f, -0.08f, 0.72f, 0.72f );
        outside.setUsingNonZeroWinding( false );
        g.saveState();
        g.reduceClipRegion( put( outside ) );
        p.addEllipse( 0.06f, 0.06f, 0.88f, 0.88f );
        g.fillPath( put( p ) );
        g.restoreState();
        break;
    }
    case IconE::COMPARE:
    {
        // Before | after: a frame split down the middle, its left half filled -- no letters, so it is not taken for an
        // instrument's own A and B (Noctuary's morph slots).
        p.addRoundedRectangle( 0.06f, 0.2f, 0.88f, 0.6f, 0.08f );
        p.startNewSubPath( 0.5f, 0.2f );
        p.lineTo( 0.5f, 0.8f );
        g.strokePath( put( p ), stroke );
        juce::Path half;
        half.addRectangle( 0.06f, 0.2f, 0.44f, 0.6f );
        g.fillPath( put( half ) );
        break;
    }
    default: break;
    }
}

// =========================================================================================================== header

void layoutHeader( juce::Rectangle<int> &area, const Header &h )
{
    // The first row: who, what, how long -- then make, play, silence. What does not fit squeezes the length slider.
    auto top = area.removeFromTop( HeaderRow );
    if ( h.m_mute != nullptr )
        h.m_mute->setBounds( top.removeFromRight( IconWidth ).reduced( 3 ) );   // icons since 03.10.2026
    if ( h.m_play != nullptr ) h.m_play->setBounds( top.removeFromRight( IconWidth ).reduced( 3 ) );
    for ( auto it = h.m_actions.rbegin(); it != h.m_actions.rend(); ++it )
        it->first->setBounds( top.removeFromRight( it->second > 0 ? it->second : 96 ).reduced( 3 ) );
    top.removeFromRight( 6 );
    if ( h.m_logo != nullptr ) *h.m_logo = top.removeFromLeft( 34 ).toFloat().reduced( 2.0f );
    if ( h.m_title != nullptr ) h.m_title->setBounds( top.removeFromLeft( h.m_titleWidth > 0 ? h.m_titleWidth : 110 ) );
    for ( const auto &[c, w] : h.m_choices ) c->setBounds( top.removeFromLeft( w > 0 ? w : 110 ).reduced( 3 ) );
    top.removeFromLeft( 8 );
    for ( const auto &[c, w] : h.m_modes ) c->setBounds( top.removeFromLeft( w > 0 ? w : 70 ).reduced( 0, 3 ) );
    top.removeFromLeft( 4 );
    if ( h.m_lengthLabel != nullptr ) h.m_lengthLabel->setBounds( top.removeFromLeft( top.getWidth() > 200 ? 50 : 0 ) );
    if ( h.m_length != nullptr ) h.m_length->setBounds( top.withWidth( std::min( top.getWidth(), 230 ) ).reduced( 2 ) );
    area.removeFromTop( 4 );
    // The second: the ratings, what plays and what was rerolled; the tools at its right end.
    auto row = area.removeFromTop( StatusRow );
    for ( auto it = h.m_tools.rbegin(); it != h.m_tools.rend(); ++it )
    {
        if ( *it == nullptr )
            row.removeFromRight( 8 );
        else if ( ( *it )->isVisible() )
            ( *it )->setBounds( row.removeFromRight( 30 ).reduced( 2, 0 ) );
    }
    row.removeFromRight( 8 );
    if ( h.m_update != nullptr ) h.m_update->setBounds( row.removeFromRight( h.m_update->isVisible() ? 180 : 0 ) );
    if ( h.m_like != nullptr ) h.m_like->setBounds( row.removeFromLeft( 28 ).reduced( 1, 0 ) );
    if ( h.m_dislike != nullptr ) h.m_dislike->setBounds( row.removeFromLeft( 28 ).reduced( 1, 0 ) );
    row.removeFromLeft( 6 );
    if ( h.m_status != nullptr && h.m_curation != nullptr )
    {
        h.m_status->setBounds( row.removeFromLeft( row.getWidth() * 3 / 5 ) );
        h.m_curation->setBounds( row );
    }
    else if ( h.m_status != nullptr )
    {
        h.m_status->setBounds( row );
    }
    area.removeFromTop( 4 );
}

void connectModes( std::initializer_list<juce::Button *> modes, juce::Colour on )
{
    const int n = static_cast<int>( modes.size() );
    int       i = 0;
    for ( juce::Button *b : modes )
    {
        int edges = 0;
        if ( i > 0 ) edges |= juce::Button::ConnectedOnLeft;
        if ( i < n - 1 ) edges |= juce::Button::ConnectedOnRight;
        b->setConnectedEdges( edges );
        b->setColour( juce::TextButton::buttonOnColourId, on );
        ++i;
    }
}

// ========================================================================================================= backdrop

void Backdrop::paint( juce::Graphics &g, juce::Rectangle<int> area, int headerBottom, const Skin &skin, bool on )
{
    g.setColour( skin.m_bg );
    g.fillRect( area );
    const juce::Image picture = skin.backdrop();
    if ( !on || !picture.isValid() || area.isEmpty() ) return;
    if ( m_cache.isNull() || m_cachedFor != area || m_cachedHeader != headerBottom || m_cachedOn != on )
    {
        // Rendered once per size: the picture covering the window (cropped, centred), strong behind the header and fading
        // to the page strength below it, under a vignette that keeps the edges and the corners quiet.
        m_cache = juce::Image( juce::Image::RGB, area.getWidth(), area.getHeight(), true );
        juce::Graphics c( m_cache );
        c.fillAll( skin.m_bg );
        const auto                &src = picture;
        const juce::Rectangle<int> crop(
            juce::roundToInt( skin.m_crop.getX() * static_cast<float>( src.getWidth() ) ),
            juce::roundToInt( skin.m_crop.getY() * static_cast<float>( src.getHeight() ) ),
            juce::roundToInt( skin.m_crop.getWidth() * static_cast<float>( src.getWidth() ) ),
            juce::roundToInt( skin.m_crop.getHeight() * static_cast<float>( src.getHeight() ) ) );
        const juce::Image part = src.getClippedImage( crop );
        const float       sw = static_cast<float>( area.getWidth() ) / static_cast<float>( part.getWidth() );
        const float       sh = static_cast<float>( area.getHeight() ) / static_cast<float>( part.getHeight() );
        const float       k = std::max( sw, sh );
        const float       w = static_cast<float>( part.getWidth() ) * k, h = static_cast<float>( part.getHeight() ) * k;
        const juce::Rectangle<float> dest( ( static_cast<float>( area.getWidth() ) - w ) * 0.5f, 0.0f, w, h );
        c.setImageResamplingQuality( juce::Graphics::highResamplingQuality );
        c.setOpacity( skin.m_backdropTop );
        c.drawImage( part, dest );
        c.setOpacity( 1.0f );
        // Down from the header the picture fades to its page strength.
        const float          hb = static_cast<float>( headerBottom ), H = static_cast<float>( area.getHeight() );
        juce::ColourGradient fade(
            skin.m_bg.withAlpha( 0.0f ), 0.0f, hb * 0.6f,
            skin.m_bg.withAlpha( 1.0f - skin.m_backdropPage / std::max( 0.01f, skin.m_backdropTop ) ), 0.0f, hb + 40.0f,
            false );
        c.setGradientFill( fade );
        c.fillRect( 0.0f, hb * 0.6f, static_cast<float>( area.getWidth() ), H );
        // The header's text stays readable: a soft darkening behind its left and right ends.
        c.setGradientFill( juce::ColourGradient( skin.m_bg.withAlpha( 0.55f ), 0.0f, 0.0f, skin.m_bg.withAlpha( 0.0f ),
                                                 0.0f, hb, false ) );
        c.fillRect( 0.0f, 0.0f, static_cast<float>( area.getWidth() ), hb );
        // The vignette.
        juce::ColourGradient v( skin.m_bg.withAlpha( 0.0f ), static_cast<float>( area.getWidth() ) * 0.5f, H * 0.4f,
                                skin.m_bg.withAlpha( 0.65f ), 0.0f, H, true );
        c.setGradientFill( v );
        c.fillRect( 0.0f, 0.0f, static_cast<float>( area.getWidth() ), H );
        m_cachedFor = area;
        m_cachedHeader = headerBottom;
        m_cachedOn = on;
    }
    g.drawImageAt( m_cache, area.getX(), area.getY() );
}

// ========================================================================================================= settings

namespace
{
/** @brief Every instrument's settings in this process, deleted when JUCE shuts down (not at the DLL's unloading). */
struct SettingsRegistry final : public juce::DeletedAtShutdown
{
    std::map<juce::String, std::unique_ptr<Settings>> m_all;   ///< the settings by instrument name
    /**
     * @brief The registry, created on first use; null after JUCE has deleted it.
     * @return the registry, null after JUCE has deleted it
     */
    static SettingsRegistry *&instance()
    {
        static SettingsRegistry *r = nullptr;
        return r;
    }
    ~SettingsRegistry() override { instance() = nullptr; }
};
}   // namespace

Settings &Settings::of( const juce::String &app )
{
    SettingsRegistry *&r = SettingsRegistry::instance();
    if ( r == nullptr ) r = new SettingsRegistry();
    auto &s = r->m_all[app];
    if ( s == nullptr ) s = std::make_unique<Settings>( app );
    return *s;
}

Settings::Settings( const juce::String &app ) : m_app( app )
{
    juce::PropertiesFile::Options o;
    o.applicationName = app;
    o.filenameSuffix = ".frame";   // beside \<app\>.settings (the standalone's window and audio) and \<app\>.updates
    o.folderName = app;
    o.osxLibrarySubFolder = "Application Support";
    m_file = std::make_unique<juce::PropertiesFile>( o );
}

Settings::HeadsetModeE Settings::headset() const
{
    const juce::String m = m_file->getValue( "headset", "auto" );
    return m == "on" ? HeadsetModeE::ON : m == "off" ? HeadsetModeE::OFF : HeadsetModeE::AUTO;
}

void Settings::setHeadset( HeadsetModeE m )
{
    m_file->setValue( "headset", m == HeadsetModeE::ON ? "on" : m == HeadsetModeE::OFF ? "off" : "auto" );
    m_file->saveIfNeeded();
    sendChangeMessage();
}

int Settings::headsetPort() const
{
    return m_file->getIntValue( "headsetPort", portOf( m_app ) );
}

bool Settings::backdrop() const
{
    return m_file->getBoolValue( "backdrop", true );
}

void Settings::setBackdrop( bool on )
{
    m_file->setValue( "backdrop", on );
    m_file->saveIfNeeded();
    sendChangeMessage();
}

bool Settings::overview() const
{
    return m_file->getBoolValue( "overview", true );
}

void Settings::setOverview( bool on )
{
    m_file->setValue( "overview", on );
    m_file->saveIfNeeded();
    sendChangeMessage();
}

bool Settings::link() const
{
    return m_file->getBoolValue( "link", false );
}

void Settings::setLink( bool on )
{
    m_file->setValue( "link", on );
    m_file->saveIfNeeded();
    sendChangeMessage();
}

// FAMILY_CUES=\<port\> switches the cues on for one process, to that port (a test aid, as FAMILY_LINK is Link's).
bool Settings::cues() const
{
    return std::getenv( "FAMILY_CUES" ) != nullptr || m_file->getBoolValue( "cues", false );
}

void Settings::setCues( bool on )
{
    m_file->setValue( "cues", on );
    m_file->saveIfNeeded();
    sendChangeMessage();
}

int Settings::cuePort() const
{
    if ( const char *p = std::getenv( "FAMILY_CUES" ) ) return juce::jlimit( 1024, 65535, std::atoi( p ) );
    return juce::jlimit( 1024, 65535, m_file->getIntValue( "cuePort", 9000 ) );
}

Settings::JamRoleE Settings::jamRole() const
{
    // FAMILY_JAM=lead|follow sets the role for one process (a test aid, as FAMILY_LINK is Link's).
    if ( const char *e = std::getenv( "FAMILY_JAM" ) )
        return std::strcmp( e, "lead" ) == 0 ? JamRoleE::LEAD : JamRoleE::FOLLOW;
    const int r = m_file->getIntValue( "jam", 0 );
    return r == 1 ? JamRoleE::LEAD : r == 2 ? JamRoleE::FOLLOW : JamRoleE::OFF;
}

void Settings::setJamRole( JamRoleE r )
{
    m_file->setValue( "jam", r == JamRoleE::LEAD ? 1 : r == JamRoleE::FOLLOW ? 2 : 0 );
    m_file->saveIfNeeded();
    sendChangeMessage();
}

juce::String Settings::value( const juce::String &key, const juce::String &fallback ) const
{
    return m_file->getValue( key, fallback );
}

void Settings::setValue( const juce::String &key, const juce::String &v, bool now )
{
    m_file->setValue( key, v );
    if ( now ) m_file->saveIfNeeded();   // else the file's own timer writes it a few seconds later
}

void SettingsMenu::show( juce::Component &target ) const
{
    Settings       &s = Settings::of( m_app );
    juce::PopupMenu m;
    m.addSectionHeader( m_app + " " + m_version );
    if ( m_updatesOn && m_setUpdates )
        m.addItem( "Check for updates once a day", true, m_updatesOn(),
                   [f = m_setUpdates, on = m_updatesOn()] { f( !on ); } );
    m.addItem( "Picture behind the panel", true, s.backdrop(), [&s] { s.setBackdrop( !s.backdrop() ); } );
    m.addItem( "Overview above the tabs (off: the pages get its height)", true, s.overview(),
               [&s] { s.setOverview( !s.overview() ); } );
    {
        juce::PopupMenu h;
        const auto      mode = s.headset();
        using MT = Settings::HeadsetModeE;
        h.addItem( "Auto: show its controls when a headset sends", true, mode == MT::AUTO,
                   [&s] { s.setHeadset( MT::AUTO ); } );
        h.addItem( "Always show them", true, mode == MT::ON, [&s] { s.setHeadset( MT::ON ); } );
        h.addItem( m_headsetOffText, true, mode == MT::OFF, [&s] { s.setHeadset( MT::OFF ); } );
        if ( m_headsetItems )
        {
            h.addSeparator();
            m_headsetItems( h );
        }
        if ( m_headsetStatus )
        {
            h.addSeparator();
            h.addItem( m_headsetStatus(), false, false, nullptr );
        }
        m.addSubMenu( "Headset (Meta Quest)", h );
    }
    if ( m_linkStatus )   // the standalone only (02.10.2026): a DAW's transport rules the plugin
        m.addItem( "Ableton Link: tempo and beat with other apps (" + m_linkStatus() + ")", true, s.link(),
                   [&s] { s.setLink( !s.link() ); } );
    if ( m_cueStatus )
        m.addItem( "Score cues for a visualiser: OSC to port " + juce::String( s.cuePort() ) + " (" + m_cueStatus() +
                       ")",
                   true, s.cues(), [&s] { s.setCues( !s.cues() ); } );
    if ( m_jamStatus )
    {
        // The family jam (02.10.2026, Jam.h): one instrument leads -- its key, its sections, its breaks and drops go out
        // to the others on the network --, the ones that follow take them up; tempo and bars come from Ableton Link.
        juce::PopupMenu j;
        using RT = Settings::JamRoleE;
        const RT role = s.jamRole();
        j.addItem( "Off", true, role == RT::OFF, [&s] { s.setJamRole( RT::OFF ); } );
        j.addItem( "Lead: the others follow this one's key, sections, breaks and drops", true, role == RT::LEAD,
                   [&s] { s.setJamRole( RT::LEAD ); } );
        j.addItem( "Follow: take up the leader's key, energy, breaks and drops", true, role == RT::FOLLOW,
                   [&s] { s.setJamRole( RT::FOLLOW ); } );
        j.addSeparator();
        j.addItem( m_jamStatus(), false, false, nullptr );
        m.addSubMenu( "Family jam", j );
    }
    if ( m_setWindowScale )
    {
        juce::PopupMenu w;
        for ( int pct : { 75, 100, 125, 150, 200 } )
            w.addItem( juce::String( pct ) + " %",
                       [f = m_setWindowScale, pct] { f( static_cast<float>( pct ) / 100.0f ); } );
        m.addSubMenu( "Window size", w );
    }
    if ( m_canFullScreen && m_canFullScreen() && m_toggleFullScreen )
        m.addItem( "Full screen (F11)", true, m_isFullScreen && m_isFullScreen(), [f = m_toggleFullScreen] { f(); } );
    if ( m_moreItems )
    {
        m.addSeparator();
        m_moreItems( m );
    }
    m.addSeparator();
    m.addItem( "Keys ...",
               [n = m_app]
               {
                   juce::AlertWindow::showMessageBoxAsync( juce::MessageBoxIconType::NoIcon, n + ": keys", keysText(),
                                                           "Close" );
               } );
    m.addItem( "About " + m_app + " ...",
               [n = m_app, v = m_version, more = m_about, own = m_showAbout]
               {
                   if ( own )
                   {
                       own();
                       return;
                   }
                   juce::String t;
                   t << n << " " << v << "\nbuilt " << juce::String( __DATE__ ).trim() << "\n\n"
                     << ( more ? more() : juce::String() );
                   juce::AlertWindow::showMessageBoxAsync( juce::MessageBoxIconType::NoIcon, "About " + n, t, "Close" );
               } );
    m.showMenuAsync( juce::PopupMenu::Options().withTargetComponent( &target ) );
}

// ============================================================================================================= keys

bool handleKey( const juce::KeyPress &key, const Keys &keys )
{
    const auto mods = key.getModifiers();
    const int  code = key.getKeyCode();
    auto       run = []( const std::function<void()> &f )
    {
        if ( !f ) return false;
        f();
        return true;
    };
    if ( code == juce::KeyPress::spaceKey && !mods.isAnyModifierKeyDown() ) return run( keys.m_playStop );
    if ( code == juce::KeyPress::F1Key ) return run( keys.m_help );
    if ( code == juce::KeyPress::F11Key ) return run( keys.m_fullScreen );
    if ( code == juce::KeyPress::escapeKey ) return run( keys.m_escape );
    if ( mods.isCommandDown() )
    {
        const juce::juce_wchar c = juce::CharacterFunctions::toLowerCase(
            key.getTextCharacter() != 0 ? key.getTextCharacter() : static_cast<juce::juce_wchar>( code ) );
        if ( c == 'z' && mods.isShiftDown() ) return run( keys.m_redo );
        if ( c == 'z' ) return run( keys.m_undo );
        if ( c == 'y' ) return run( keys.m_redo );
        if ( c == 's' ) return run( keys.m_save );
        if ( c == 'o' ) return run( keys.m_open );
        if ( c == 'e' ) return run( keys.m_exportFile );
        if ( c == 'k' ) return run( keys.m_palette );
    }
    return false;
}

juce::String keysText()
{
    return "Space\tplay / stop\n"
           "Ctrl+Z\tundo\n"
           "Ctrl+Y, Ctrl+Shift+Z\tredo\n"
           "F1\thelp\n"
           "F11\tfull screen (the standalone)\n"
           "Esc\tclose the help, leave full screen\n"
           "Ctrl+S\tsave the set\n"
           "Ctrl+O\topen a set\n"
           "Ctrl+E\texport\n"
           "Ctrl+K\tsearch: any control, preset or action\n\n"
           "On every control: double click its default, right click MIDI learn, forget, default.";
}

void keepKeysForEditor( juce::Component &root )
{
    for ( auto *c : root.getChildren() )
    {
        if ( dynamic_cast<juce::Button *>( c ) != nullptr || dynamic_cast<juce::ComboBox *>( c ) != nullptr ||
             dynamic_cast<juce::Slider *>( c ) != nullptr || dynamic_cast<juce::TabBarButton *>( c ) != nullptr )
        {
            c->setWantsKeyboardFocus( false );
            c->setMouseClickGrabsKeyboardFocus( false );
        }
        keepKeysForEditor( *c );
    }
}

// ============================================================================================================= help

HelpView::HelpView( const juce::String &chapters, const Skin &skin ) : m_skin( skin )
{
    // chapters.txt as (name, text): paragraphs joined, lines indented by four spaces kept as they stand.
    juce::String name, textOf, para;
    auto         flushPara = [&]
    {
        if ( para.isNotEmpty() )
        {
            textOf << para.trimEnd() << "\n\n";
            para.clear();
        }
    };
    auto flushChapter = [&]
    {
        flushPara();
        if ( name.isNotEmpty() ) m_chapters.emplace_back( name, textOf.trimEnd() );
        textOf.clear();
    };
    for ( const juce::String &raw : juce::StringArray::fromLines( chapters ) )
    {
        const juce::String l = raw.trimEnd();
        if ( l.startsWith( "#" ) ) continue;
        if ( l.startsWith( "== " ) && l.endsWith( " ==" ) )
        {
            flushChapter();
            name = l.substring( 3, l.length() - 3 ).upToFirstOccurrenceOf( "|", false, false ).trim();
            continue;
        }
        if ( name.isEmpty() ) continue;
        if ( l.isEmpty() )
        {
            flushPara();
            continue;
        }
        if ( l.startsWith( "    " ) )
        {
            flushPara();
            textOf << l.substring( 4 ) << "\n";
            continue;
        }
        if ( l.startsWith( "[" ) && l.endsWith( "]" ) ) continue;   // a picture of the printed manual
        if ( para.isNotEmpty() ) para << " ";
        para << l.trim();
    }
    flushChapter();
    m_list.setModel( this );
    m_list.setRowHeight( 26 );
    m_list.setColour( juce::ListBox::backgroundColourId, m_skin.m_group );
    addAndMakeVisible( m_list );
    m_text.setMultiLine( true, true );
    m_text.setReadOnly( true );
    m_text.setScrollbarsShown( true );
    m_text.setCaretVisible( false );
    m_text.setFont( font( 15.0f ) );
    m_text.setColour( juce::TextEditor::backgroundColourId, m_skin.m_group );
    m_text.setColour( juce::TextEditor::outlineColourId, juce::Colours::transparentBlack );
    m_text.setIndents( 14, 12 );
    addAndMakeVisible( m_text );
    m_close.onClick = [this]
    {
        if ( m_onClose ) m_onClose();
    };
    addAndMakeVisible( m_close );
    setWantsKeyboardFocus( true );
}

void HelpView::refresh()
{
    const juce::String was = m_names[m_list.getSelectedRow()];
    m_names.clear();
    m_texts.clear();
    if ( m_extraTopics )
        for ( const auto &[n, t] : m_extraTopics() )
        {
            m_names.add( n );
            m_texts.add( t );
        }
    for ( const auto &[n, t] : m_chapters )
    {
        m_names.add( n );
        m_texts.add( t );
    }
    m_list.updateContent();
    const int keep = m_names.indexOf( was );
    showTopic( keep >= 0 ? keep : 0 );
}

void HelpView::showTopic( int index )
{
    if ( index < 0 || index >= m_names.size() ) return;
    m_list.selectRow( index, false, true );
    m_text.setText( m_texts[index], false );
    m_text.setCaretPosition( 0 );
    m_text.scrollEditorToPositionCaret( 0, 0 );
}

void HelpView::selectedRowsChanged( int row )
{
    if ( row >= 0 && row < m_texts.size() )
    {
        m_text.setText( m_texts[row], false );
        m_text.setCaretPosition( 0 );
        m_text.scrollEditorToPositionCaret( 0, 0 );
    }
}

void HelpView::paintListBoxItem( int row, juce::Graphics &g, int width, int height, bool selected )
{
    if ( selected )
    {
        g.setColour( m_skin.m_accent.withAlpha( 0.22f ) );
        g.fillRect( 0, 0, width, height );
        g.setColour( m_skin.m_accent );
        g.fillRect( 0, 0, 3, height );
    }
    g.setColour( selected ? m_skin.m_ink : m_skin.m_dim );
    g.setFont( font( 14.0f ) );
    g.drawText( m_names[row], 12, 0, width - 16, height, juce::Justification::centredLeft, true );
}

void HelpView::resized()
{
    auto r = getLocalBounds().reduced( 10 );
    auto top = r.removeFromTop( 30 );
    m_close.setBounds( top.removeFromRight( 90 ).reduced( 2 ) );
    r.removeFromTop( 6 );
    m_list.setBounds( r.removeFromLeft( std::min( 260, r.getWidth() / 3 ) ) );
    r.removeFromLeft( 10 );
    m_text.setBounds( r );
}

void HelpView::paint( juce::Graphics &g )
{
    g.fillAll( m_skin.m_bg );
    g.setColour( m_skin.m_accent );
    g.setFont( font( 16.0f, true ) );
    g.drawText( "HELP", getLocalBounds().reduced( 14, 10 ).withHeight( 30 ), juce::Justification::centredLeft );
    g.setColour( m_skin.m_dim );
    g.setFont( font( 12.5f ) );
    g.drawText( "F1 opens it, Esc closes it", getLocalBounds().reduced( 80, 10 ).withHeight( 30 ),
                juce::Justification::centredLeft );
}

bool HelpView::keyPressed( const juce::KeyPress &key )
{
    if ( key == juce::KeyPress::escapeKey || key.getKeyCode() == juce::KeyPress::F1Key )
    {
        if ( m_onClose ) m_onClose();
        return true;
    }
    return false;
}

// ============================================================================================================= undo

void UndoHistory::begin( const juce::String &what, std::vector<float> values, juce::String extra, bool allValues )
{
    if ( m_depth++ > 0 )
    {
        m_all = m_all || allValues;
        return;
    }
    m_what = what;
    m_before = std::move( values );
    m_extra = std::move( extra );
    m_touched.clear();
    m_all = allValues;
}

void UndoHistory::end( const std::vector<float> &values, const juce::String &extra )
{
    if ( m_depth == 0 || --m_depth > 0 ) return;
    UndoStep s;
    s.m_what = m_what;
    s.m_extraBefore = m_extra;
    s.m_extraAfter = extra;
    const size_t n = std::min( m_before.size(), values.size() );
    auto         add = [&]( size_t i )
    {
        if ( i < n && m_before[i] != values[i] )
            s.m_values.emplace_back( static_cast<int>( i ), m_before[i], values[i] );
    };
    if ( m_all )
    {
        for ( size_t i = 0; i < n; ++i ) add( i );
    }
    else
    {
        std::sort( m_touched.begin(), m_touched.end() );
        m_touched.erase( std::unique( m_touched.begin(), m_touched.end() ), m_touched.end() );
        for ( int id : m_touched )
            if ( id >= 0 ) add( static_cast<size_t>( id ) );
    }
    m_before.clear();
    m_touched.clear();
    if ( s.empty() ) return;
    m_redo.clear();
    m_undo.push_back( std::move( s ) );
    while ( m_undo.size() > Steps ) m_undo.pop_front();
}

const UndoStep *UndoHistory::undo()
{
    if ( m_undo.empty() || m_depth > 0 ) return nullptr;
    m_redo.push_back( std::move( m_undo.back() ) );
    m_undo.pop_back();
    return &m_redo.back();
}

const UndoStep *UndoHistory::redo()
{
    if ( m_redo.empty() || m_depth > 0 ) return nullptr;
    m_undo.push_back( std::move( m_redo.back() ) );
    m_redo.pop_back();
    return &m_undo.back();
}

// ========================================================================================================= sub-tabs

void SubTabs::add( const juce::String &name, std::function<std::unique_ptr<juce::Component>()> make,
                   juce::Colour colour )
{
    Page p;
    p.m_name = name;
    p.m_colour = colour;
    p.m_make = std::move( make );
    p.m_button = std::make_unique<juce::TextButton>( name );
    p.m_button->setClickingTogglesState( false );
    p.m_button->setWantsKeyboardFocus( false );
    const int index = static_cast<int>( m_pages.size() );
    p.m_button->onClick = [this, index] { show( index ); };
    addAndMakeVisible( *p.m_button );
    m_pages.push_back( std::move( p ) );
    if ( m_current < 0 ) show( 0 );
}

void SubTabs::show( int index )
{
    if ( index < 0 || index >= count() ) return;
    if ( m_current >= 0 && m_pages[static_cast<size_t>( m_current )].m_comp != nullptr )
        m_pages[static_cast<size_t>( m_current )].m_comp->setVisible( false );
    m_current = index;
    Page &p = m_pages[static_cast<size_t>( index )];
    if ( p.m_comp == nullptr )
    {
        p.m_comp = p.m_make();
        addAndMakeVisible( *p.m_comp );
    }
    p.m_comp->setVisible( true );
    for ( size_t i = 0; i < m_pages.size(); ++i )
    {
        const juce::Colour c = m_pages[i].m_colour.isTransparent() ? m_skin.m_accent : m_pages[i].m_colour;
        m_pages[i].m_button->setColour( juce::TextButton::buttonOnColourId, c.withAlpha( 0.42f ) );
        m_pages[i].m_button->setToggleState( static_cast<int>( i ) == index, juce::dontSendNotification );
    }
    resized();
    if ( m_onChange ) m_onChange( index );
}

juce::Component *SubTabs::page() const
{
    return m_current >= 0 ? m_pages[static_cast<size_t>( m_current )].m_comp.get() : nullptr;
}

void SubTabs::reveal( juce::Component &part )
{
    for ( size_t i = 0; i < m_pages.size(); ++i )
    {
        juce::Component *c = m_pages[i].m_comp.get();
        if ( c != nullptr && ( c == &part || c->isParentOf( &part ) ) )
        {
            if ( m_current != static_cast<int>( i ) ) show( static_cast<int>( i ) );
            return;
        }
    }
}

void SubTabs::makeAll()
{
    auto r = getLocalBounds();
    r.removeFromTop( 36 );
    for ( auto &p : m_pages )
        if ( p.m_comp == nullptr )
        {
            p.m_comp = p.m_make();
            addChildComponent( *p.m_comp );   // hidden until its tab is chosen
            p.m_comp->setBounds( r );
        }
}

void SubTabs::resized()
{
    auto r = getLocalBounds();
    auto row = r.removeFromTop( 36 ).reduced( 10, 5 );
    for ( auto &p : m_pages )
    {
        const int w = juce::GlyphArrangement::getStringWidthInt( font( 13.5f ), p.m_name ) + 34;
        p.m_button->setBounds( row.removeFromLeft( std::max( 84, w ) ).reduced( 2, 0 ) );
    }
    if ( auto *c = page() ) c->setBounds( r );
}

void SubTabs::paint( juce::Graphics &g )
{
    g.setColour( m_skin.m_edge );
    g.fillRect( 10, 35, getWidth() - 20, 1 );
}

// ======================================================================================================== sections

bool isModulationGroup( const juce::String &title )
{
    const juce::String t = title.toLowerCase();
    return t.contains( "lfo" ) || t.startsWith( "mod " ) || t.contains( "matrix" ) || t.startsWith( "modulation" ) ||
           t == "trance gate";
}

SectionPlan planSections( const juce::StringArray &titles, int available,
                          const std::function<int( const std::vector<int> & )> &height, int tolerance )
{
    SectionPlan      plan;
    std::vector<int> all, sound, mod;
    for ( int i = 0; i < titles.size(); ++i )
    {
        all.push_back( i );
        ( isModulationGroup( titles[i] ) ? mod : sound ).push_back( i );
    }
    // FAMILY_NO_SECTIONS=1: every page whole (the manual's full-page pictures, which show all of a page at once).
    static const bool whole = juce::SystemStats::getEnvironmentVariable( "FAMILY_NO_SECTIONS", "" ).isNotEmpty();
    if ( whole || all.empty() || available <= 0 || height( all ) <= available + tolerance )
    {
        plan.m_groups.push_back( all );
        plan.m_names.add( {} );
        return plan;
    }
    // A section's name: the first group's title without its number ("LFO 1" -> "LFO", "Matrix 1-2" -> "Matrix").
    auto plain = [&]( int group )
    {
        juce::String t = titles[group].trim();
        while ( t.isNotEmpty() &&
                ( juce::CharacterFunctions::isDigit( t.getLastCharacter() ) || t.getLastCharacter() == '-' ) )
            t = t.dropLastCharacters( 1 ).trimEnd();
        return t.isEmpty() ? titles[group] : t;
    };
    const bool both = !sound.empty() && !mod.empty();
    // A synth's page begins with its presets: its first section is its sound, whatever group follows them.
    const bool presets = !sound.empty() && titles[sound.front()].trim().endsWithIgnoreCase( "preset" );
    auto       cut = [&]( const std::vector<int> &kind, const juce::String &firstName )
    {
        std::vector<int> section;
        bool             first = true;
        auto             close = [&]
        {
            if ( section.empty() ) return;
            const bool   named = first && ( both || ( presets && &kind == &sound ) );
            juce::String name = named ? firstName : plain( section.front() );
            for ( int n = 2; plan.m_names.contains( name ); ++n )
                name = plain( section.front() ) + " " + juce::String( n );
            plan.m_groups.push_back( section );
            plan.m_names.add( name );
            section.clear();
            first = false;
        };
        for ( int g : kind )
        {
            std::vector<int> more = section;
            more.push_back( g );
            if ( !section.empty() && height( more ) > available + tolerance ) close();
            section.push_back( g );
        }
        close();
    };
    cut( sound, "Sound" );
    cut( mod, "Modulation" );
    return plan;
}

SectionSwitch::SectionSwitch( const Skin &skin, const juce::StringArray &names ) : m_skin( skin )
{
    setNames( names );
}

void SectionSwitch::setNames( const juce::StringArray &names )
{
    juce::StringArray now;
    for ( auto *b : m_buttons ) now.add( b->getButtonText() );
    if ( now == names ) return;
    m_buttons.clear();
    for ( int i = 0; i < names.size(); ++i )
    {
        auto *b = m_buttons.add( new juce::TextButton( names[i] ) );
        b->setClickingTogglesState( false );
        b->setWantsKeyboardFocus( false );
        b->setColour( juce::TextButton::buttonOnColourId, m_skin.m_accent.withAlpha( 0.42f ) );
        b->onClick = [this, i]
        {
            if ( i == m_current ) return;
            setCurrent( i );
            if ( m_onChange ) m_onChange( i );
        };
        addAndMakeVisible( b );
    }
    setCurrent( m_current );
    resized();
}

void SectionSwitch::setCurrent( int index )
{
    m_current = juce::jlimit( 0, std::max( 0, m_buttons.size() - 1 ), index );
    for ( int i = 0; i < m_buttons.size(); ++i )
        m_buttons[i]->setToggleState( i == m_current, juce::dontSendNotification );
}

int SectionSwitch::bestWidth() const
{
    int w = 0;
    for ( auto *b : m_buttons )
        w += std::max( 84, juce::GlyphArrangement::getStringWidthInt( font( 13.5f ), b->getButtonText() ) + 34 );
    return w;
}

void SectionSwitch::resized()
{
    auto r = getLocalBounds();
    for ( auto *b : m_buttons )
        b->setBounds(
            r.removeFromLeft(
                 std::max( 84, juce::GlyphArrangement::getStringWidthInt( font( 13.5f ), b->getButtonText() ) + 34 ) )
                .reduced( 2, 0 ) );
}

int pageOverflow( juce::Component &c )
{
    int most = 0;
    if ( auto *v = dynamic_cast<juce::Viewport *>( &c ) )
        if ( auto *inner = v->getViewedComponent() ) most = inner->getHeight() - v->getMaximumVisibleHeight();
    for ( auto *child : c.getChildren() )
        if ( child->isVisible() ) most = std::max( most, pageOverflow( *child ) );
    return std::max( 0, most );
}

namespace
{
/**
 * @brief The visible section switches under @p c (a page split into Sound and Modulation has one).
 * @param[out]    c    where the search begins
 * @param[in,out] out  the switches found, appended to
 */
void findSwitches( juce::Component &c, std::vector<SectionSwitch *> &out )
{
    if ( auto *s = dynamic_cast<SectionSwitch *>( &c ) )
    {
        if ( s->isVisible() ) out.push_back( s );
        return;
    }
    for ( auto *child : c.getChildren() )
        if ( child->isVisible() ) findSwitches( *child, out );
}

}   // namespace

juce::String pageLines( juce::Component &page, const juce::String &name )
{
    juce::String                 out;
    std::vector<SectionSwitch *> switches;
    findSwitches( page, switches );
    if ( switches.empty() )
    {
        out << name << "\t" << pageOverflow( page ) << "\n";
        return out;
    }
    SectionSwitch *s = switches.front();
    const int      was = s->current();
    for ( int k = 0; k < s->count(); ++k )
    {
        s->setCurrent( k );
        if ( s->m_onChange ) s->m_onChange( k );
        out << name << " [" << s->name( k ) << "]\t" << pageOverflow( page ) << "\n";
    }
    s->setCurrent( was );
    if ( s->m_onChange ) s->m_onChange( was );
    return out;
}

juce::String pageReport( juce::TabbedComponent &tabs )
{
    juce::String            out;
    const int               front = tabs.getCurrentTabIndex();
    const juce::StringArray names = tabs.getTabNames();
    for ( int i = 0; i < tabs.getNumTabs(); ++i )
    {
        tabs.setCurrentTabIndex( i, false );
        juce::Component *page = tabs.getCurrentContentComponent();
        if ( page == nullptr ) continue;
        if ( auto *st = dynamic_cast<SubTabs *>( page ) )
        {
            const int was = st->current();
            for ( int j = 0; j < st->count(); ++j )
            {
                st->show( j );
                out << pageLines( *st, names[i] + ": " + st->name( j ) );
            }
            st->show( was );
        }
        else
        {
            out << pageLines( *page, names[i] );
        }
    }
    tabs.setCurrentTabIndex( front, false );
    return out;
}

// ========================================================================================================= controls

void ControlActions::showMenu( juce::Component &target, int id ) const
{
    juce::PopupMenu m;
    if ( m_describe ) m.addSectionHeader( m_describe( id ) );
    const int cc = m_controllerFor ? m_controllerFor( id ) : -1;
    if ( m_learn )
        m.addItem( cc >= 0 ? "MIDI learn (now CC " + juce::String( cc ) + ") ..." : juce::String( "MIDI learn ..." ),
                   [f = m_learn, id] { f( id ); } );
    if ( m_forget && cc >= 0 ) m.addItem( "Forget CC " + juce::String( cc ), [f = m_forget, id] { f( id ); } );
    if ( m_reset ) m.addItem( "Default value", [f = m_reset, id] { f( id ); } );
    if ( m_more ) m_more( m, id );
    m.showMenuAsync( juce::PopupMenu::Options().withTargetComponent( &target ) );
}

Knob::Knob( SliderStyle style, TextEntryBoxPosition box, const ControlActions *actions, int id )
    : juce::Slider( style, box ), m_actions( actions ), m_id( id )
{
}

void Knob::mouseDown( const juce::MouseEvent &e )
{
    if ( e.mods.isPopupMenu() && m_actions != nullptr )
    {
        m_actions->showMenu( *this, m_id );
        return;
    }
    m_altDragging =
        e.mods.isAltDown() && m_actions != nullptr && m_actions->m_altDrag && m_actions->m_altDrag( m_id, e, 0 );
    if ( m_altDragging ) return;
    juce::Slider::mouseDown( e );
}

void Knob::mouseDrag( const juce::MouseEvent &e )
{
    if ( m_altDragging )
    {
        m_actions->m_altDrag( m_id, e, 1 );
        return;
    }
    juce::Slider::mouseDrag( e );
}

void Knob::mouseUp( const juce::MouseEvent &e )
{
    if ( m_altDragging )
    {
        m_altDragging = false;
        m_actions->m_altDrag( m_id, e, 2 );
        return;
    }
    juce::Slider::mouseUp( e );
}

void Choice::mouseDown( const juce::MouseEvent &e )
{
    if ( e.mods.isPopupMenu() && m_actions != nullptr )
    {
        m_actions->showMenu( *this, m_id );
        return;
    }
    juce::ComboBox::mouseDown( e );
}

void Switch::mouseDown( const juce::MouseEvent &e )
{
    if ( e.mods.isPopupMenu() && m_actions != nullptr )
    {
        m_actions->showMenu( *this, m_id );
        return;
    }
    juce::ToggleButton::mouseDown( e );
}

void Switch::mouseUp( const juce::MouseEvent &e )
{
    if ( e.mods.isPopupMenu() && m_actions != nullptr ) return;   // the menu, not a click
    juce::ToggleButton::mouseUp( e );
}

LiveRings::LiveRings( std::function<float( int )> playedNormalised ) : m_played( std::move( playedNormalised ) )
{
    startTimerHz( 15 );
}

void LiveRings::add( juce::Slider &slider, int id )
{
    m_items.emplace_back( &slider, id );
}

void LiveRings::timerCallback()
{
    for ( auto &[s, id] : m_items )
    {
        juce::Slider *sl = s.getComponent();
        if ( sl == nullptr || !sl->isShowing() ) continue;
        const float     v = m_played( id );
        auto           &props = sl->getProperties();
        const juce::var was = props["live"];
        if ( std::isnan( v ) )
        {
            if ( !was.isVoid() )
            {
                props.remove( "live" );
                sl->repaint();
            }
        }
        else if ( was.isVoid() || std::abs( static_cast<float>( static_cast<double>( was ) ) - v ) > 0.002f )
        {
            props.set( "live", v );
            sl->repaint();
        }
        if ( m_arcs ) showModArcs( *sl, m_arcs( id ) );   // the modulations on it, redrawn when one of them moved
    }
}

// ========================================================================================================== console

namespace
{
constexpr float TopDb = 6.0f;       ///< the top of the strips' meter scale, dB
constexpr float FloorDb = -60.0f;   ///< the floor of the strips' meter scale, dB
/**
 * @brief A linear amplitude in dB, -100 for silence.
 * @param[in] linear  the linear amplitude
 * @return the level in dB, -100 for silence
 */
float toDb( float linear )
{
    return linear > 1.0e-5f ? 20.0f * std::log10( linear ) : -100.0f;
}
}   // namespace

ChannelStrip::ChannelStrip( const Skin &skin, const juce::String &name, juce::Colour colour, Control fader,
                            std::vector<Control> knobs, const ControlActions *actions, LiveRings *live )
    : m_skin( skin ), m_name( name ), m_colour( colour )
{
    if ( fader.m_param != nullptr )
    {
        m_fader = std::make_unique<Knob>( juce::Slider::LinearVertical, juce::Slider::NoTextBox, actions, fader.m_id );
        m_fader->setColour( juce::Slider::trackColourId, m_colour.withAlpha( 0.8f ) );
        m_fader->setColour( juce::Slider::thumbColourId, m_skin.m_ink );
        m_fader->setColour( juce::Slider::backgroundColourId, m_skin.m_bg );
        m_fader->setPopupDisplayEnabled( true, true, nullptr );
        m_fader->setTooltip( fader.m_label );
        addAndMakeVisible( *m_fader );
        m_faderLink = std::make_unique<juce::SliderParameterAttachment>( *fader.m_param, *m_fader );
        m_fader->setDoubleClickReturnValue( true, fader.m_param->convertFrom0to1( fader.m_param->getDefaultValue() ) );
        if ( live != nullptr ) live->add( *m_fader, fader.m_id );
    }
    for ( const Control &c : knobs )
    {
        if ( c.m_param == nullptr ) continue;
        auto k = std::make_unique<Knob>( juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox, actions,
                                         c.m_id );
        k->setColour( juce::Slider::rotarySliderFillColourId, m_colour );
        k->setPopupDisplayEnabled( true, true, nullptr );
        k->setTooltip( c.m_label );
        addAndMakeVisible( *k );
        m_links.push_back( std::make_unique<juce::SliderParameterAttachment>( *c.m_param, *k ) );
        k->setDoubleClickReturnValue( true, c.m_param->convertFrom0to1( c.m_param->getDefaultValue() ) );
        if ( live != nullptr ) live->add( *k, c.m_id );
        auto l = std::make_unique<juce::Label>( juce::String(), c.m_label );
        l->setJustificationType( juce::Justification::centred );
        l->setColour( juce::Label::textColourId, m_skin.m_dim );
        l->setFont( font( 10.0f ) );
        l->setMinimumHorizontalScale( 0.7f );
        l->setInterceptsMouseClicks( false, false );
        addAndMakeVisible( *l );
        m_isSend.push_back( c.m_send );
        m_knobs.push_back( std::move( k ) );
        m_names.push_back( std::move( l ) );
    }
}

void ChannelStrip::setSound( const juce::String &s )
{
    if ( s == m_sound ) return;
    m_sound = s;
    setTooltip( s );
    repaint();
}

void ChannelStrip::setHeadButton( juce::Component *b )
{
    m_head = b;
    if ( b != nullptr ) addAndMakeVisible( b );
    resized();
}

void ChannelStrip::meter( float peak, float rms, double seconds )
{
    const float pDb = toDb( peak ), rDb = toDb( rms );
    const float a = static_cast<float>( std::exp( -seconds / 0.3 ) );
    m_rmsDb = rDb >= m_rmsDb ? rDb : juce::jmax( rDb, FloorDb - 1.0f + ( m_rmsDb - ( FloorDb - 1.0f ) ) * a );
    if ( pDb >= m_holdDb )
    {
        m_holdDb = pDb;
        m_holdAge = 0.0;
    }
    else
    {
        m_holdAge += seconds;
        if ( m_holdAge > 1.5 ) m_holdDb = juce::jmax( pDb, m_holdDb - static_cast<float>( 20.0 * seconds ) );
    }
    repaint( m_meterArea.expanded( 2 ).withBottom( getHeight() ) );
}

void ChannelStrip::resized()
{
    auto r = getLocalBounds().reduced( 3 );
    r.removeFromTop( 32 );   // the name, and the sound under it
    if ( m_head != nullptr ) m_head->setBounds( r.removeFromTop( 22 ).reduced( 4, 1 ) );
    // Folded, only the pan above the fader; unfolded, the sends too. Every strip keeps the same room, so the faders and
    // the meters line up across the console.
    const int slots = m_sends ? 6 : 1;
    const int lh = 12, kh = juce::jlimit( 20, 42, std::min( r.getWidth() - 18, ( r.getHeight() - 150 ) / slots - lh ) );
    int       used = 0;
    for ( size_t i = 0; i < m_knobs.size(); ++i )
    {
        const bool show = m_sends || !m_isSend[i];
        m_knobs[i]->setVisible( show && used < slots );
        m_names[i]->setVisible( show && used < slots );
        if ( !show || used >= slots ) continue;
        auto row = r.removeFromTop( kh + lh );
        m_knobs[i]->setBounds( row.removeFromTop( kh ) );
        m_names[i]->setBounds( row );
        ++used;
    }
    r.removeFromTop( std::max( 0, slots - used ) * ( kh + lh ) );
    r.removeFromTop( 6 );
    r.removeFromBottom( 15 );   // the peak readout
    if ( m_fader != nullptr )
        m_fader->setBounds( r.removeFromLeft( r.getWidth() / 2 ) );
    else
        r.removeFromLeft( r.getWidth() / 4 );
    m_meterArea = r.reduced( 4, 6 );
}

void ChannelStrip::paint( juce::Graphics &g )
{
    const auto b = getLocalBounds().toFloat().reduced( 1.0f );
    g.setColour( m_skin.m_group );
    g.fillRoundedRectangle( b, m_skin.m_radius );
    g.setColour( m_colour );
    g.fillRoundedRectangle( b.withHeight( 3.0f ), 1.5f );
    g.setColour( m_skin.m_ink );
    g.setFont( font( 12.5f, true ) );
    g.drawFittedText( m_name, getLocalBounds().withHeight( 22 ).reduced( 2, 0 ), juce::Justification::centred, 1 );
    if ( m_sound.isNotEmpty() )
    {
        g.setColour( m_skin.m_dim );
        g.setFont( font( 10.0f ) );
        g.drawFittedText( m_sound, getLocalBounds().withTrimmedTop( 18 ).withHeight( 14 ).reduced( 3, 0 ),
                          juce::Justification::centred, 1, 0.8f );
    }
    // The meter: -60 .. +6 dB, the RMS bar, the held peak, 0 dBFS marked.
    const auto m = m_meterArea.toFloat();
    g.setColour( m_skin.m_bg );
    g.fillRect( m );
    auto yOf = [&]( float db )
    {
        const float t = ( juce::jlimit( FloorDb, TopDb, db ) - FloorDb ) / ( TopDb - FloorDb );
        return m.getBottom() - t * m.getHeight();
    };
    if ( m_rmsDb > FloorDb )
    {
        const float top = yOf( m_rmsDb ), y6 = yOf( -6.0f ), y0 = yOf( 0.0f );
        g.setColour( m_skin.m_good.withAlpha( 0.85f ) );
        g.fillRect( m.getX(), std::max( top, y6 ), m.getWidth(), m.getBottom() - std::max( top, y6 ) );
        if ( top < y6 )
        {
            g.setColour( m_skin.m_accent.withAlpha( 0.85f ) );
            g.fillRect( m.getX(), std::max( top, y0 ), m.getWidth(), y6 - std::max( top, y0 ) );
        }
        if ( top < y0 )
        {
            g.setColour( m_skin.m_bad );
            g.fillRect( m.getX(), top, m.getWidth(), y0 - top );
        }
    }
    if ( m_holdDb > FloorDb )
    {
        g.setColour( m_holdDb > 0.0f ? m_skin.m_bad : m_skin.m_ink );
        g.fillRect( m.getX(), yOf( m_holdDb ) - 1.0f, m.getWidth(), 2.0f );
    }
    g.setColour( m_skin.m_faint );
    g.fillRect( m.getX() - 3.0f, yOf( 0.0f ), 3.0f, 1.0f );
    g.setColour( m_holdDb > 0.0f ? m_skin.m_bad : m_skin.m_dim );
    g.setFont( font( 10.0f ) );
    g.drawText( m_holdDb > FloorDb ? juce::String( m_holdDb, 1 ) : juce::String( "-inf" ),
                getLocalBounds().removeFromBottom( 17 ), juce::Justification::centred );
}

Console::Console( const Skin &skin ) : m_skin( skin )
{
    m_fold.setClickingTogglesState( true );
    m_fold.setTooltip( "Unfold every strip's sends, or fold them away" );
    m_fold.setWantsKeyboardFocus( false );
    m_fold.onClick = [this]
    {
        m_sends = m_fold.getToggleState();
        for ( auto &s : m_strips ) s->showSends( m_sends );
    };
    addAndMakeVisible( m_fold );
    m_view.setViewedComponent( &m_inner, false );
    m_view.setScrollBarsShown( false, true );
    addAndMakeVisible( m_view );
}

ChannelStrip &Console::add( std::unique_ptr<ChannelStrip> strip )
{
    m_inner.addAndMakeVisible( *strip );
    strip->showSends( m_sends );
    m_strips.push_back( std::move( strip ) );
    resized();
    return *m_strips.back();
}

void Console::resized()
{
    auto r = getLocalBounds().reduced( 6, 4 );
    m_fold.setBounds( r.removeFromTop( 26 ).removeFromLeft( 110 ) );
    r.removeFromTop( 6 );
    m_view.setBounds( r );
    if ( m_strips.empty() ) return;
    const int  n = size();
    const bool scroll = r.getWidth() / n < MinStrip;
    const int  w = scroll ? MinStrip : r.getWidth() / n;
    const int  h = r.getHeight() - ( scroll ? m_view.getScrollBarThickness() : 0 );
    m_inner.setSize( w * n, h );
    for ( int i = 0; i < n; ++i ) m_strips[static_cast<size_t>( i )]->setBounds( i * w, 0, w - 3, h );
}

// ========================================================================================================== headset

Headset::Headset()
{
    m_receiver.addListener( this );
}

Headset::~Headset()
{
    m_receiver.removeListener( this );
    m_receiver.disconnect();
}

void Headset::listen( int port )
{
    if ( port == m_port && ( port == 0 || m_error.isEmpty() ) ) return;
    m_receiver.disconnect();
    m_port = port;
    m_error = {};
    if ( port <= 0 ) return;
    if ( !m_receiver.connect( port ) ) m_error = "port " + juce::String( port ) + " is taken";
}

bool Headset::active() const
{
    return juce::Time::getMillisecondCounterHiRes() - m_last < 3000.0;
}

void Headset::oscMessageReceived( const juce::OSCMessage &m )
{
    if ( m.getAddressPattern().toString() != "/hands" || m.size() < 6 ) return;
    auto f = [&]( int i )
    {
        return m[i].isFloat32() ? m[i].getFloat32() : m[i].isInt32() ? static_cast<float>( m[i].getInt32() ) : 0.0f;
    };
    Hands h;
    for ( int i = 0; i < 2; ++i )
    {
        h.m_height[i] = juce::jlimit( 0.0f, 1.0f, f( i ) );
        h.m_pinch[i] = f( 2 + i ) > 0.5f;
        h.m_tracked[i] = f( 4 + i ) > 0.5f;
    }
    inject( h );
}

void Headset::inject( const Hands &h )
{
    m_hands = h;
    ++m_messages;
    m_last = juce::Time::getMillisecondCounterHiRes();
    m_source = "the headset";
}

HeadsetEvents Headset::poll()
{
    // The grammar of the Quest apps (their main.cpp, the same rules and the same numbers): both hands closed together
    // fire the next one at once and spoil both pinches; a single short pinch acts when it opens again, so it never also
    // counts as part of a pinch of both; the right one held 0.6 s fires its second action and is spoiled too; a hand
    // moves its control only while it does not pinch; both controls glide (0.15 s) and are centred, so a hand at mid
    // height plays what was composed: the filter with a dead zone of 0.1 round the middle, the throw from the middle up.
    HeadsetEvents e;
    const double  now = juce::Time::getMillisecondCounterHiRes();
    const double  dt = m_lastPoll > 0.0 ? std::min( 0.2, ( now - m_lastPoll ) / 1000.0 ) : 0.0;
    m_lastPoll = now;
    if ( !active() )
    {
        m_was[0] = m_was[1] = m_both = false;
        m_spoiled[0] = m_spoiled[1] = true;
        if ( m_holding )
        {
            m_holding = false;
            e.m_holdEnded = true;
        }
        return e;
    }
    const Hands h = m_hands;
    const bool  closed[2] = { h.m_tracked[0] && h.m_pinch[0], h.m_tracked[1] && h.m_pinch[1] };
    if ( closed[0] && closed[1] )
    {
        m_spoiled[0] = m_spoiled[1] = true;
        if ( !m_both )
        {
            e.m_next = true;
            m_both = true;
        }
    }
    for ( int i = 0; i < 2; ++i )
    {
        if ( closed[i] && !m_was[i] && !closed[1 - i] )
        {
            m_spoiled[i] = false;
            m_since[i] = now;
        }
        if ( i == 1 && closed[1] && !m_spoiled[1] && now - m_since[1] >= 600.0 )
        {
            m_spoiled[1] = true;
            e.m_hold = true;
            m_holding = true;
        }
        if ( i == 1 && !closed[1] && m_holding )
        {
            m_holding = false;
            e.m_holdEnded = true;
        }
        if ( !closed[i] && m_was[i] && !m_spoiled[i] )
        {
            if ( i == 0 )
                e.m_playStop = true;
            else
                e.m_action = true;
        }
        m_was[i] = closed[i];
    }
    if ( !closed[0] && !closed[1] ) m_both = false;
    const float a = dt > 0.0 ? static_cast<float>( 1.0 - std::exp( -dt / 0.15 ) ) : 1.0f;
    // The left hand: below the middle a low pass, above it a high pass, a dead zone round the middle.
    float ft = 0.0f;
    if ( h.m_tracked[0] && !h.m_pinch[0] )
    {
        const float d = ( h.m_height[0] - 0.5f ) * 2.0f;
        ft = std::abs( d ) < 0.1f ? 0.0f : juce::jlimit( -1.0f, 1.0f, ( d - std::copysign( 0.1f, d ) ) / 0.9f );
    }
    else if ( h.m_tracked[0] )
    {
        ft = m_filter;   // pinching: held where it was
    }
    // The right hand: the throw from the middle up.
    float tt = 0.0f;
    if ( h.m_tracked[1] && !h.m_pinch[1] )
        tt = juce::jlimit( 0.0f, 1.0f, 2.0f * ( h.m_height[1] - 0.5f ) );
    else if ( h.m_tracked[1] )
        tt = m_throw;
    const float nf = m_filter + a * ( ft - m_filter ), nt = m_throw + a * ( tt - m_throw );
    if ( std::abs( nf - m_filter ) > 1.0e-4f || ( nf == 0.0f ) != ( m_filter == 0.0f ) ) e.m_filterMoved = true;
    if ( std::abs( nt - m_throw ) > 1.0e-4f || ( nt == 0.0f ) != ( m_throw == 0.0f ) ) e.m_throwMoved = true;
    m_filter = std::abs( nf ) < 1.0e-3f ? 0.0f : nf;
    m_throw = nt < 1.0e-3f ? 0.0f : nt;
    e.m_filter = m_filter;
    e.m_throwAmount = m_throw;
    return e;
}

juce::String Headset::statusText() const
{
    if ( m_port <= 0 ) return "the headset: not listening (Off)";
    if ( m_error.isNotEmpty() ) return "the headset: " + m_error;
    if ( active() ) return "the headset sends its hands (port " + juce::String( m_port ) + ")";
    if ( everSeen() ) return "the headset is quiet (port " + juce::String( m_port ) + ")";
    return "listening for a headset on port " + juce::String( m_port );
}

std::vector<std::pair<juce::String, juce::String>> headsetGrammarShort( const juce::String &action,
                                                                        const juce::String &hold,
                                                                        const juce::String &left,
                                                                        const juce::String &right )
{
    std::vector<std::pair<juce::String, juce::String>> t = { { "left pinch", "play / stop" },
                                                             { "both pinched", "the next one" },
                                                             { "right pinch", action } };
    if ( hold.isNotEmpty() ) t.emplace_back( "right held", hold );
    t.emplace_back( "left hand", left );
    t.emplace_back( "right hand", right );
    return t;
}

void drawHeadsetBox( juce::Graphics &g, juce::Rectangle<int> area, const Skin &skin, const Headset &headset,
                     const juce::String &action, const juce::String &hold, const juce::String &left,
                     const juce::String &right, bool framed )
{
    auto h = area.reduced( framed ? 6 : 0, 0 );
    if ( framed )
    {   // a box of its own; inside a page's group (Phosphene's) the group is the box
        g.setColour( skin.m_group );
        g.fillRoundedRectangle( h.toFloat(), skin.m_radius );
        g.setColour( skin.m_edge );
        g.drawRoundedRectangle( h.toFloat().reduced( 0.5f ), skin.m_radius, 1.0f );
        h = h.reduced( 12, 8 );
        g.setColour( skin.family( FamilyE::MOTION ) );
        g.setFont( font( 11.5f, true ) );
        g.drawText( "HEADSET", h.removeFromTop( 16 ), juce::Justification::centredLeft );
    }
    g.setColour( skin.m_dim );
    g.setFont( font( 11.5f ) );
    g.drawFittedText( headset.statusText(), h.removeFromTop( 18 ), juce::Justification::centredLeft, 1, 0.8f );
    h.removeFromTop( 4 );
    // The hands: a slot each, the dot on its height, lit while it pinches.
    const Hands hands = headset.hands();
    const bool  live = headset.active();
    auto        bars = h.removeFromLeft( 96 );
    for ( int i = 0; i < 2; ++i )
    {
        auto col = bars.removeFromLeft( 48 ).reduced( 6, 0 );
        g.setColour( skin.m_dim );
        g.drawText( i == 0 ? "left" : "right", col.removeFromBottom( 16 ), juce::Justification::centred );
        const auto track = col.withSizeKeepingCentre( 10, col.getHeight() ).toFloat();
        g.setColour( skin.m_faint );
        g.fillRoundedRectangle( track, 5.0f );
        if ( live && hands.m_tracked[i] )
        {
            const float y = track.getBottom() - hands.m_height[i] * track.getHeight();
            g.setColour( hands.m_pinch[i] ? skin.m_onset : skin.m_accent );
            g.fillEllipse( track.getCentreX() - 7.0f, y - 7.0f, 14.0f, 14.0f );
        }
    }
    // The grammar.
    h.removeFromLeft( 8 );
    const auto lines = headsetGrammarShort( action, hold, left, right );
    const int  lh = juce::jlimit( 12, 16, h.getHeight() / std::max( 1, static_cast<int>( lines.size() ) ) );
    g.setFont( font( std::min( 12.0f, static_cast<float>( lh ) - 1.0f ) ) );
    for ( const auto &[gesture, effect] : lines )
    {
        auto row = h.removeFromTop( lh );
        g.setColour( skin.m_dim );
        g.drawText( gesture, row.removeFromLeft( 80 ), juce::Justification::centredLeft );
        g.setColour( skin.m_ink );
        g.drawFittedText( effect, row, juce::Justification::centredLeft, 1, 0.75f );
    }
}

juce::String headsetGrammar( const juce::String &action, const juce::String &hold )
{
    juce::String t;
    t << "left pinch\tplay / stop\n"
      << "both hands pinched\tthe next one, from a new seed\n"
      << "right pinch\t" << action << "\n";
    if ( hold.isNotEmpty() ) t << "right pinch held\t" << hold << "\n";
    t << "left hand up or down\tthe master filter: low pass below the middle, high pass above\n"
      << "right hand up\tthe echo throw\n\n"
      << "A hand moves its control only while it does not pinch; both glide and are centred, so a hand at mid height "
         "plays what was composed. The Quest app sends its hands to this computer in bridge mode: bridge_host = this "
         "computer's address in the app's config file (audio=0 leaves the headset silent).";
    return t;
}

// ---------------------------------------------------------------------------------------------------------------------
// The keyboard's options (02.10.2026).

uint16_t scaleMask( const juce::String &name )
{
    static const std::pair<const char *, std::array<int, 8>> Scales[] = {
        { "aeolian", { 0, 2, 3, 5, 7, 8, 10, -1 } },         { "minor", { 0, 2, 3, 5, 7, 8, 10, -1 } },
        { "dorian", { 0, 2, 3, 5, 7, 9, 10, -1 } },          { "phrygian", { 0, 1, 3, 5, 7, 8, 10, -1 } },
        { "harmonic minor", { 0, 2, 3, 5, 7, 8, 11, -1 } },  { "minor pentatonic", { 0, 3, 5, 7, 10, -1, -1, -1 } },
        { "ionian", { 0, 2, 4, 5, 7, 9, 11, -1 } },          { "major", { 0, 2, 4, 5, 7, 9, 11, -1 } },
        { "mixolydian", { 0, 2, 4, 5, 7, 9, 10, -1 } },      { "lydian", { 0, 2, 4, 6, 7, 9, 11, -1 } },
        { "locrian", { 0, 1, 3, 5, 6, 8, 10, -1 } },         { "phrygian dominant", { 0, 1, 4, 5, 7, 8, 10, -1 } },
        { "double harmonic", { 0, 1, 4, 5, 7, 8, 11, -1 } }, { "hexachord", { 0, 2, 3, 5, 7, 10, -1, -1 } },
    };
    const juce::String n = name.trim().toLowerCase();
    for ( const auto &[key, steps] : Scales )
        if ( n == key )
        {
            uint16_t m = 0;
            for ( int s : steps )
                if ( s >= 0 ) m = static_cast<uint16_t>( m | ( 1u << s ) );
            return m;
        }
    return 0x0FFF;
}

int snapToScale( int pitch, int root, uint16_t mask )
{
    if ( ( mask & 0x0FFF ) == 0x0FFF || ( mask & 0x0FFF ) == 0 ) return pitch;
    auto in = [&]( int p ) { return ( ( mask >> ( ( ( p - root ) % 12 + 12 ) % 12 ) ) & 1u ) != 0; };
    for ( int d = 0; d < 12; ++d )
    {
        if ( pitch - d >= 0 && in( pitch - d ) ) return pitch - d;   // a tie goes down
        if ( pitch + d <= 127 && in( pitch + d ) ) return pitch + d;
    }
    return pitch;
}

int shapeVelocity( int velocity, int curve )
{
    const int v = std::clamp( velocity, 1, 127 );
    if ( curve == 3 ) return 100;
    if ( curve != 1 && curve != 2 ) return v;
    const double x = static_cast<double>( v ) / 127.0;
    return std::clamp( static_cast<int>( std::lround( 127.0 * std::pow( x, curve == 1 ? 0.6 : 1.7 ) ) ), 1, 127 );
}

void KeyMemory::clear()
{
    for ( auto &row : m_target )
        for ( auto &t : row ) t = -2;
    for ( auto &row : m_pitch )
        for ( auto &p : row ) p = 0;
}

void KeyMemory::press( int channel, int key, int target, int pitch )
{
    if ( channel < 0 || channel > 15 || key < 0 || key > 127 ) return;
    m_target[channel][key] = static_cast<int16_t>( target );
    m_pitch[channel][key] = static_cast<int16_t>( pitch );
}

bool KeyMemory::release( int channel, int key, int &target, int &pitch )
{
    if ( channel < 0 || channel > 15 || key < 0 || key > 127 || m_target[channel][key] == -2 ) return false;
    target = m_target[channel][key];
    pitch = m_pitch[channel][key];
    m_target[channel][key] = -2;
    return true;
}

// ========================================================================================================== window

WindowKeeper::~WindowKeeper()
{
    if ( m_window != nullptr ) m_window->removeComponentListener( this );
}

bool WindowKeeper::attach( juce::Component &editor, const juce::String &app )
{
    if ( m_window != nullptr ) return true;
    // Only the standalone's own window: a host's is the host's to place.
    if ( !juce::JUCEApplicationBase::isStandaloneApp() ) return false;
    auto *w = editor.findParentComponentOfClass<juce::ResizableWindow>();
    if ( w == nullptr ) return false;
    m_app = app;
    const juce::String state = Settings::of( app ).value( "window" );
    if ( state.isNotEmpty() ) w->restoreWindowStateFromString( state );   // onto a screen should that one be gone
    m_window = w;
    w->addComponentListener( this );
    return true;
}

void WindowKeeper::componentMovedOrResized( juce::Component &, bool, bool )
{
    // Not while minimised or in full screen (the state would be the screen's); maximised is kept as such ("fs ...").
    if ( m_window == nullptr || m_window->isMinimised() ||
         juce::Desktop::getInstance().getKioskModeComponent() != nullptr )
        return;
    Settings::of( m_app ).setValue( "window", m_window->getWindowStateAsString(), false );
}

// ========================================================================================================== reveal

namespace
{

/**
 * @brief The revealers under @p c (c itself included).
 * @param[out]    c    where the search begins
 * @param[in,out] out  the revealers found, appended to
 */
void collectRevealers( juce::Component &c, std::vector<Revealer *> &out )
{
    if ( auto *r = dynamic_cast<Revealer *>( &c ) ) out.push_back( r );
    for ( auto *child : c.getChildren() ) collectRevealers( *child, out );
}

/**
 * @brief Makes every part under @p page that is made only when first shown (a few rounds: a part made may hold more).
 * @param[in,out] page  the page
 */
void makeEverything( juce::Component &page )
{
    std::vector<Revealer *> done;
    for ( int round = 0; round < 4; ++round )
    {
        std::vector<Revealer *> all;
        collectRevealers( page, all );
        bool any = false;
        for ( Revealer *r : all )
            if ( std::find( done.begin(), done.end(), r ) == done.end() )
            {
                done.push_back( r );
                r->makeAll();
                any = true;
            }
        if ( !any ) return;
    }
}

/** @brief A ring that lights a control for a moment, over its window, and goes by itself. */
class Flash final : public juce::Component, private juce::Timer
{
public:
    /**
     * @brief Lights @p target in @p colour.
     * @param[in,out] target  the component lit
     * @param[in]     colour  the ring's colour
     */
    Flash( juce::Component &target, juce::Colour colour ) : m_target( &target ), m_colour( colour )
    {
        setInterceptsMouseClicks( false, false );
        if ( auto *top = target.getTopLevelComponent() ) top->addAndMakeVisible( this );
        place();
        startTimerHz( 30 );
    }
    /**
     * @brief The ring, pulsing twice and fading.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override
    {
        const float t = static_cast<float>( m_ticks ) / 30.0f;
        const float a = juce::jlimit( 0.0f, 1.0f, 1.0f - t / 1.6f ) *
                        ( 0.6f + 0.4f * std::cos( t * juce::MathConstants<float>::twoPi * 1.25f ) );
        const auto  r = getLocalBounds().toFloat().reduced( 2.0f );
        g.setColour( m_colour.withAlpha( 0.14f * a ) );
        g.fillRoundedRectangle( r, 6.0f );
        g.setColour( m_colour.withAlpha( a ) );
        g.drawRoundedRectangle( r, 6.0f, 2.5f );
    }

private:
    /** @brief Over the control, a little larger. */
    void place()
    {
        auto *top = getParentComponent();
        if ( top == nullptr || m_target == nullptr ) return;
        setBounds( top->getLocalArea( m_target.getComponent(), m_target->getLocalBounds() ).expanded( 6 ) );
    }
    /** @brief Follows the control; gone after 1.6 s. */
    void timerCallback() override
    {
        if ( ++m_ticks > 48 || m_target == nullptr || getParentComponent() == nullptr )
        {
            stopTimer();
            juce::MessageManager::callAsync( [safe = juce::Component::SafePointer<Flash>( this )]
                                             { delete safe.getComponent(); } );
            return;
        }
        place();
        repaint();
    }
    juce::Component::SafePointer<juce::Component> m_target;      ///< what it lights
    juce::Colour                                  m_colour;      ///< in what
    int                                           m_ticks = 0;   ///< timer ticks so far
};

}   // namespace

int controlId( const juce::Component &c )
{
    if ( auto *k = dynamic_cast<const Knob *>( &c ) ) return k->id();
    if ( auto *ch = dynamic_cast<const Choice *>( &c ) ) return ch->id();
    if ( auto *s = dynamic_cast<const Switch *>( &c ) ) return s->id();
    const juce::var v = c.getProperties()["frameId"];
    return v.isVoid() ? -1 : static_cast<int>( v );
}

juce::Component *findControl( juce::Component &root, int id )
{
    if ( controlId( root ) == id ) return &root;
    for ( auto *child : root.getChildren() )
        if ( auto *f = findControl( *child, id ) ) return f;
    return nullptr;
}

void showModArcs( juce::Slider &slider, const std::vector<ModArc> &arcs )
{
    auto        &props = slider.getProperties();
    juce::String key;
    for ( const ModArc &m : arcs )
        key << juce::roundToInt( m.m_depth * 1000.0f ) << ',' << ( m.m_unipolar ? 1 : 0 ) << ','
            << juce::roundToInt( m.m_now * 300.0f ) << ',' << static_cast<juce::int64>( m.m_colour.getARGB() ) << ';';
    if ( key == props["modsKey"].toString() ) return;
    if ( arcs.empty() )
    {
        props.remove( "mods" );
        props.remove( "modsKey" );
    }
    else
    {
        juce::Array<juce::var> list;
        for ( const ModArc &m : arcs )
        {
            juce::Array<juce::var> one;
            one.add( m.m_depth );
            one.add( m.m_unipolar );
            one.add( m.m_now );
            one.add( static_cast<juce::int64>( m.m_colour.getARGB() ) );
            list.add( juce::var( one ) );
        }
        props.set( "mods", juce::var( list ) );
        props.set( "modsKey", key );
    }
    slider.repaint();
}

void flash( juce::Component &c, juce::Colour colour )
{
    new Flash( c, colour );
}

bool revealControl( const std::vector<juce::Component *> &pages, const std::function<void( int )> &showPage, int id,
                    juce::Colour colour )
{
    for ( size_t i = 0; i < pages.size(); ++i )
    {
        juce::Component *page = pages[i];
        if ( page == nullptr ) continue;
        juce::Component *c = findControl( *page, id );
        if ( c == nullptr )
        {   // a page that makes its parts when they are first shown: all of them now
            makeEverything( *page );
            c = findControl( *page, id );
        }
        if ( c == nullptr )
        {   // a part that shows one of several (an instance chooser): asked for this one
            std::vector<Revealer *> all;
            collectRevealers( *page, all );
            for ( Revealer *r : all )
                if ( r->bringId( id ) && ( c = findControl( *page, id ) ) != nullptr ) break;
        }
        if ( c == nullptr ) continue;
        if ( showPage ) showPage( static_cast<int>( i ) );
        // Every revealer between the page and the control, the outermost first: its sub-tab, its section.
        std::vector<Revealer *> chain;
        for ( juce::Component *p = c->getParentComponent(); p != nullptr; p = p->getParentComponent() )
        {
            if ( auto *r = dynamic_cast<Revealer *>( p ) ) chain.push_back( r );
            if ( p == page ) break;
        }
        for ( auto it = chain.rbegin(); it != chain.rend(); ++it ) ( *it )->reveal( *c );
        // A viewport holding it scrolled to it (inside the page, or the panel's own around it).
        for ( juce::Component *p = c->getParentComponent(); p != nullptr; p = p->getParentComponent() )
            if ( auto *v = dynamic_cast<juce::Viewport *>( p ) )
                if ( auto *inner = v->getViewedComponent() )
                {
                    const auto r = inner->getLocalArea( c, c->getLocalBounds() );
                    const auto seen = v->getViewArea();
                    if ( !seen.contains( r ) )
                        v->setViewPosition( std::max( 0, r.getX() - 20 ), std::max( 0, r.getY() - 40 ) );
                }
        // Lit on the next turn of the message loop, when the page has laid itself out.
        juce::MessageManager::callAsync(
            [safe = juce::Component::SafePointer<juce::Component>( c ), colour]
            {
                if ( safe != nullptr && safe->isShowing() ) flash( *safe, colour );
            } );
        return true;
    }
    return false;
}

bool revealControl( juce::TabbedComponent &tabs, int id, juce::Colour colour )
{
    std::vector<juce::Component *> pages;
    for ( int i = 0; i < tabs.getNumTabs(); ++i ) pages.push_back( tabs.getTabContentComponent( i ) );
    return revealControl( pages, [&tabs]( int i ) { tabs.setCurrentTabIndex( i ); }, id, colour );
}

// ========================================================================================================= palette

namespace
{

/**
 * @brief How well one word of the search fits a text: its start best, a word's start next, anywhere in it, then its
 *        letters in order (scattered). 0 when it does not fit, else the higher the better.
 * @param[in] text  the text searched
 * @param[in] word  one word of the search
 * @return how well the word fits, 0 when it does not
 */
float fitWord( const juce::String &text, const juce::String &word )
{
    if ( text.startsWith( word ) ) return 4.0f;
    const int at = text.indexOf( word );
    if ( at > 0 ) return text[at - 1] == ' ' ? 3.0f : 2.0f;
    int i = 0;
    for ( const juce::juce_wchar c : text )
        if ( i < word.length() && c == word[i] ) ++i;
    return i == word.length() ? 0.6f : 0.0f;
}

}   // namespace

CommandPalette::CommandPalette( const Skin &skin ) : m_skin( skin ), m_list( {}, this )
{
    m_search.setFont( juce::FontOptions( 17.0f ) );
    m_search.setTextToShowWhenEmpty( "A control, a preset or an action -- \"cutoff\", \"cutoff 800\", \"export\" ...",
                                     m_skin.m_dim );
    m_search.setColour( juce::TextEditor::backgroundColourId, m_skin.m_raised );
    m_search.setColour( juce::TextEditor::textColourId, m_skin.m_ink );
    m_search.setColour( juce::TextEditor::outlineColourId, m_skin.m_edge );
    m_search.setColour( juce::TextEditor::focusedOutlineColourId, m_skin.m_accent );
    m_search.addListener( this );
    m_search.addKeyListener( this );
    m_list.setRowHeight( 30 );
    m_list.setColour( juce::ListBox::backgroundColourId, juce::Colours::transparentBlack );
    addAndMakeVisible( m_search );
    addAndMakeVisible( m_list );
    setInterceptsMouseClicks( true, true );
    setVisible( false );
}

void CommandPalette::open( std::vector<Command> commands )
{
    m_all = std::move( commands );
    m_fixed = m_all.size();
    if ( auto *p = getParentComponent() ) setBounds( p->getLocalBounds() );
    m_search.clear();
    filter();
    setVisible( true );
    toFront( true );
    m_search.grabKeyboardFocus();
}

void CommandPalette::close()
{
    setVisible( false );
    m_all.clear();
    m_shown.clear();
    if ( auto *p = getParentComponent() ) p->grabKeyboardFocus();
}

juce::Rectangle<int> CommandPalette::panel() const
{
    const int w = std::min( 680, getWidth() - 40 ), h = std::min( 480, getHeight() - 120 );
    return { ( getWidth() - w ) / 2, 70, w, h };
}

void CommandPalette::resized()
{
    auto r = panel().reduced( 12 );
    m_search.setBounds( r.removeFromTop( 36 ) );
    r.removeFromTop( 8 );
    m_list.setBounds( r );
}

void CommandPalette::paint( juce::Graphics &g )
{
    g.fillAll( m_skin.m_bg.withAlpha( 0.55f ) );
    const auto p = panel().toFloat();
    g.setColour( m_skin.m_group.withAlpha( 1.0f ) );
    g.fillRoundedRectangle( p, m_skin.m_radius * 2.0f );
    g.setColour( m_skin.m_accent.withAlpha( 0.6f ) );
    g.drawRoundedRectangle( p.reduced( 0.5f ), m_skin.m_radius * 2.0f, 1.2f );
}

void CommandPalette::mouseDown( const juce::MouseEvent &e )
{
    if ( !panel().contains( e.getPosition() ) ) close();
}

void CommandPalette::filter()
{
    // The words, and a number after them (a control's value).
    juce::StringArray words = juce::StringArray::fromTokens( m_search.getText().toLowerCase().trim(), " ", "" );
    words.removeEmptyStrings();
    m_number.clear();
    if ( !words.isEmpty() && words[words.size() - 1].containsOnly( "0123456789.-" ) &&
         words[words.size() - 1].containsAnyOf( "0123456789" ) )
    {
        m_number = words[words.size() - 1];
        words.remove( words.size() - 1 );
    }
    std::vector<std::pair<float, int>> scored;
    // What the words make (an instrument's own): first, in their own order.
    m_all.resize( m_fixed );
    if ( m_fromWords && !words.isEmpty() )
        for ( Command &c : m_fromWords( words ) )
        {
            scored.emplace_back( 1000.0f - static_cast<float>( scored.size() ), static_cast<int>( m_all.size() ) );
            m_all.push_back( std::move( c ) );
        }
    for ( size_t i = 0; i < m_fixed; ++i )
    {
        const Command &c = m_all[i];
        if ( words.isEmpty() )
        {   // nothing typed: the actions and the pages
            if ( c.m_kind == "Action" || c.m_kind == "Page" )
                scored.emplace_back( c.m_kind == "Action" ? 1.0f : 0.5f, static_cast<int>( i ) );
            continue;
        }
        const juce::String title = c.m_title.toLowerCase(), detail = c.m_detail.toLowerCase();
        float              s = 0.0f;
        bool               all = true;
        for ( const juce::String &w : words )
        {
            const float a = fitWord( title, w ), d = fitWord( detail, w ) * 0.4f;
            if ( a <= 0.0f && d <= 0.0f )
            {
                all = false;
                break;
            }
            s += std::max( a, d );
        }
        if ( !all ) continue;
        if ( !m_number.isEmpty() && !c.m_takesNumber ) s *= 0.5f;   // a number asks for a control
        s += c.m_kind == "Action"    ? 0.3f
             : c.m_kind == "Page"    ? 0.25f
             : c.m_kind == "Control" ? 0.2f
             : c.m_kind == "Yours"   ? 0.1f
                                     : 0.0f;
        s -= 0.002f * static_cast<float>( c.m_title.length() );   // the shorter of two equal fits
        scored.emplace_back( s, static_cast<int>( i ) );
    }
    std::stable_sort( scored.begin(), scored.end(), []( const auto &a, const auto &b ) { return a.first > b.first; } );
    m_shown.clear();
    for ( size_t i = 0; i < scored.size() && i < 80; ++i ) m_shown.push_back( scored[i].second );
    m_list.updateContent();
    m_list.selectRow( m_shown.empty() ? -1 : 0 );
    m_list.repaint();
}

void CommandPalette::run( int row )
{
    if ( row < 0 || row >= static_cast<int>( m_shown.size() ) ) return;
    const Command      c = m_all[static_cast<size_t>( m_shown[static_cast<size_t>( row )] )];
    const juce::String number = c.m_takesNumber ? m_number : juce::String();
    close();
    if ( c.m_run ) c.m_run( number );
}

int CommandPalette::getNumRows()
{
    return static_cast<int>( m_shown.size() );
}

void CommandPalette::paintListBoxItem( int row, juce::Graphics &g, int width, int height, bool selected )
{
    if ( row < 0 || row >= static_cast<int>( m_shown.size() ) ) return;
    const Command &c = m_all[static_cast<size_t>( m_shown[static_cast<size_t>( row )] )];
    if ( selected )
    {
        g.setColour( m_skin.m_accent.withAlpha( 0.18f ) );
        g.fillRoundedRectangle(
            juce::Rectangle<float>( 0.0f, 1.0f, static_cast<float>( width ), static_cast<float>( height - 2 ) ),
            m_skin.m_radius );
    }
    const juce::Colour kindColour = c.m_kind == "Control"  ? m_skin.family( FamilyE::SOURCE )
                                    : c.m_kind == "Action" ? m_skin.m_accent
                                    : c.m_kind == "Page"   ? m_skin.family( FamilyE::MOTION )
                                    : c.m_kind == "Yours"  ? m_skin.family( FamilyE::ENVELOPE )
                                                           : m_skin.family( FamilyE::SPACE );
    g.setColour( kindColour );
    g.setFont( font( 11.0f, true ) );
    g.drawText( c.m_kind.toUpperCase(), 8, 0, 70, height, juce::Justification::centredLeft );
    g.setColour( m_skin.m_ink );
    g.setFont( font( 14.0f ) );
    const juce::String title = c.m_takesNumber && m_number.isNotEmpty() ? c.m_title + "  = " + m_number : c.m_title;
    g.drawText( title, 82, 0, width / 2, height, juce::Justification::centredLeft );
    g.setColour( m_skin.m_dim );
    g.setFont( font( 12.0f ) );
    g.drawText( c.m_detail, 82 + width / 2, 0, width / 2 - 90, height, juce::Justification::centredRight );
}

void CommandPalette::listBoxItemClicked( int row, const juce::MouseEvent & )
{
    run( row );
}

void CommandPalette::returnKeyPressed( int row )
{
    run( row );
}

void CommandPalette::textEditorTextChanged( juce::TextEditor & )
{
    filter();
}

void CommandPalette::textEditorReturnKeyPressed( juce::TextEditor & )
{
    run( m_list.getSelectedRow() );
}

void CommandPalette::textEditorEscapeKeyPressed( juce::TextEditor & )
{
    close();
}

bool CommandPalette::keyPressed( const juce::KeyPress &key, juce::Component * )
{
    const int n = static_cast<int>( m_shown.size() );
    if ( n == 0 ) return false;
    if ( key.isKeyCode( juce::KeyPress::downKey ) )
    {
        m_list.selectRow( std::min( m_list.getSelectedRow() + 1, n - 1 ) );
        return true;
    }
    if ( key.isKeyCode( juce::KeyPress::upKey ) )
    {
        m_list.selectRow( std::max( m_list.getSelectedRow() - 1, 0 ) );
        return true;
    }
    if ( key.isKeyCode( juce::KeyPress::pageDownKey ) )
    {
        m_list.selectRow( std::min( m_list.getSelectedRow() + 10, n - 1 ) );
        return true;
    }
    if ( key.isKeyCode( juce::KeyPress::pageUpKey ) )
    {
        m_list.selectRow( std::max( m_list.getSelectedRow() - 10, 0 ) );
        return true;
    }
    return false;
}

// ======================================================================================================= info line

InfoLine::InfoLine( const Skin &skin, juce::Component &watched ) : m_skin( skin ), m_watched( watched )
{
    m_tracker.m_line = this;
    m_watched.addMouseListener( &m_tracker, true );
    setInterceptsMouseClicks( false, false );
    m_text = "Point at a control: what it is and what it does is told here.";
    startTimerHz( 5 );
}

InfoLine::~InfoLine()
{
    m_watched.removeMouseListener( &m_tracker );
}

void InfoLine::over( juce::Component *c )
{
    // The control under the mouse, or the nearest of its parents that is one.
    for ( juce::Component *p = c; p != nullptr && p != &m_watched; p = p->getParentComponent() )
    {
        const int    id = controlId( *p );
        juce::String name, text;
        if ( id >= 0 && m_describe && m_describe( id, name, text ) )
        {
            if ( id != m_id || name != m_name || text != m_text )
            {
                m_id = id;
                m_name = name;
                m_text = text;
                repaint();
            }
            return;
        }
        if ( auto *tip = dynamic_cast<juce::SettableTooltipClient *>( p ) )
        {
            const juce::String t = tip->getTooltip();
            if ( t.isNotEmpty() )
            {
                if ( m_id != -1 || m_name.isNotEmpty() || m_text != t )
                {
                    m_id = -1;
                    m_name.clear();
                    m_text = t;
                    repaint();
                }
                return;
            }
        }
    }
}

void InfoLine::showControl( int id )
{
    juce::String name, text;
    if ( !m_describe || !m_describe( id, name, text ) ) return;
    m_id = id;
    m_name = name;
    m_text = text;
    repaint();
}

void InfoLine::showText( const juce::String &text )
{
    m_id = -1;
    m_name.clear();
    m_text = text;
    repaint();
}

void InfoLine::timerCallback()
{
    if ( m_id < 0 || !m_describe ) return;
    juce::String name, text;
    if ( m_describe( m_id, name, text ) && ( text != m_text || name != m_name ) )
    {
        m_name = name;
        m_text = text;
        repaint();
    }
}

void InfoLine::paint( juce::Graphics &g )
{
    g.setColour( m_skin.m_group.withAlpha( 0.85f ) );
    g.fillRect( getLocalBounds() );
    g.setColour( m_skin.m_edge );
    g.drawHorizontalLine( 0, 0.0f, static_cast<float>( getWidth() ) );
    juce::AttributedString a;
    if ( m_name.isNotEmpty() )
        a.append( m_name + "    ", juce::FontOptions( 12.5f, juce::Font::bold ), m_skin.m_accent );
    a.append( m_text, juce::FontOptions( 12.5f ), m_skin.m_dim );
    a.setJustification( juce::Justification::centredLeft );
    a.setWordWrap( juce::AttributedString::none );
    a.draw( g, getLocalBounds().reduced( 12, 0 ).toFloat() );
}

// ============================================================================================================ tour

Tour::Tour( const Skin &skin ) : m_skin( skin )
{
    m_next.onClick = [this] { next(); };
    m_skip.onClick = [this] { finish(); };
    addAndMakeVisible( m_next );
    addAndMakeVisible( m_skip );
    setInterceptsMouseClicks( true, true );
    setVisible( false );
}

void Tour::start( std::vector<Step> steps )
{
    m_steps = std::move( steps );
    m_at = 0;
    if ( m_steps.empty() ) return;
    if ( auto *p = getParentComponent() ) setBounds( p->getLocalBounds() );
    setVisible( true );
    toFront( true );
    resized();
    repaint();
}

void Tour::next()
{
    if ( ++m_at >= m_steps.size() )
    {
        finish();
        return;
    }
    resized();
    repaint();
}

void Tour::finish()
{
    setVisible( false );
    m_steps.clear();
    if ( m_onDone ) m_onDone();
}

juce::Rectangle<int> Tour::hole() const
{
    if ( m_at >= m_steps.size() || m_steps[m_at].m_target == nullptr || !m_steps[m_at].m_target->isShowing() )
        return {};
    return getLocalArea( m_steps[m_at].m_target.getComponent(), m_steps[m_at].m_target->getLocalBounds() )
        .expanded( 6 );
}

juce::Rectangle<int> Tour::bubble() const
{
    const auto h = hole();
    const int  w = 440, ht = 156;
    if ( h.isEmpty() ) return { ( getWidth() - w ) / 2, ( getHeight() - ht ) / 2, w, ht };
    const int x = juce::jlimit( 12, std::max( 12, getWidth() - w - 12 ), h.getCentreX() - w / 2 );
    int       y = h.getBottom() + 14;
    if ( y + ht > getHeight() - 12 ) y = std::max( 12, h.getY() - ht - 14 );   // above it when there is no room below
    if ( y + ht > getHeight() - 12 || ( y < h.getBottom() && y + ht > h.getY() ) )
        y = juce::jlimit( 12, std::max( 12, getHeight() - ht - 12 ), h.getCentreY() - ht / 2 );
    return { x, y, w, ht };
}

void Tour::resized()
{
    const auto b = bubble();
    m_next.setButtonText( m_at + 1 >= m_steps.size() ? "Done" : "Next" );
    m_next.setBounds( b.getRight() - 92, b.getBottom() - 36, 80, 26 );
    m_skip.setBounds( b.getRight() - 180, b.getBottom() - 36, 80, 26 );
    m_skip.setVisible( m_at + 1 < m_steps.size() );
}

void Tour::paint( juce::Graphics &g )
{
    if ( m_at >= m_steps.size() ) return;
    const auto h = hole();
    // The window dimmed, the control left bright.
    juce::Path dim;
    dim.addRectangle( getLocalBounds().toFloat() );
    if ( !h.isEmpty() ) dim.addRoundedRectangle( h.toFloat(), 8.0f );
    dim.setUsingNonZeroWinding( false );
    g.setColour( m_skin.m_bg.withAlpha( 0.72f ) );
    g.fillPath( dim );
    if ( !h.isEmpty() )
    {
        g.setColour( m_skin.m_accent );
        g.drawRoundedRectangle( h.toFloat(), 8.0f, 2.0f );
    }
    const auto b = bubble().toFloat();
    g.setColour( m_skin.m_group.withAlpha( 1.0f ) );
    g.fillRoundedRectangle( b, 8.0f );
    g.setColour( m_skin.m_accent.withAlpha( 0.7f ) );
    g.drawRoundedRectangle( b.reduced( 0.5f ), 8.0f, 1.2f );
    const Step &s = m_steps[m_at];
    g.setColour( m_skin.m_accent );
    g.setFont( font( 15.0f, true ) );
    g.drawText( s.m_title, b.reduced( 14.0f, 10.0f ).withHeight( 22.0f ), juce::Justification::centredLeft );
    g.setColour( m_skin.m_dim );
    g.setFont( font( 11.0f ) );
    g.drawText( juce::String( static_cast<int>( m_at ) + 1 ) + " / " +
                    juce::String( static_cast<int>( m_steps.size() ) ),
                b.reduced( 14.0f, 10.0f ).withHeight( 22.0f ), juce::Justification::centredRight );
    g.setColour( m_skin.m_ink );
    g.setFont( font( 13.0f ) );
    g.drawFittedText( s.m_text,
                      b.reduced( 14.0f, 10.0f ).withTrimmedTop( 26.0f ).withTrimmedBottom( 36.0f ).toNearestInt(),
                      juce::Justification::topLeft, 5 );
}

// ================================================================================================= preset browser

namespace
{
constexpr int Recent = 40;   ///< the most presets the recent list keeps per bank
}   // namespace

/** @brief A row of the list (see PresetBrowser): the play mark (held: listen), the star, the name, the group. */
class PresetBrowser::Row final : public juce::Component
{
public:
    /**
     * @brief A row of @p owner.
     * @param[in,out] owner  the browser
     */
    explicit Row( PresetBrowser &owner ) : m_owner( owner ) {}
    /**
     * @brief Shows entry @p e (-1 none) at list row @p row.
     * @param[in] row       the list row
     * @param[in] e         the entry, -1 for none
     * @param[in] selected  true when the row is chosen
     */
    void show( int row, int e, bool selected )
    {
        m_row = row;
        m_entry = e;
        m_selected = selected;
        repaint();
    }
    /**
     * @brief The play mark, the star, the name, the group.
     * @param[in,out] g  the graphics context to draw into
     */
    void paint( juce::Graphics &g ) override
    {
        if ( m_entry < 0 ) return;
        const Skin  &skin = m_owner.m_skin;
        const Entry &e = m_owner.m_bank.m_entries[static_cast<size_t>( m_entry )];
        if ( m_selected )
        {
            g.setColour( skin.m_accent.withAlpha( 0.16f ) );
            g.fillRoundedRectangle( getLocalBounds().toFloat().reduced( 1.0f ), skin.m_radius );
        }
        if ( m_owner.m_bank.m_preview )
        {   // the play mark: a triangle, lit while held
            juce::Path tri;
            tri.addTriangle( 10.0f, 8.0f, 10.0f, static_cast<float>( getHeight() ) - 8.0f, 22.0f,
                             static_cast<float>( getHeight() ) * 0.5f );
            g.setColour( m_listening ? skin.m_accent : skin.m_dim );
            g.fillPath( tri );
        }
        const bool fav = m_owner.favourite( e.m_name );
        juce::Path star;
        star.addStar( { 40.0f, static_cast<float>( getHeight() ) * 0.5f }, 5, 3.0f, 7.0f );
        g.setColour( fav ? skin.family( FamilyE::ENVELOPE ) : skin.m_faint );
        if ( fav )
            g.fillPath( star );
        else
            g.strokePath( star, juce::PathStrokeType( 1.2f ) );
        g.setColour( e.m_yours ? skin.family( FamilyE::ENVELOPE ) : skin.m_ink );
        g.setFont( font( 14.0f ) );
        g.drawText( e.m_name, 56, 0, getWidth() / 2 - 56, getHeight(), juce::Justification::centredLeft );
        g.setColour( skin.m_dim );
        g.setFont( font( 12.0f ) );
        g.drawText( e.m_group + ( e.m_note.isNotEmpty() ? "   (" + e.m_note + ")" : juce::String() ), getWidth() / 2, 0,
                    getWidth() / 2 - 12, getHeight(), juce::Justification::centredLeft );
    }
    /**
     * @brief Listens (the play mark), stars, or chooses the row.
     * @param[in] e  the mouse event
     */
    void mouseDown( const juce::MouseEvent &e ) override
    {
        if ( m_entry < 0 ) return;
        m_owner.m_list.selectRow( m_row );
        const Entry en = m_owner.m_bank.m_entries[static_cast<size_t>( m_entry )];
        if ( e.x < 30 && m_owner.m_bank.m_preview )
        {
            m_listening = true;
            m_owner.m_bank.m_preview( en );
            repaint();
        }
        else if ( e.x < 52 )
        {
            m_owner.setFavourite( en.m_name, !m_owner.favourite( en.m_name ) );
            repaint();
            if ( m_owner.m_categories[static_cast<size_t>( m_owner.m_category )].m_kind == 1 ) m_owner.filter();
        }
    }
    /** @brief Back to the sound before the listening. */
    void mouseUp( const juce::MouseEvent & ) override
    {
        if ( !m_listening ) return;
        m_listening = false;
        if ( m_owner.m_bank.m_endPreview ) m_owner.m_bank.m_endPreview();
        repaint();
    }
    /**
     * @brief Takes it.
     * @param[in] e  the mouse event
     */
    void mouseDoubleClick( const juce::MouseEvent &e ) override
    {
        if ( e.x >= 52 ) m_owner.choose( m_row );
    }

private:
    PresetBrowser &m_owner;               ///< whose
    int            m_row = -1;            ///< the list's row
    int            m_entry = -1;          ///< the entry shown
    bool           m_selected = false;    ///< chosen
    bool           m_listening = false;   ///< its play mark is held
};

/** @brief The categories' list (see PresetBrowser). */
class PresetBrowser::Categories final : public juce::ListBoxModel
{
public:
    /**
     * @brief The categories of @p owner.
     * @param[in,out] owner  the browser
     */
    explicit Categories( PresetBrowser &owner ) : m_owner( owner ) {}
    /**
     * @brief How many.
     * @return how many categories there are
     */
    int getNumRows() override { return static_cast<int>( m_owner.m_categories.size() ); }
    /**
     * @brief One: a heading for a section, indented for a group.
     * @param[in]     row       the row
     * @param[in,out] g         the graphics context to draw into
     * @param[in]     width     the row's width
     * @param[in]     height    the row's height
     * @param[in]     selected  true when the row is chosen
     */
    void paintListBoxItem( int row, juce::Graphics &g, int width, int height, bool selected ) override
    {
        if ( row < 0 || row >= getNumRows() ) return;
        const Skin     &skin = m_owner.m_skin;
        const Category &c = m_owner.m_categories[static_cast<size_t>( row )];
        if ( selected )
        {
            g.setColour( skin.m_accent.withAlpha( 0.18f ) );
            g.fillRoundedRectangle(
                juce::Rectangle<float>( 0.0f, 0.0f, static_cast<float>( width ), static_cast<float>( height ) ),
                skin.m_radius );
        }
        const bool heading = c.m_kind <= 4;
        g.setColour( heading ? skin.m_ink : skin.m_dim );
        g.setFont( font( heading ? 13.0f : 12.0f, heading ) );
        g.drawText( c.m_title, c.m_kind == 5 ? 18 : 6, 0, width - 20, height, juce::Justification::centredLeft );
    }
    /**
     * @brief Chooses it.
     * @param[in] row  the row chosen
     */
    void selectedRowsChanged( int row ) override
    {
        if ( row < 0 ) return;
        m_owner.m_category = row;
        m_owner.filter();
    }

private:
    PresetBrowser &m_owner;   ///< whose
};

PresetBrowser::PresetBrowser( const Skin &skin, const juce::String &app )
    : m_skin( skin ), m_app( app ), m_list( {}, this )
{
    m_search.setFont( juce::FontOptions( 16.0f ) );
    m_search.setTextToShowWhenEmpty( "A name or a group ...", m_skin.m_dim );
    m_search.setColour( juce::TextEditor::backgroundColourId, m_skin.m_raised );
    m_search.setColour( juce::TextEditor::textColourId, m_skin.m_ink );
    m_search.setColour( juce::TextEditor::outlineColourId, m_skin.m_edge );
    m_search.addListener( this );
    addAndMakeVisible( m_search );
    m_title.setColour( juce::Label::textColourId, m_skin.m_accent );
    m_title.setFont( juce::FontOptions( 15.0f, juce::Font::bold ) );
    addAndMakeVisible( m_title );
    m_categoryModel = std::make_unique<Categories>( *this );
    m_categoryList = std::make_unique<juce::ListBox>( "categories", m_categoryModel.get() );
    m_categoryList->setRowHeight( 22 );
    m_categoryList->setColour( juce::ListBox::backgroundColourId, juce::Colours::transparentBlack );
    addAndMakeVisible( *m_categoryList );
    m_take.onClick = [this] { choose( m_list.getSelectedRow() ); };
    m_close.onClick = [this] { close(); };
    addAndMakeVisible( m_take );
    addAndMakeVisible( m_close );
    m_count.setColour( juce::Label::textColourId, m_skin.m_dim );
    m_count.setFont( juce::FontOptions( 12.0f ) );
    addAndMakeVisible( m_count );
    m_list.setRowHeight( 28 );
    m_list.setColour( juce::ListBox::backgroundColourId, juce::Colours::transparentBlack );
    addAndMakeVisible( m_list );
    setWantsKeyboardFocus( true );
    setVisible( false );
}

PresetBrowser::~PresetBrowser() = default;

bool PresetBrowser::favourite( const juce::String &name ) const
{
    return m_favourites.contains( name );
}

void PresetBrowser::setFavourite( const juce::String &name, bool on )
{
    if ( on )
        m_favourites.addIfNotAlreadyThere( name );
    else
        m_favourites.removeString( name );
    Settings::of( m_app ).setValue( "favourites." + m_bank.m_key, m_favourites.joinIntoString( "\n" ) );
}

void PresetBrowser::played( const juce::String &app, const juce::String &key, const juce::String &name )
{
    Settings         &s = Settings::of( app );
    juce::StringArray recent = juce::StringArray::fromLines( s.value( "recent." + key ) );
    recent.removeEmptyStrings();
    recent.removeString( name );
    recent.insert( 0, name );
    while ( recent.size() > Recent ) recent.remove( recent.size() - 1 );
    s.setValue( "recent." + key, recent.joinIntoString( "\n" ), false );
}

void PresetBrowser::open( Bank bank )
{
    m_bank = std::move( bank );
    m_favourites = juce::StringArray::fromLines( Settings::of( m_app ).value( "favourites." + m_bank.m_key ) );
    m_favourites.removeEmptyStrings();
    m_title.setText( m_bank.m_title, juce::dontSendNotification );
    m_categories.clear();
    m_categories.push_back( { "All", 0, {} } );
    m_categories.push_back( { "Favourites", 1, {} } );
    m_categories.push_back( { "Recently played", 2, {} } );
    bool yours = false;
    for ( const Entry &e : m_bank.m_entries ) yours = yours || e.m_yours;
    if ( yours ) m_categories.push_back( { "Yours", 3, {} } );
    // The sections and their groups, in the bank's order.
    juce::String section, group;
    for ( const Entry &e : m_bank.m_entries )
    {
        if ( e.m_yours ) continue;
        if ( e.m_section.isNotEmpty() && e.m_section != section )
        {
            section = e.m_section;
            m_categories.push_back( { section, 4, section } );
            group.clear();   // a section begins its groups afresh
        }
        if ( e.m_group != group )
        {   // under its section: the same group name in another section is another group (the DJ sets' arcs)
            group = e.m_group;
            m_categories.push_back( { group, 5, group, e.m_section } );
        }
    }
    m_categoryList->updateContent();
    m_category = 0;
    m_categoryList->selectRow( 0, true, true );
    m_search.clear();
    if ( auto *p = getParentComponent() ) setBounds( p->getLocalBounds() );
    filter();
    setVisible( true );
    toFront( true );
    m_search.grabKeyboardFocus();
}

void PresetBrowser::close()
{
    if ( m_bank.m_endPreview ) m_bank.m_endPreview();
    setVisible( false );
    if ( auto *p = getParentComponent() ) p->grabKeyboardFocus();
}

void PresetBrowser::filter()
{
    const juce::String q = m_search.getText().trim().toLowerCase();
    const Category     c = m_categories.empty() ? Category{} : m_categories[static_cast<size_t>( m_category )];
    juce::StringArray  recent;
    if ( c.m_kind == 2 )
    {
        recent = juce::StringArray::fromLines( Settings::of( m_app ).value( "recent." + m_bank.m_key ) );
        recent.removeEmptyStrings();
    }
    m_shown.clear();
    for ( size_t i = 0; i < m_bank.m_entries.size(); ++i )
    {
        const Entry &e = m_bank.m_entries[i];
        if ( c.m_kind == 1 && !favourite( e.m_name ) ) continue;
        if ( c.m_kind == 2 && !recent.contains( e.m_name ) ) continue;
        if ( c.m_kind == 3 && !e.m_yours ) continue;
        if ( c.m_kind == 4 && ( e.m_yours || e.m_section != c.m_which ) ) continue;
        if ( c.m_kind == 5 &&
             ( e.m_yours || e.m_group != c.m_which || ( c.m_section.isNotEmpty() && e.m_section != c.m_section ) ) )
            continue;
        if ( q.isNotEmpty() && !e.m_name.toLowerCase().contains( q ) && !e.m_group.toLowerCase().contains( q ) )
            continue;
        m_shown.push_back( static_cast<int>( i ) );
    }
    if ( c.m_kind == 2 )   // the recent ones newest first
        std::stable_sort( m_shown.begin(), m_shown.end(),
                          [&]( int a, int b )
                          {
                              return recent.indexOf( m_bank.m_entries[static_cast<size_t>( a )].m_name ) <
                                     recent.indexOf( m_bank.m_entries[static_cast<size_t>( b )].m_name );
                          } );
    m_list.updateContent();
    m_list.selectRow( m_shown.empty() ? -1 : 0 );
    m_list.repaint();
    m_count.setText( juce::String( static_cast<int>( m_shown.size() ) ) + " presets", juce::dontSendNotification );
}

void PresetBrowser::choose( int row )
{
    if ( row < 0 || row >= static_cast<int>( m_shown.size() ) ) return;
    const Entry e = m_bank.m_entries[static_cast<size_t>( m_shown[static_cast<size_t>( row )] )];
    played( m_app, m_bank.m_key, e.m_name );
    if ( m_bank.m_choose ) m_bank.m_choose( e );
    close();
}

juce::Rectangle<int> PresetBrowser::panel() const
{
    const int w = std::min( 880, getWidth() - 40 ), h = std::min( 600, getHeight() - 80 );
    return { ( getWidth() - w ) / 2, 50, w, h };
}

void PresetBrowser::resized()
{
    auto r = panel().reduced( 12 );
    auto top = r.removeFromTop( 34 );
    m_close.setBounds( top.removeFromRight( 80 ).reduced( 2 ) );
    m_take.setBounds( top.removeFromRight( 80 ).reduced( 2 ) );
    top.removeFromRight( 8 );
    m_title.setBounds( top.removeFromLeft( 200 ) );
    m_search.setBounds( top );
    r.removeFromTop( 8 );
    auto left = r.removeFromLeft( 210 );
    m_count.setBounds( left.removeFromBottom( 20 ) );
    m_categoryList->setBounds( left );
    r.removeFromLeft( 10 );
    m_list.setBounds( r );
}

void PresetBrowser::paint( juce::Graphics &g )
{
    g.fillAll( m_skin.m_bg.withAlpha( 0.55f ) );
    const auto p = panel().toFloat();
    g.setColour( m_skin.m_group.withAlpha( 1.0f ) );
    g.fillRoundedRectangle( p, m_skin.m_radius * 2.0f );
    g.setColour( m_skin.m_accent.withAlpha( 0.5f ) );
    g.drawRoundedRectangle( p.reduced( 0.5f ), m_skin.m_radius * 2.0f, 1.2f );
}

void PresetBrowser::mouseDown( const juce::MouseEvent &e )
{
    if ( !panel().contains( e.getPosition() ) ) close();
}

bool PresetBrowser::keyPressed( const juce::KeyPress &key )
{
    if ( key.isKeyCode( juce::KeyPress::escapeKey ) )
    {
        close();
        return true;
    }
    if ( key.isKeyCode( juce::KeyPress::returnKey ) )
    {
        choose( m_list.getSelectedRow() );
        return true;
    }
    return false;
}

int PresetBrowser::getNumRows()
{
    return static_cast<int>( m_shown.size() );
}

juce::Component *PresetBrowser::refreshComponentForRow( int row, bool selected, juce::Component *existing )
{
    auto *r = dynamic_cast<Row *>( existing );
    if ( r == nullptr )
    {
        delete existing;
        r = new Row( *this );
    }
    r->show( row, row >= 0 && row < static_cast<int>( m_shown.size() ) ? m_shown[static_cast<size_t>( row )] : -1,
             selected );
    return r;
}

// ============================================================================================= modulation at the knob

namespace
{

/** @brief One row on a knob in its menu: its source, its depth to drag, a button that removes it. */
class ModRowItem final : public juce::PopupMenu::CustomComponent
{
public:
    /**
     * @brief Row @p row of the matrix @p access works on.
     * @param[in] access  how the matrix is reached
     * @param[in] row     the matrix row
     */
    ModRowItem( ModAccess access, const ModAccess::Row &row )
        : juce::PopupMenu::CustomComponent( false ), m_access( std::move( access ) ), m_row( row.m_index )
    {
        m_name.setText( row.m_name, juce::dontSendNotification );
        m_name.setColour( juce::Label::textColourId, row.m_colour );
        m_name.setFont( juce::FontOptions( 13.0f ) );
        m_name.setInterceptsMouseClicks( false, false );
        m_depth.setSliderStyle( juce::Slider::LinearHorizontal );
        m_depth.setTextBoxStyle( juce::Slider::TextBoxRight, false, 52, 18 );
        m_depth.setRange( -1.0, 1.0, 0.001 );
        m_depth.setValue( row.m_depth, juce::dontSendNotification );
        m_depth.setDoubleClickReturnValue( true, 0.0 );
        m_depth.setColour( juce::Slider::trackColourId, row.m_colour );
        m_depth.setTooltip( "How far it moves the knob, of the knob's whole range (negative: the other way)" );
        m_depth.onValueChange = [this]
        {
            if ( m_access.m_setDepth ) m_access.m_setDepth( m_row, static_cast<float>( m_depth.getValue() ) );
        };
        m_depth.onDragStart = [this]
        {
            if ( m_access.m_begin ) m_access.m_begin( "Modulation depth" );
        };   // one step for the whole drag
        m_depth.onDragEnd = [this]
        {
            if ( m_access.m_end ) m_access.m_end();
        };
        m_remove.setTooltip( "Takes this row out of the matrix" );
        m_remove.onClick = [this]
        {
            if ( m_access.m_remove ) m_access.m_remove( m_row );
            triggerMenuItem();   // the menu closes: the row is gone
        };
        addAndMakeVisible( m_name );
        addAndMakeVisible( m_depth );
        addAndMakeVisible( m_remove );
    }
    /**
     * @brief A row's size in the menu.
     * @param[out] w  receives the width
     * @param[out] h  receives the height
     */
    void getIdealSize( int &w, int &h ) override
    {
        w = 340;
        h = 30;
    }
    /** @brief The name left, the depth, the button right. */
    void resized() override
    {
        auto r = getLocalBounds().reduced( 6, 3 );
        m_remove.setBounds( r.removeFromRight( 24 ) );
        m_name.setBounds( r.removeFromLeft( 120 ) );
        m_depth.setBounds( r.reduced( 4, 0 ) );
    }

private:
    ModAccess        m_access;          ///< the matrix
    int              m_row;             ///< which row
    juce::Label      m_name;            ///< its source
    juce::Slider     m_depth;           ///< its depth
    juce::TextButton m_remove{ "x" };   ///< takes it out
};

}   // namespace

void ModulationAtKnob::install( ControlActions &actions, LiveRings *rings )
{
    actions.m_more = [this]( juce::PopupMenu &m, int id ) { addMenuItems( m, id ); };
    actions.m_altDrag = [this]( int id, const juce::MouseEvent &e, int phase ) { return altDrag( id, e, phase ); };
    if ( rings != nullptr && m_access.m_arcs ) rings->setModArcs( m_access.m_arcs );
}

void ModulationAtKnob::addMenuItems( juce::PopupMenu &m, int id )
{
    if ( !m_access.m_sources ) return;
    const std::vector<ModAccess::Source> sources = m_access.m_sources( id );
    if ( sources.empty() ) return;
    m.addSeparator();
    m.addSectionHeader( "Modulation" );
    if ( m_access.m_rowsOn )
        for ( const ModAccess::Row &r : m_access.m_rowsOn( id ) )
            m.addCustomItem( 0x7f000000 + r.m_index, std::make_unique<ModRowItem>( m_access, r ), nullptr, r.m_name );
    juce::PopupMenu with;
    juce::String    section;
    for ( const ModAccess::Source &s : sources )
    {
        if ( s.m_section != section )
        {
            section = s.m_section;
            if ( section.isNotEmpty() ) with.addSectionHeader( section );
        }
        with.addItem( s.m_name,
                      [this, id, src = s.m_id]
                      {
                          if ( m_access.m_add && !m_access.m_add( id, src ) && m_access.m_say )
                              m_access.m_say( "Every row of the matrix is taken: free one first." );
                      } );
    }
    m.addSubMenu( "Modulate with", with );
}

bool ModulationAtKnob::altDrag( int id, const juce::MouseEvent &e, int phase )
{
    if ( phase == 0 )
    {
        m_row = -1;
        if ( !m_access.m_rowsOn ) return false;
        const std::vector<ModAccess::Row> rows = m_access.m_rowsOn( id );
        if ( rows.empty() ) return false;   // nothing on it: the knob turns as usual
        m_row = rows.back().m_index;
        m_depth0 = rows.back().m_depth;
        if ( m_access.m_begin ) m_access.m_begin( "Modulation depth" );   // one step for the whole drag
        return true;
    }
    if ( m_row < 0 ) return false;
    // Up deepens, down the other way: 200 pixels the whole range.
    const float d =
        juce::jlimit( -1.0f, 1.0f, m_depth0 - static_cast<float>( e.getDistanceFromDragStartY() ) / 200.0f );
    if ( m_access.m_setDepth ) m_access.m_setDepth( m_row, d );
    if ( m_access.m_say )
        m_access.m_say(
            "Modulation depth " + juce::String( d, 2 ) +
            ( phase == 2 ? juce::String() : juce::String( "   (Alt-drag: up deeper, down the other way)" ) ) );
    if ( phase == 2 )
    {
        m_row = -1;
        if ( m_access.m_end ) m_access.m_end();
    }
    return true;
}

}   // namespace frame
