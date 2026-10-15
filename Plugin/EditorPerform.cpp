/**
 * @file EditorPerform.cpp
 * @brief The perform page (EditorPerform.h).
 * @author Rene Weller
 */
#include "EditorPerform.h"
#include "EditorTheme.h"
#include "PluginEditor.h"

using namespace eph;

namespace
{
const juce::Colour Ink = ephui::colour::Ink;        ///< text: parchment
const juce::Colour Dim = ephui::colour::Dim;        ///< names, secondary text
const juce::Colour Accent = ephui::colour::Amber;   ///< the sun: the accent
}   // namespace

PerformPage::PerformPage( EphemerisProcessor &p, std::unique_ptr<ParamPage> params )
    : m_proc( p ), m_params( std::move( params ) )
{
    addAndMakeVisible( *m_params );
    const ParamStore &s = m_proc.store();
    for ( int index : { perform::FILTER, perform::TRANSPOSE, perform::HOLD, perform::THROW } )
    {
        const int id = s.id( ModuleE::PERFORM, 0, index );
        m_ids.push_back( id );
        auto *label = m_bindings.add( new juce::Label() );
        label->setColour( juce::Label::textColourId, Ink );
        addAndMakeVisible( label );
        auto *button = m_learn.add( new frame::IconButton( frame::IconButton::IconE::MIDI_LEARN,
                                                           "MIDI learn: binds the next controller that moves to " +
                                                               juce::String( s.desc( id ).m_name ) ) );
        button->setColour( juce::TextButton::buttonOnColourId, Accent.withAlpha( 0.45f ) );
        button->onClick = [this, id]
        {
            m_proc.learn( m_proc.learning() == id ? -1 : id );
            timerCallback();
        };
        addAndMakeVisible( button );
    }
    timerCallback();
    startTimerHz( 5 );
}

PerformPage::~PerformPage()
{
    stopTimer();
}

void PerformPage::timerCallback()
{
    // The headset's box: shown while one sends (or always, as the settings say), its hands live.
    const bool hs = m_proc.headset().shown( frame::Settings::of( "Ephemeris" ).headset() );
    if ( hs != m_headset )
    {
        m_headset = hs;
        resized();
        repaint();
    }
    if ( m_headset ) repaint( m_headsetArea );
    const ParamStore &s = m_proc.store();
    for ( size_t i = 0; i < m_ids.size(); ++i )
    {
        const int    id = m_ids[i];
        const int    cc = m_proc.controllerFor( id );
        const bool   learning = m_proc.learning() == id;
        juce::String text = juce::String( s.desc( id ).m_name ) + ": ";
        if ( learning )
            text << "move a controller ...";
        else if ( cc >= 0 )
            text << "CC " << cc;
        else
            text << "not bound";
        if ( id == s.id( ModuleE::PERFORM, 0, perform::TRANSPOSE ) ) text << "  (and the keys)";
        m_bindings[static_cast<int>( i )]->setText( text, juce::dontSendNotification );
        m_bindings[static_cast<int>( i )]->setColour( juce::Label::textColourId, learning ? Accent : Ink );
        m_learn[static_cast<int>( i )]->setToggleState( learning, juce::dontSendNotification );
        m_learn[static_cast<int>( i )]->show( frame::IconButton::IconE::MIDI_LEARN,
                                              learning ? juce::String( "Cancel: bind nothing" )
                                                       : "MIDI learn: binds the next controller that moves to " +
                                                             juce::String( s.desc( id ).m_name ) );
    }
}

void PerformPage::resized()
{
    auto r = getLocalBounds();
    m_controlsHeight = m_params->heightFor( r.getWidth() );   // the controls' group as tall as it draws itself
    m_params->setBounds( r.removeFromTop( m_controlsHeight ) );
    r.removeFromTop( 10 );
    r = r.reduced( 16, 0 );
    for ( int i = 0; i < m_bindings.size(); ++i )
    {
        auto row = r.removeFromTop( 30 );
        m_learn[i]->setBounds( row.removeFromLeft( frame::IconWidth ).reduced( 3 ) );
        row.removeFromLeft( 12 );
        m_bindings[i]->setBounds( row.withWidth( std::min( row.getWidth(), 420 ) ) );
    }
    m_headsetArea = m_headset ? juce::Rectangle<int>( r.getRight() - 380,
                                                      getLocalBounds().getY() + m_controlsHeight + 10, 380, 190 )
                              : juce::Rectangle<int>();
}

void PerformPage::paint( juce::Graphics &g )
{
    auto r = getLocalBounds().reduced( 16, 0 );
    r.removeFromTop( m_controlsHeight + 10 + 30 * m_bindings.size() + 12 );
    g.setColour( Dim );
    g.setFont( juce::Font( juce::FontOptions( 13.0f ) ) );
    g.drawFittedText(
        "A MIDI keyboard's keys transpose the rows by their distance from middle C (C3 plays as composed); "
        "the transposition holds until the next key. With Keyboard Plays on a voice the keys play that voice "
        "instead (Replace leaves out its composed notes, Layer plays over them; Composer off leaves out every "
        "composed note); Lower Keys Play puts a second voice under Split At, Scale Lock keeps the keys in the "
        "piece's scale, the velocity curve shapes the touch. Controller 74 (brightness) grabs the rows' filters, the "
        "expression pedal throws the echo, the sustain pedal holds the composed moves on the knobs -- as in every "
        "generator. Every control can be learned for any controller (a right click on it); the bindings are "
        "saved with the set.",
        r.removeFromTop( 80 ).withTrimmedRight( m_headset ? 400 : 0 ), juce::Justification::topLeft, 5 );
    if ( m_headset && !m_headsetArea.isEmpty() )   // the headset (the frame): what the hands do, and how they stand now
        frame::drawHeadsetBox( g, m_headsetArea, ephui::skin(), m_proc.headset(), "hold the moves / let go", {} );
}
