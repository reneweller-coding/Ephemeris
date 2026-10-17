/**
 * @file PluginEditor.cpp
 * @brief The plugin's panel.
 * @author Rene Weller
 */
#include "PluginEditor.h"
#include "EditorMixer.h"
#include "EditorOrrery.h"
#include "EditorPerform.h"
#include "EditorGestures.h"
#include "EditorStyle.h"
#include "EditorTheme.h"
#include "eph/compose/Composer.h"
#include "eph/FactorySets.h"
#include "eph/Presets.h"
#include "EphemerisData.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <cstdlib>

using namespace eph;

namespace
{

const juce::Colour Back = ephui::colour::Bg;        ///< the window: midnight
const juce::Colour Panel = ephui::colour::Panel;    ///< a page (the orrery shows faintly through it)
const juce::Colour Ink = ephui::colour::Ink;        ///< text: parchment
const juce::Colour Dim = ephui::colour::Dim;        ///< names, secondary text
const juce::Colour Accent = ephui::colour::Amber;   ///< the sun: the accent

/**
 * @brief Colour of a section by the first word of its marker: the families' colours, darkened -- the spaces (the
 *        atmosphere, the bridges, the fade) in the room's blue, the machine's stages from the sources' amber through the
 *        filter's copper at the peak, the breakdown in the teal of what moves by itself.
 * @param[in] name  the marker's text
 * @return the section's colour
 */
juce::Colour sectionColour( const juce::String &name )
{
    using ephui::Family;
    const juce::String n = name.fromLastOccurrenceOf( ": ", false, false );
    auto tone = []( Family f, float k ) { return ephui::familyColour( f ).interpolatedWith( ephui::colour::Bg, k ); };
    if ( n.startsWith( "Atmo" ) || n.startsWith( "Ausklang" ) || n.startsWith( "Zwischenspiel" ) )
        return tone( Family::SPACE, 0.72f );
    if ( n.startsWith( "Bruecke" ) ) return tone( Family::SPACE, 0.64f );
    if ( n.startsWith( "Einsatz" ) ) return tone( Family::SOURCE, 0.78f );
    if ( n.startsWith( "Aufbau" ) ) return tone( Family::SOURCE, 0.68f );
    if ( n.startsWith( "Lead" ) ) return tone( Family::SOURCE, 0.56f );
    if ( n.startsWith( "Hoehepunkt" ) ) return tone( Family::FILTER, 0.52f );
    if ( n.startsWith( "Abbau" ) ) return tone( Family::MOTION, 0.66f );
    return tone( Family::SPACE, 0.8f );
}

/**
 * @brief A synth's name in the presets' folder and the browser: its parameters' key without the lane's number ("bass",
 *        "perc").
 * @param[in] s  the parameters
 * @param[in] m  the synth
 * @return its name for the presets
 */
juce::String synthKey( const ParamStore &s, ModuleE m )
{
    const std::string &k = s.key( s.id( m, 0, 0 ) );
    return juce::String( k.substr( 0, k.find( '.' ) ) ).trimCharactersAtEnd( "0123456789" );
}

/**
 * @brief "perc3.decay" -> "Perc 3", "kick.tune" -> "Kick": the module instance a store id belongs to, for the palette
 *        and the info line.
 * @param[in] s   the parameters
 * @param[in] id  the store id
 * @return the module instance's label
 */
juce::String moduleLabel( const ParamStore &s, int id )
{
    const std::string &k = s.key( id );
    const juce::String p( k.substr( 0, k.find( '.' ) ) );
    const juce::String name = p.trimCharactersAtEnd( "0123456789" );
    const juce::String digits = p.substring( name.length() );
    const juce::String nice = name.substring( 0, 1 ).toUpperCase() + name.substring( 1 );
    return digits.isEmpty() ? nice : nice + " " + digits;
}

/**
 * @brief Whether the knobs stand where @p values put them (within a thousandth of each one's range).
 * @param[in] s       the parameters
 * @param[in] values  the knobs and their values
 * @return true when every knob stands there
 */
bool knobsAt( const ParamStore &s, const std::vector<std::pair<int, float>> &values )
{
    for ( const auto &[id, v] : values )
    {
        const ParamDesc &d = s.desc( id );
        if ( std::abs( s.get( id ) - v ) > 0.001f * std::max( 1.0e-6f, d.m_maxValue - d.m_minValue ) ) return false;
    }
    return true;
}

/**
 * @brief The small tabs of a page: the page itself, or the first SubTabs inside it.
 * @param[in,out] c  the page
 * @return its small tabs, null when it has none
 */
frame::SubTabs *findSubTabs( juce::Component *c )
{
    if ( c == nullptr ) return nullptr;
    if ( auto *subs = dynamic_cast<frame::SubTabs *>( c ) ) return subs;
    for ( auto *child : c->getChildren() )
        if ( auto *subs = findSubTabs( child ) ) return subs;
    return nullptr;
}

/**
 * @brief The knobs of menu id @p menuId of synth @p m on instance @p instance (a factory preset's index + 1, or 5001 +
 *        one of the player's).
 * @param[in,out] p         the processor
 * @param[in]     m         the synth
 * @param[in]     instance  its instance
 * @param[in]     menuId    the menu id
 * @return (parameter id, value) of the preset's knobs
 */
std::vector<std::pair<int, float>> knobsOf( EphemerisProcessor &p, ModuleE m, int instance, int menuId )
{
    std::vector<std::pair<int, float>> out;
    const ParamStore                  &s = p.store();
    const SoundPreset                 *preset = nullptr;
    std::vector<SoundPreset>           user;
    if ( menuId > 5000 )
    {
        user = p.userPresets( m );
        if ( menuId - 5001 < static_cast<int>( user.size() ) ) preset = &user[static_cast<size_t>( menuId - 5001 )];
    }
    else
    {
        const std::vector<SoundPreset> &list = factoryPresets( m );
        if ( menuId >= 1 && menuId <= static_cast<int>( list.size() ) )
            preset = &list[static_cast<size_t>( menuId - 1 )];
    }
    if ( preset != nullptr )
        for ( const auto &[k, v] : presetKnobs( m, *preset ) ) out.emplace_back( s.id( m, instance, k ), v );
    return out;
}

}   // namespace

// ---------------------------------------------------------------------------------------------------

ParamPage::ParamPage( EphemerisProcessor &p, std::vector<std::pair<ModuleE, int>> groups, int instances )
    : m_proc( p ), m_live( [this]( int id ) { return m_proc.playedNormalised( id ); } ),
      m_groups( std::move( groups ) ), m_instances( instances )
{
    m_actions.m_learn = [this]( int id ) { m_proc.learn( id ); };
    m_actions.m_controllerFor = [this]( int id ) { return m_proc.controllerFor( id ); };
    m_actions.m_forget = [this]( int id ) { m_proc.forget( id ); };
    m_actions.m_reset = [this]( int id ) { m_proc.resetToDefault( id ); };
    m_actions.m_describe = [this]( int id )
    { return juce::String( m_proc.store().desc( id ).m_name ) + "  (" + m_proc.store().key( id ) + ")"; };
    // The modulation at the knob (15.10.2026, Frame.h): the voice's matrix rows on its knobs, as arcs round them and in
    // their right-click menus; an Alt-drag sets a row's depth.
    {
        frame::ModAccess a = frame::slotMatrix(
            m_proc.store(), [this]( int id, float v ) { m_proc.setFromUi( id, v ); }, [this]( const juce::String &what )
            { m_proc.beginStep( what ); }, [this] { m_proc.endStep(); }, ephui::skin() );
        a.m_say = [this]( const juce::String &text )
        {
            if ( auto *ed = findParentComponentOfClass<EphemerisEditor>() ) ed->say( text );
        };
        m_modulation = std::make_unique<frame::ModulationAtKnob>( std::move( a ) );
        m_modulation->install( m_actions, &m_live );
    }
    if ( m_instances > 1 )
    {
        for ( int i = 0; i < m_instances; ++i ) m_instance.addItem( juce::String( i + 1 ), i + 1 );
        m_instance.setSelectedId( 1, juce::dontSendNotification );
        m_instance.onChange = [this]
        {
            build();
            resized();
        };
        addAndMakeVisible( m_instance );
    }
    // The synth's factory presets, in their groups (a submenu each), where the page is a synth's.
    const ModuleE                   m = m_groups.empty() ? ModuleE::COUNT : m_groups.front().first;
    const std::vector<SoundPreset> &list = factoryPresets( m );
    if ( !list.empty() )
    {
        m_presetModule = m;
        m_presetCount = static_cast<int>( list.size() );
        fillPresets();
        m_preset.onChange = [this]
        {
            const int id = m_preset.getSelectedId();
            if ( id > 5000 && id - 5001 < static_cast<int>( m_user.size() ) )
            {
                m_presetIndex = -1;
                const int one = m_instances > 1 ? m_instance.getSelectedId() - 1 : 0;
                for ( int i = 0; i < m_instances; ++i )
                    if ( i == one || m_allRows.getToggleState() )
                        m_proc.applyPresetValues( m_presetModule, i, m_user[static_cast<size_t>( id - 5001 )] );
                chosen( id );
            }
            else if ( id > 0 )
            {
                choosePreset( id - 1 );
            }
        };
        // Save...: the synth's knobs as they stand, under a name, into the user's presets.
        m_save.onClick = [this]
        {
            m_nameDialog = std::make_unique<juce::AlertWindow>(
                "Save preset", "A name for the sound as it is:", juce::MessageBoxIconType::NoIcon );
            m_nameDialog->addTextEditor( "name", "", "Name" );
            m_nameDialog->addButton( "Save", 1, juce::KeyPress( juce::KeyPress::returnKey ) );
            m_nameDialog->addButton( "Cancel", 0, juce::KeyPress( juce::KeyPress::escapeKey ) );
            m_nameDialog->enterModalState(
                true,
                juce::ModalCallbackFunction::create(
                    [this]( int result )
                    {
                        if ( result == 1 && m_nameDialog != nullptr )
                        {
                            const juce::String name = m_nameDialog->getTextEditorContents( "name" );
                            if ( m_proc.saveUserPreset( m_presetModule,
                                                        m_instances > 1 ? m_instance.getSelectedId() - 1 : 0, name ) )
                                fillPresets();
                        }
                        m_nameDialog.reset();
                    } ),
                false );
        };
        addAndMakeVisible( m_save );
        m_browse.onClick = [this]
        {
            if ( auto *ed = findParentComponentOfClass<EphemerisEditor>() )
                ed->browsePresets( m_presetModule, instance(),
                                   [safe = juce::Component::SafePointer<ParamPage>( this )]( int id )
                                   {
                                       if ( safe == nullptr ) return;
                                       safe->fillPresets();
                                       safe->chosen( id );
                                   } );
        };
        addAndMakeVisible( m_browse );
        m_star.setColour( juce::Label::textColourId, Accent );
        m_star.setFont( juce::FontOptions( 16.0f, juce::Font::bold ) );
        m_star.setJustificationType( juce::Justification::centred );
        m_star.setTooltip( "The knobs have moved from the preset (the disk saves the sound as one of your own)" );
        addAndMakeVisible( m_star );
        m_composed.setColour( juce::Label::textColourId, Dim );
        m_composed.setTooltip(
            "The preset the composer chose for this synth in the piece that plays (Composer's Sounds on the "
            "Master page; reroll sounds draws others)" );
        addAndMakeVisible( m_composed );
        startTimerHz( 2 );
        m_prev.onClick = [this]
        { choosePreset( ( ( m_presetIndex < 0 ? 0 : m_presetIndex ) - 1 + m_presetCount ) % m_presetCount ); };
        m_next.onClick = [this] { choosePreset( ( m_presetIndex + 1 ) % m_presetCount ); };
        m_prev.setTooltip( "the preset before" );
        m_next.setTooltip( "the next preset" );
        addAndMakeVisible( m_preset );
        addAndMakeVisible( m_prev );
        addAndMakeVisible( m_next );
        if ( m_instances > 1 )
        {
            m_allRows.setTooltip( "a preset goes to every row, not only the one chosen" );
            addAndMakeVisible( m_allRows );
        }
    }
    else if ( m == ModuleE::ECHO || m == ModuleE::REVERB )
    {
        // The effect pages: the user's own presets of the whole page (all its rooms), saved and recalled by full key.
        m_pagePresets = true;
        m_presetCount = 0;
        fillPagePresets();
        m_preset.onChange = [this]
        {
            const int id = m_preset.getSelectedId();
            if ( id > 0 && id - 1 < static_cast<int>( m_pageUser.size() ) )
                m_proc.applyKeyText( m_pageUser[static_cast<size_t>( id - 1 )].second );
        };
        m_save.onClick = [this]
        {
            m_nameDialog = std::make_unique<juce::AlertWindow>(
                "Save preset", "A name for the page as it is:", juce::MessageBoxIconType::NoIcon );
            m_nameDialog->addTextEditor( "name", "", "Name" );
            m_nameDialog->addButton( "Save", 1, juce::KeyPress( juce::KeyPress::returnKey ) );
            m_nameDialog->addButton( "Cancel", 0, juce::KeyPress( juce::KeyPress::escapeKey ) );
            m_nameDialog->enterModalState(
                true,
                juce::ModalCallbackFunction::create(
                    [this]( int result )
                    {
                        if ( result == 1 && m_nameDialog != nullptr )
                        {
                            const juce::String name =
                                juce::File::createLegalFileName( m_nameDialog->getTextEditorContents( "name" ).trim() );
                            const ParamStore &st = m_proc.store();
                            juce::String      text;
                            for ( const auto &g : m_groups )
                                for ( int k = 0; k < ParamStore::moduleCount( g.first ); ++k )
                                {
                                    const int id = st.id( g.first, g.second, k );
                                    text << juce::String( st.key( id ) ) << "=" << juce::String( st.get( id ), 6 )
                                         << "\n";
                                }
                            if ( name.isNotEmpty() && pageFolder().createDirectory() &&
                                 pageFolder().getChildFile( name + ".txt" ).replaceWithText( text ) )
                                fillPagePresets();
                        }
                        m_nameDialog.reset();
                    } ),
                false );
        };
        addAndMakeVisible( m_preset );
        addAndMakeVisible( m_save );
    }
    m_section.m_onChange = [this]( int )
    {
        applySection();
        refit();
    };
    addChildComponent( m_section );
    build();
}

juce::File ParamPage::pageFolder() const
{
    const juce::String page = m_groups.front().first == ModuleE::ECHO ? "Echo + Spring" : "Hall";
    return juce::File::getSpecialLocation( juce::File::userApplicationDataDirectory )
        .getChildFile( "Ephemeris" )
        .getChildFile( "Presets" )
        .getChildFile( page );
}

void ParamPage::fillPagePresets()
{
    m_preset.clear( juce::dontSendNotification );
    m_pageUser.clear();
    juce::Array<juce::File> files = pageFolder().findChildFiles( juce::File::findFiles, false, "*.txt" );
    files.sort();
    for ( const juce::File &f : files )
    {
        m_pageUser.push_back( { f.getFileNameWithoutExtension(), f.loadFileAsString() } );
        m_preset.addItem( f.getFileNameWithoutExtension(), static_cast<int>( m_pageUser.size() ) );
    }
    m_preset.setTextWhenNothingSelected( m_pageUser.empty() ? juce::String( "your presets (Save...)" )
                                                            : juce::String( m_pageUser.size() ) + " of yours" );
}

void ParamPage::fillPresets()
{
    const std::vector<SoundPreset> &list = factoryPresets( m_presetModule );
    m_preset.clear( juce::dontSendNotification );
    std::vector<std::string>               order;
    std::map<std::string, juce::PopupMenu> menus;
    for ( size_t i = 0; i < list.size(); ++i )
    {
        if ( menus.find( list[i].m_group ) == menus.end() ) order.push_back( list[i].m_group );
        menus[list[i].m_group].addItem( static_cast<int>( i ) + 1, list[i].m_name );
    }
    for ( const std::string &g : order ) m_preset.getRootMenu()->addSubMenu( g, menus[g] );
    m_user = m_proc.userPresets( m_presetModule );
    if ( !m_user.empty() )
    {
        juce::PopupMenu mine;
        for ( size_t i = 0; i < m_user.size(); ++i ) mine.addItem( 5001 + static_cast<int>( i ), m_user[i].m_name );
        m_preset.getRootMenu()->addSubMenu( "User", mine );
    }
    m_preset.setTextWhenNothingSelected(
        juce::String( m_presetCount ) + " presets" +
        ( m_user.empty() ? "" : " + " + juce::String( m_user.size() ) + " of yours" ) );
}

void ParamPage::timerCallback()
{
    const int inst = m_instances > 1 ? m_instance.getSelectedId() - 1 : 0;
    const int index = m_proc.composedPreset( m_presetModule, inst );
    if ( index != m_shown )
    {
        m_shown = index;
        const std::vector<SoundPreset> &list = factoryPresets( m_presetModule );
        m_composed.setText( index >= 0 && index < static_cast<int>( list.size() )
                                ? juce::String( "this piece: " ) + list[static_cast<size_t>( index )].m_name + " (" +
                                      list[static_cast<size_t>( index )].m_group + ")"
                                : juce::String(),
                            juce::dontSendNotification );
        if ( index >= 0 && index < static_cast<int>( list.size() ) )
            m_loadedKnobs =
                knobsOf( m_proc, m_presetModule, inst, index + 1 );   // the composer's, until another is chosen
    }
    // The star: the knobs have moved from the preset they came from.
    const bool moved = !m_loadedKnobs.empty() && !knobsAt( m_proc.store(), m_loadedKnobs );
    if ( m_star.getText().isNotEmpty() != moved ) m_star.setText( moved ? "*" : "", juce::dontSendNotification );
}

void ParamPage::choosePreset( int index )
{
    if ( m_presetCount == 0 || index < 0 || index >= m_presetCount ) return;
    m_presetIndex = index;
    m_preset.setSelectedId( index + 1, juce::dontSendNotification );
    const int one = m_instances > 1 ? m_instance.getSelectedId() - 1 : 0;
    for ( int i = 0; i < m_instances; ++i )
        if ( i == one || m_allRows.getToggleState() ) m_proc.applyPreset( m_presetModule, i, index );
    chosen( index + 1 );
}

void ParamPage::chosen( int menuId )
{
    if ( menuId <= 5000 ) m_presetIndex = menuId - 1;
    m_preset.setSelectedId( menuId, juce::dontSendNotification );
    m_loadedKnobs = knobsOf( m_proc, m_presetModule, instance(), menuId );
    if ( m_preset.getText().isNotEmpty() )
        frame::PresetBrowser::played( "Ephemeris", synthKey( m_proc.store(), m_presetModule ), m_preset.getText() );
    m_star.setText( {}, juce::dontSendNotification );
}

void ParamPage::reveal( juce::Component &part )
{
    // The box holding it, and its section in front.
    for ( size_t b = 0; b < m_boxes.size(); ++b )
        for ( const Cell &c : m_boxes[b].m_cells )
        {
            juce::Component *comp = m_controls[c.m_control];
            if ( comp != &part && !comp->isParentOf( &part ) ) continue;
            if ( m_split && b < m_sectionOf.size() && m_sectionOf[b] != m_section.current() )
            {
                m_section.setCurrent( m_sectionOf[b] );
                applySection();
                refit();
            }
            return;
        }
}

bool ParamPage::bringId( int id )
{
    if ( m_instances <= 1 ) return false;
    const ParamStore &s = m_proc.store();
    if ( id < 0 || id >= s.count() ) return false;
    // The instance of a module of this page whose parameters hold it.
    for ( const auto &g : m_groups )
        for ( int inst = 0; inst < m_instances; ++inst )
        {
            const int b = s.base( g.first, inst );
            if ( b < 0 || id < b || id >= b + ParamStore::moduleCount( g.first ) ) continue;
            m_instance.setSelectedId( inst + 1, juce::sendNotificationSync );   // build() makes that row's controls
            return true;
        }
    return false;
}

ModSlotControl::ModSlotControl( int number, juce::Colour colour ) : m_number( number )
{
    for ( juce::ComboBox *c : { &m_source, &m_target } )
    {
        c->setColour( juce::ComboBox::arrowColourId, colour );
        addAndMakeVisible( *c );
    }
    m_amount.setSliderStyle( juce::Slider::LinearHorizontal );
    m_amount.setTextBoxStyle( juce::Slider::TextBoxRight, false, 46, 18 );
    m_amount.setColour( juce::Slider::trackColourId, colour );
    m_amount.setColour( juce::Slider::thumbColourId, ephui::colour::Ink );
    m_amount.setDoubleClickReturnValue( true, 0.0 );
    m_amount.setTooltip( "Amount (double-click: none)" );
    addAndMakeVisible( m_amount );
}

void ModSlotControl::resized()
{
    auto r = getLocalBounds().reduced( 2, 0 );
    r.removeFromLeft( 18 );   // the slot's number
    const int w = r.getWidth();
    m_source.setBounds( r.removeFromLeft( w * 30 / 100 ).withSizeKeepingCentre( w * 30 / 100 - 4, 24 ) );
    m_target.setBounds( r.removeFromLeft( w * 34 / 100 ).withSizeKeepingCentre( w * 34 / 100 - 4, 24 ) );
    m_amount.setBounds( r.withSizeKeepingCentre( r.getWidth(), 24 ) );
}

void ModSlotControl::paint( juce::Graphics &g )
{
    g.setColour( ephui::colour::Dim );
    g.setFont( juce::FontOptions( 12.0f, juce::Font::bold ) );
    g.drawText( juce::String( m_number ), 2, 0, 16, getHeight(), juce::Justification::centred );
}

void ParamPage::build()
{
    m_live.clear();
    m_sliders.clear();
    m_combos.clear();
    m_buttons.clear();
    m_controls.clear();
    m_labels.clear();
    m_boxes.clear();
    const int   inst = m_instances > 1 ? m_instance.getSelectedId() - 1 : 0;
    ParamStore &s = m_proc.store();
    // A control for parameter id, in a box of the given colour: a knob, a menu or a switch by its descriptor.
    // A control's name in its box, without the box's title in front ("Rate" in "LFO 1", "Attack" in "Amp Envelope").
    auto shortName = []( const juce::String &name, const juce::String &title )
    {
        if ( name.startsWith( title + " " ) ) return name.substring( title.length() + 1 );
        const juce::String first = title.upToFirstOccurrenceOf( " ", false, false );
        if ( first != title && name.startsWith( first + " " ) ) return name.substring( first.length() + 1 );
        return name;
    };
    auto make = [&]( int id, juce::Colour colour, bool big, const juce::String &title = {}, bool narrow = false )
    {
        StoreParameter *param = m_proc.parameter( id );
        if ( param == nullptr ) return Cell{};
        const ParamDesc &d = s.desc( id );
        auto            *label = m_labels.add(
            new juce::Label( {}, title.isEmpty() ? juce::String( d.m_name ) : shortName( d.m_name, title ) ) );
        label->setJustificationType( juce::Justification::centred );
        label->setColour( juce::Label::textColourId, big ? ephui::colour::Ink : ephui::colour::Dim );
        label->setFont( juce::FontOptions( big ? 13.0f : 12.0f ) );
        label->setMinimumHorizontalScale( 0.75f );
        addAndMakeVisible( label );
        Cell cell;
        cell.m_big = big;
        if ( d.m_curve == CurveE::CHOICE && d.m_choices != nullptr )
        {
            auto *box = new frame::Choice( &m_actions, id );
            for ( int c = 0; c <= static_cast<int>( d.m_maxValue ); ++c ) box->addItem( d.m_choices[c], c + 1 );
            box->setColour( juce::ComboBox::arrowColourId, colour );
            m_controls.add( box );
            m_combos.push_back( std::make_unique<juce::ComboBoxParameterAttachment>( *param, *box ) );
            cell.m_kind = 1;
            cell.m_narrow = narrow;
        }
        else if ( d.m_curve == CurveE::TOGGLE )
        {
            auto *b = new frame::Switch( &m_actions, id );
            b->setColour( juce::ToggleButton::tickColourId, colour );
            m_controls.add( b );
            m_buttons.push_back( std::make_unique<juce::ButtonParameterAttachment>( *param, *b ) );
            cell.m_kind = 2;
        }
        else
        {
            auto *sl = new frame::Knob( juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow,
                                        &m_actions, id );
            sl->setTextBoxStyle( juce::Slider::TextBoxBelow, false, big ? 96 : 70, 16 );
            sl->setColour( juce::Slider::rotarySliderFillColourId, colour );
            sl->setTextValueSuffix( d.m_unit[0] != 0 ? juce::String( " " ) + d.m_unit : juce::String() );
            sl->setTooltip( juce::String( d.m_name ) +
                            ( d.m_unit[0] != 0 ? juce::String( " (" ) + d.m_unit + ")" : juce::String() ) );
            m_controls.add( sl );
            m_sliders.push_back( std::make_unique<juce::SliderParameterAttachment>( *param, *sl ) );
            sl->setDoubleClickReturnValue( true, d.m_defValue );
            m_live.add( *sl, id );
        }
        addAndMakeVisible( m_controls.getLast() );
        cell.m_control = m_controls.size() - 1;
        return cell;
    };
    for ( const auto &g : m_groups )
    {
        const int count = ParamStore::moduleCount( g.first );
        const int instance = m_instances > 1 ? inst : g.second;
        // The module's keys (the part after the dot: "cutoff" of "voice3.cutoff").
        std::vector<std::string> keys( static_cast<size_t>( count ) );
        for ( int i = 0; i < count; ++i )
        {
            const std::string &k = s.key( s.id( g.first, instance, i ) );
            keys[static_cast<size_t>( i )] = k.substr( k.find( '.' ) + 1 );
        }
        std::vector<bool> placed( static_cast<size_t>( count ), false );
        auto              indexOf = [&]( const std::string &name )
        {
            for ( int i = 0; i < count; ++i )
                if ( !placed[static_cast<size_t>( i )] && keys[static_cast<size_t>( i )] == name ) return i;
            return -1;
        };
        // A slot of the modulation matrix ("@mod3": mod3_src, mod3_dst, mod3_amt) as one control.
        auto makeSlot = [&]( const std::string &name, juce::Colour colour )
        {
            const int src = indexOf( name + "_src" ), dst = indexOf( name + "_dst" ), amt = indexOf( name + "_amt" );
            if ( src < 0 || dst < 0 || amt < 0 ) return Cell{};
            StoreParameter *ps = m_proc.parameter( s.id( g.first, instance, src ) );
            StoreParameter *pd = m_proc.parameter( s.id( g.first, instance, dst ) );
            StoreParameter *pa = m_proc.parameter( s.id( g.first, instance, amt ) );
            if ( ps == nullptr || pd == nullptr || pa == nullptr ) return Cell{};
            auto *slot = new ModSlotControl( std::atoi( name.c_str() + 3 ), colour );
            for ( const auto &[box, id] : { std::pair<juce::ComboBox *, int>{ &slot->m_source, src },
                                            std::pair<juce::ComboBox *, int>{ &slot->m_target, dst } } )
            {
                const ParamDesc &d = s.desc( s.id( g.first, instance, id ) );
                for ( int c = 0; c <= static_cast<int>( d.m_maxValue ); ++c ) box->addItem( d.m_choices[c], c + 1 );
            }
            m_combos.push_back( std::make_unique<juce::ComboBoxParameterAttachment>( *ps, slot->m_source ) );
            m_combos.push_back( std::make_unique<juce::ComboBoxParameterAttachment>( *pd, slot->m_target ) );
            m_sliders.push_back( std::make_unique<juce::SliderParameterAttachment>( *pa, slot->m_amount ) );
            for ( int i : { src, dst, amt } ) placed[static_cast<size_t>( i )] = true;
            addAndMakeVisible( m_labels.add( new juce::Label() ) );   // none: the slot shows its number
            m_controls.add( slot );
            addAndMakeVisible( slot );
            Cell cell;
            cell.m_kind = 3;
            cell.m_control = m_controls.size() - 1;
            return cell;
        };
        for ( const ephui::GroupSpec &spec : ephui::layoutOf( g.first ) )
        {
            Box box;
            box.m_title = spec.m_title;
            box.m_colour = ephui::familyColour( spec.m_family );
            for ( const char *key : spec.m_keys )
            {
                if ( key[0] == '@' )
                {
                    const Cell c = makeSlot( key + 1, box.m_colour );
                    if ( c.m_control >= 0 ) box.m_cells.push_back( c );
                    continue;
                }
                const bool        big = key[0] == '*', narrow = key[0] == '~';
                const std::string name = big || narrow ? key + 1 : key;
                for ( int i = 0; i < count; ++i )
                {
                    if ( placed[static_cast<size_t>( i )] || keys[static_cast<size_t>( i )] != name ) continue;
                    const Cell c = make( s.id( g.first, instance, i ), box.m_colour, big, box.m_title, narrow );
                    if ( c.m_control >= 0 ) box.m_cells.push_back( c );
                    placed[static_cast<size_t>( i )] = true;
                }
            }
            if ( !box.m_cells.empty() ) m_boxes.push_back( std::move( box ) );
        }
        // Whatever the panel does not name, so nothing added to a table is lost from the editor.
        Box more;
        more.m_title = ephui::layoutOf( g.first ).empty() ? juce::String( "Settings" ) : juce::String( "More" );
        more.m_colour = ephui::familyColour( ephui::Family::SPACE );
        for ( int i = 0; i < count; ++i )
        {
            if ( placed[static_cast<size_t>( i )] ) continue;
            const Cell c = make( s.id( g.first, instance, i ), more.m_colour, false );
            if ( c.m_control >= 0 ) more.m_cells.push_back( c );
        }
        if ( !more.m_cells.empty() ) m_boxes.push_back( std::move( more ) );
    }
    // The sections are planned again for the new boxes (plan(), when the page is measured next).
    m_plannedWidth = -1;
    m_sectionOf.assign( m_boxes.size(), 0 );
    m_split = false;
    m_section.setVisible( false );
    applySection();
    if ( getWidth() > 0 ) refit();
    frame::keepKeysForEditor( *this );   // the keys stay the editor's (Space plays, not the switch last clicked)
}

bool ParamPage::shown( size_t box ) const
{
    if ( m_measuring != nullptr )
        return std::find( m_measuring->begin(), m_measuring->end(), static_cast<int>( box ) ) != m_measuring->end();
    return !m_split || ( box < m_sectionOf.size() && m_sectionOf[box] == m_section.current() );
}

void ParamPage::plan( int width )
{
    if ( width == m_plannedWidth && m_available == m_plannedAvailable ) return;
    m_plannedWidth = width;
    m_plannedAvailable = m_available;
    juce::StringArray titles;
    for ( const Box &b : m_boxes ) titles.add( b.m_title );
    // A section's height as the page lays it out: its boxes alone, the row the switch needs, the margins.
    auto height = [this, width]( const std::vector<int> &set )
    {
        const std::vector<Box> kept = m_boxes;
        m_measuring = &set;
        const int h = layoutBoxes( { 10, 10, width - 20, 100 }, false );
        m_measuring = nullptr;
        m_boxes = kept;
        return 20 + 32 + h + 10;
    };
    const frame::SectionPlan p = frame::planSections( titles, m_available, height );
    m_sectionOf.assign( m_boxes.size(), 0 );
    for ( size_t s = 0; s < p.m_groups.size(); ++s )
        for ( int b : p.m_groups[s] ) m_sectionOf[static_cast<size_t>( b )] = static_cast<int>( s );
    m_split = p.m_groups.size() > 1;
    if ( m_split ) m_section.setNames( p.m_names );
    m_section.setVisible( m_split );
    applySection();
}

void ParamPage::applySection()
{
    for ( size_t b = 0; b < m_boxes.size(); ++b )
    {
        const Box &box = m_boxes[b];
        const bool on = shown( b );
        for ( const Cell &c : box.m_cells )
        {
            m_controls[c.m_control]->setVisible( on );
            if ( c.m_kind != 4 ) m_labels[c.m_control]->setVisible( on );
        }
    }
}

void ParamPage::refit()
{
    resized();
    repaint();
    // The page that hosts this one measures again: the section shown is shorter or taller than the other.
    for ( auto *p = getParentComponent(); p != nullptr; p = p->getParentComponent() )
        if ( dynamic_cast<juce::Viewport *>( p ) != nullptr )
        {
            if ( auto *host = p->getParentComponent() ) host->resized();
            break;
        }
}

juce::String ParamPage::describe() const
{
    juce::String      t;
    const ParamStore &s = const_cast<EphemerisProcessor &>( m_proc ).store();
    for ( const Box &box : m_boxes )
    {
        t << box.m_title.toUpperCase() << "\n";
        for ( const Cell &c : box.m_cells )
        {
            if ( c.m_kind == 3 )
            {
                t << "  a slot of the modulation matrix: source, target, amount\n";
                continue;
            }
            int id = -1;
            if ( auto *k = dynamic_cast<frame::Knob *>( m_controls[c.m_control] ) )
                id = k->id();
            else if ( auto *m = dynamic_cast<frame::Choice *>( m_controls[c.m_control] ) )
                id = m->id();
            else if ( auto *w = dynamic_cast<frame::Switch *>( m_controls[c.m_control] ) )
                id = w->id();
            if ( id < 0 ) continue;
            const ParamDesc &d = s.desc( id );
            t << "  " << d.m_name << ": ";
            if ( d.m_curve == CurveE::TOGGLE )
                t << ( s.getBool( id ) ? "on" : "off" );
            else if ( d.m_curve == CurveE::CHOICE && d.m_choices != nullptr )
                t << d.m_choices[s.getInt( id )];
            else
                t << juce::String( s.get( id ), 2 )
                  << ( d.m_unit[0] != 0 ? juce::String( " " ) + d.m_unit : juce::String() ) << "   ("
                  << juce::String( d.m_minValue, 2 ) << " .. " << juce::String( d.m_maxValue, 2 ) << ", default "
                  << juce::String( d.m_defValue, 2 ) << ")";
            t << "   [" << s.key( id ) << "]\n";
        }
        t << "\n";
    }
    return t.trimEnd();
}

int ParamPage::top() const
{
    return m_instances > 1 || m_presetCount > 0 || m_pagePresets || m_split ? 32 : 0;
}

int ParamPage::layoutBoxes( juce::Rectangle<int> area, bool apply )
{
    // The panel of an instrument: titled boxes side by side, flowing into rows across the page, every box of a row
    // as tall as the tallest; inside a box its controls in a line (wrapping where the page is narrow), the large
    // encoders among the small ones, all centred on the line.
    constexpr int Title = 22, Pad = 8, Gap = 10;
    auto          cellSize = []( const Cell &c )
    {
        switch ( c.m_kind )
        {
        case 1: return juce::Point<int>( c.m_narrow ? 112 : 140, 100 );
        case 2: return juce::Point<int>( 92, 100 );
        case 3: return juce::Point<int>( 360, 34 );
        default: return c.m_big ? juce::Point<int>( 108, 136 ) : juce::Point<int>( 82, 100 );
        }
    };
    const int           width = std::max( 200, area.getWidth() );
    int                 x = 0, y = 0, lineH = 0;
    std::vector<size_t> line;
    auto                closeLine = [&]()
    {
        for ( size_t b : line ) m_boxes[b].m_bounds.setHeight( lineH );
        line.clear();
    };
    for ( size_t b = 0; b < m_boxes.size(); ++b )
    {
        Box &box = m_boxes[b];
        if ( !shown( b ) )
        {
            box.m_bounds = {};
            continue;
        }
        // Lines of cells inside the box, no wider than the page.
        const int                        inner = width - 2 * Pad;
        std::vector<std::pair<int, int>> rows;   // (width, height) of each line of cells
        int                              rw = 0, rh = 0;
        for ( const Cell &c : box.m_cells )
        {
            const auto sz = cellSize( c );
            if ( rw > 0 && rw + sz.x > inner )
            {
                rows.push_back( { rw, rh } );
                rw = 0;
                rh = 0;
            }
            rw += sz.x;
            rh = std::max( rh, sz.y );
        }
        if ( rw > 0 ) rows.push_back( { rw, rh } );
        int bw = 0, bh = Title + Pad;
        for ( const auto &r : rows )
        {
            bw = std::max( bw, r.first );
            bh += r.second;
        }
        bw = std::max( bw + 2 * Pad, 120 );
        bh += Pad / 2;
        if ( x > 0 && x + bw > width )
        {
            closeLine();
            x = 0;
            y += lineH + Gap;
            lineH = 0;
        }
        box.m_bounds = { area.getX() + x, area.getY() + y, bw, bh };
        line.push_back( b );
        lineH = std::max( lineH, bh );
        if ( apply )
        {
            int cx = 0, cy = Title, row = 0;
            int rowW = rows.empty() ? 0 : rows[0].first, rowH = rows.empty() ? 0 : rows[0].second;
            for ( Cell &c : box.m_cells )
            {
                const auto sz = cellSize( c );
                if ( cx > 0 && cx + sz.x > rowW )
                {
                    cy += rowH;
                    cx = 0;
                    ++row;
                    rowW = rows[static_cast<size_t>( row )].first;
                    rowH = rows[static_cast<size_t>( row )].second;
                }
                const int left = box.m_bounds.getX() + Pad + ( bw - 2 * Pad - rowW ) / 2;
                c.m_bounds = { left + cx, box.m_bounds.getY() + cy + ( rowH - sz.y ) / 2, sz.x, sz.y };
                cx += sz.x;
                juce::Component *comp = m_controls[c.m_control];
                juce::Label     *label = m_labels[c.m_control];
                const auto       r = c.m_bounds.reduced( 3, 2 );
                label->setBounds( r.getX(), r.getY(), r.getWidth(), c.m_kind == 3 ? 0 : 16 );
                if ( c.m_kind == 0 )
                    comp->setBounds( r.withTrimmedTop( 16 ) );
                else if ( c.m_kind == 3 )
                    comp->setBounds( c.m_bounds );
                else
                    comp->setBounds( r.getX() + 2, r.getCentreY() - 12, r.getWidth() - 4, 24 );
            }
        }
        x += bw + Gap;
    }
    closeLine();
    return y + lineH;
}

int ParamPage::heightFor( int width ) const
{
    const_cast<ParamPage *>( this )->plan( width );   // the sections for this width first
    // Measured on a copy: the boxes as they are laid out and drawn stay where resized() put them.
    auto                  *self = const_cast<ParamPage *>( this );
    const std::vector<Box> kept = m_boxes;
    const int              h = self->layoutBoxes( { 10, 10, width - 20, 100 }, false );
    self->m_boxes = kept;
    return 20 + top() + h + 10;
}

void ParamPage::paint( juce::Graphics &g )
{
    for ( const Box &box : m_boxes )
    {
        if ( !shown( static_cast<size_t>( &box - m_boxes.data() ) ) ) continue;
        const auto r = box.m_bounds.toFloat();
        g.setColour( ephui::colour::Group );
        g.fillRoundedRectangle( r, 6.0f );
        g.setColour( ephui::colour::Edge );
        g.drawRoundedRectangle( r.reduced( 0.5f ), 6.0f, 1.0f );
        g.setColour( box.m_colour );
        g.setFont( juce::FontOptions( 11.5f, juce::Font::bold ) );
        g.drawText( box.m_title.toUpperCase(), box.m_bounds.getX() + 10, box.m_bounds.getY() + 4,
                    box.m_bounds.getWidth() - 20, 14, juce::Justification::centredLeft );
        g.setColour( box.m_colour.withAlpha( 0.45f ) );
        g.fillRect( r.getX() + 10.0f, r.getY() + 19.0f, r.getWidth() - 20.0f, 1.0f );
    }
}

void ParamPage::resized()
{
    auto area = getLocalBounds().reduced( 10 );
    if ( m_instances > 1 || m_presetCount > 0 || m_pagePresets || m_split )
    {
        auto top = area.removeFromTop( 26 );
        if ( m_split )
        {
            m_section.setBounds( top.removeFromRight( m_section.bestWidth() ) );
            top.removeFromRight( 10 );
        }
        if ( m_instances > 1 )
        {
            m_instance.setBounds( top.removeFromLeft( 90 ) );
            top.removeFromLeft( 12 );
        }
        if ( m_presetCount > 0 )
        {
            m_prev.setBounds( top.removeFromLeft( 28 ) );
            top.removeFromLeft( 4 );
            m_preset.setBounds( top.removeFromLeft( 300 ) );
            top.removeFromLeft( 4 );
            m_next.setBounds( top.removeFromLeft( 28 ) );
            top.removeFromLeft( 8 );
            m_save.setBounds( top.removeFromLeft( frame::IconWidth ) );
            m_browse.setBounds( top.removeFromLeft( frame::IconWidth ) );
            m_star.setBounds( top.removeFromLeft( 16 ) );
            if ( m_instances > 1 )
            {
                top.removeFromLeft( 8 );
                m_allRows.setBounds( top.removeFromLeft( 90 ) );
            }
            top.removeFromLeft( 12 );
            m_composed.setBounds( top );
        }
        else if ( m_pagePresets )
        {
            m_preset.setBounds( top.removeFromLeft( 300 ) );
            top.removeFromLeft( 8 );
            m_save.setBounds( top.removeFromLeft( frame::IconWidth ) );
        }
    }
    area.removeFromTop( 6 );
    layoutBoxes( area, true );
}

// ---------------------------------------------------------------------------------------------------

ArrangeView::ArrangeView( EphemerisProcessor &p ) : m_proc( p )
{
    setTooltip(
        "Click: jump there. Mouse wheel: zoom in and out around the pointer; drag, or Shift + wheel: move along; "
        "double click: the whole length." );
    if ( const char *z = std::getenv( "EPH_SHOT_ZOOM" ) )
    {   // "from:to", beats
        const juce::String s( z );
        zoomTo( s.upToFirstOccurrenceOf( ":", false, false ).getDoubleValue(),
                s.fromFirstOccurrenceOf( ":", false, false ).getDoubleValue() );
    }
    startTimerHz( 20 );
}

void ArrangeView::rebuild()
{
    Score s;
    m_proc.copyScore( s );
    if ( std::fabs( s.m_lengthBeats - m_beats ) > 1.0e-6 )
    {
        m_from = 0.0;
        m_span = 0.0;
    }   // another length: the whole of it
    m_beats = s.m_lengthBeats;
    m_markers = s.m_markers;
    m_tempo = s.m_tempo;
    m_longest = 0.0;
    for ( auto &lane : m_hits ) lane.clear();
    m_bins = m_beats > 0.0 ? std::clamp( static_cast<int>( std::ceil( m_beats ) ), 1, 1 << 17 ) : 0;
    m_lanes.assign( static_cast<size_t>( Lanes ) * static_cast<size_t>( m_bins ), 0.0f );
    m_cachedVersion = -1;
    if ( m_bins == 0 ) return;
    std::vector<uint8_t> rows( static_cast<size_t>( m_bins ), 0 );   // a bit per row playing in the column
    for ( const NoteEvent &n : s.m_notes )
    {
        int       lane = -1;
        const int part = static_cast<int>( n.m_part );
        if ( part < Rows )
            lane = 0;
        else if ( n.m_part == PartE::LEAD )
            lane = 1;
        else if ( n.m_part == PartE::TAPE_KEYS )
            lane = 2;
        else if ( n.m_part == PartE::STRINGS || n.m_part == PartE::PAD )
            lane = 3;
        else if ( n.m_part == PartE::DRONE )
            lane = 4;
        else if ( n.m_part == PartE::DRUMS )
            lane = 5;
        if ( lane < 0 || n.m_velocity <= 0.0f ) continue;
        // The drums as short strokes, so their hits stand apart zoomed in; the rest as long as it sounds.
        const double length = lane == 5 ? 0.12 : std::max( n.m_length, 0.1 );
        m_hits[lane].push_back( { static_cast<float>( n.m_beat ), static_cast<float>( n.m_beat + length ),
                                  std::min( 1.0f, n.m_velocity ) } );
        m_longest = std::max( m_longest, length );
        const int b0 = std::clamp( static_cast<int>( n.m_beat / m_beats * m_bins ), 0, m_bins - 1 );
        const int b1 = std::clamp( static_cast<int>( ( n.m_beat + std::max( length, 0.25 ) ) / m_beats * m_bins ), b0,
                                   m_bins - 1 );
        for ( int b = b0; b <= b1; ++b )
        {
            if ( lane == 0 )
                rows[static_cast<size_t>( b )] =
                    static_cast<uint8_t>( rows[static_cast<size_t>( b )] | ( 1u << part ) );
            else
                m_lanes[static_cast<size_t>( lane ) * static_cast<size_t>( m_bins ) + static_cast<size_t>( b )] = 1.0f;
        }
    }
    for ( int b = 0; b < m_bins; ++b )
    {
        int count = 0;
        for ( uint8_t v = rows[static_cast<size_t>( b )]; v != 0; v &= static_cast<uint8_t>( v - 1 ) ) ++count;
        m_lanes[static_cast<size_t>( b )] =
            count == 0 ? 0.0f : 0.35f + 0.65f * static_cast<float>( count ) / static_cast<float>( Rows );
    }
    for ( auto &lane : m_hits )
        std::sort( lane.begin(), lane.end(), []( const Hit &x, const Hit &y ) { return x.m_from < y.m_from; } );
}

void ArrangeView::window( double &from, double &to ) const
{
    if ( m_span <= 0.0 || m_span >= m_beats )
    {
        from = 0.0;
        to = std::max( m_beats, 1.0 );
        return;
    }
    from = std::clamp( m_from, 0.0, m_beats - m_span );
    to = from + m_span;
}

bool ArrangeView::zoomed( double &from, double &to ) const
{
    window( from, to );
    return m_span > 0.0 && m_span < m_beats;
}

void ArrangeView::show( double from, double span )
{
    if ( m_beats <= 0.0 ) return;
    span = std::clamp( span, std::min( m_beats, Narrowest ), m_beats );
    if ( span >= m_beats - 1.0e-9 )
    {
        m_from = 0.0;
        m_span = 0.0;
        return;
    }
    m_span = span;
    m_from = std::clamp( from, 0.0, m_beats - span );
}

void ArrangeView::zoomAround( float x, double factor )
{
    double a = 0.0, b = 0.0;
    window( a, b );
    const double t = std::clamp( static_cast<double>( x ) / std::max( 1, getWidth() ), 0.0, 1.0 );
    const double at = a + ( b - a ) * t, span = ( b - a ) * factor;
    show( at - span * t, span );
    repaint();
}

double ArrangeView::beatAt( float x ) const
{
    double a = 0.0, b = 0.0;
    window( a, b );
    return a + ( b - a ) * std::clamp( static_cast<double>( x ) / std::max( 1, getWidth() ), 0.0, 1.0 );
}

void ArrangeView::timerCallback()
{
    if ( !isShowing() ) return;
    // A zoomed view pages on when the playhead runs out of it -- not when the view was moved away from the playhead.
    const double pos = m_proc.positionBeats();
    double       a = 0.0, b = 0.0;
    if ( zoomed( a, b ) && !m_dragged && a == m_lastFrom && b == m_lastTo && m_lastPos >= a && m_lastPos <= b &&
         ( pos < a || pos > b ) )
    {
        show( pos - 0.05 * ( b - a ), b - a );
        window( a, b );
    }
    m_lastPos = pos;
    m_lastFrom = a;
    m_lastTo = b;
    repaint();
}

void ArrangeView::paint( juce::Graphics &g )
{
    if ( m_proc.scoreVersion() != m_version )
    {
        m_version = m_proc.scoreVersion();
        rebuild();
    }
    if ( m_wanted.second > m_wanted.first && m_beats > 0.0 )
    {
        show( m_wanted.first, m_wanted.second - m_wanted.first );
        m_wanted = { 0.0, 0.0 };
    }
    if ( m_beats <= 0.0 )
    {
        g.fillAll( Panel );
        return;
    }
    double      a = 0.0, b = 0.0;
    const bool  zoom = zoomed( a, b );
    const float w = static_cast<float>( getWidth() ), h = static_cast<float>( getHeight() );
    // The picture at the screen's own pixels (the panel is scaled with the window), drawn again when the window moves.
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int   iw = std::max( 1, juce::roundToInt( w * scale ) ), ih = std::max( 1, juce::roundToInt( h * scale ) );
    if ( !m_cache.isValid() || m_cache.getWidth() != iw || m_cache.getHeight() != ih || m_cachedFrom != a ||
         m_cachedTo != b || m_cachedVersion != m_version )
    {
        m_cache = juce::Image( juce::Image::RGB, iw, ih, false );
        juce::Graphics ig( m_cache );
        ig.addTransform( juce::AffineTransform::scale( static_cast<float>( iw ) / w, static_cast<float>( ih ) / h ) );
        render( ig, w, h, a, b, zoom );
        m_cachedFrom = a;
        m_cachedTo = b;
        m_cachedVersion = m_version;
    }
    g.drawImage( m_cache, getLocalBounds().toFloat() );
    const double pos = m_proc.positionBeats();
    const float  x = static_cast<float>( ( pos - a ) / ( b - a ) ) * w;
    g.setColour( Accent );
    if ( x >= -1.0f && x <= w + 1.0f ) g.fillRect( x - 1.0f, 0.0f, 2.0f, h );
    if ( zoom )
        g.fillRect( w * static_cast<float>( pos / m_beats ) - 1.0f, h - 4.0f, 2.0f,
                    4.0f );   // and on the bar of the whole
}

void ArrangeView::render( juce::Graphics &g, float w, float h, double a, double b, bool zoom )
{
    g.fillAll( Panel );
    const auto  xOf = [&]( double beat ) { return static_cast<float>( ( beat - a ) / ( b - a ) ) * w; };
    const float rule = 12.0f, ry = h - rule, top = 20.0f;
    const auto  endOf = [&]( size_t i ) { return i + 1 < m_markers.size() ? m_markers[i + 1].m_beat : m_beats; };
    // The sections, behind the lanes.
    for ( size_t i = 0; i < m_markers.size(); ++i )
    {
        const float x0 = xOf( m_markers[i].m_beat ), x1 = xOf( endOf( i ) );
        if ( x1 < 0.0f || x0 > w ) continue;
        const float l = std::max( x0 + 1.0f, -2.0f ), r = std::min( x1 - 1.0f, w + 2.0f );
        g.setColour( sectionColour( juce::String( m_markers[i].m_text ) ) );
        g.fillRect( l, 2.0f, std::max( 1.0f, r - l ), ry - 3.0f );
    }
    // The names. A concert's pieces ("Stueck 2 (Cosmic): Aufbau") apart by a line and their number at their start ("#2
    // Cosmic"); the sections' names where one of the piece's has room -- else the number alone over all of them.
    const juce::Font font{ juce::FontOptions( 11.0f ) };
    g.setFont( font );
    const auto pieceOf = []( const std::string &t )
    {
        const size_t k = t.rfind( ": " );
        return k == std::string::npos ? std::string() : t.substr( 0, k );
    };
    for ( size_t i = 0; i < m_markers.size(); )
    {
        const std::string piece = pieceOf( m_markers[i].m_text );
        size_t            j = i + 1;
        while ( !piece.empty() && j < m_markers.size() && pieceOf( m_markers[j].m_text ) == piece ) ++j;
        const float gx0 = xOf( m_markers[i].m_beat ), gx1 = xOf( endOf( j - 1 ) );
        if ( gx1 >= 0.0f && gx0 <= w )
        {
            if ( i > 0 && !piece.empty() )
            {
                g.setColour( Ink.withAlpha( 0.75f ) );
                g.fillRect( gx0 - 1.0f, 0.0f, 2.0f, ry );
            }
            bool roomy = false;
            for ( size_t k = i; k < j && !roomy; ++k ) roomy = xOf( endOf( k ) ) - xOf( m_markers[k].m_beat ) >= 40.0f;
            const juce::String tag = juce::String( piece )
                                         .replace( "Stueck ", "#" )
                                         .replace( "Zwischenspiel ", "Z" )
                                         .removeCharacters( "()" );
            g.setColour( Ink );
            if ( roomy )
            {
                for ( size_t k = i; k < j; ++k )
                {
                    const float        l = std::max( xOf( m_markers[k].m_beat ), 0.0f ) + 4.0f,
                                       r = std::min( xOf( endOf( k ) ), w ) - 4.0f;
                    const juce::String part =
                        juce::String( m_markers[k].m_text ).fromLastOccurrenceOf( ": ", false, false );
                    juce::String text = part;
                    if ( k == i && tag.isNotEmpty() )
                    {
                        text = tag + "  " + part;
                        if ( juce::GlyphArrangement::getStringWidth( font, text ) > r - l ) text = tag;
                    }
                    if ( r - l < ( text == tag ? 12.0f : 26.0f ) ) continue;
                    g.drawText( text, juce::Rectangle<float>( l, 3.0f, r - l, 15.0f ), juce::Justification::centredLeft,
                                true );
                }
            }
            else
            {
                const float l = std::max( gx0, 0.0f ) + 3.0f, r = std::min( gx1, w ) - 2.0f;
                if ( r - l >= 12.0f )
                    g.drawText( tag, juce::Rectangle<float>( l, 3.0f, r - l, 15.0f ), juce::Justification::centredLeft,
                                true );
            }
        }
        i = j;
    }
    // The ruler: a piece's bars, a concert's minutes -- at a step that leaves the numbers room; faint lines through the
    // lanes.
    {
        const auto clock = []( double secs )
        {
            const int s = static_cast<int>( std::lround( secs ) ), hours = s / 3600, mins = ( s / 60 ) % 60;
            return ( hours > 0 ? juce::String( hours ) + ":" + juce::String( mins ).paddedLeft( '0', 2 )
                               : juce::String( mins ) ) +
                   ":" + juce::String( s % 60 ).paddedLeft( '0', 2 );
        };
        const float room = 52.0f;
        g.setFont( juce::FontOptions( 9.0f ) );
        const auto tick = [&]( double beat, const juce::String &label )
        {
            const float x = xOf( beat );
            g.setColour( Ink.withAlpha( 0.07f ) );
            g.fillRect( x, top, 1.0f, ry - top );
            g.setColour( Dim );
            g.fillRect( x, ry, 1.0f, 4.0f );
            g.drawText( label, juce::Rectangle<float>( x + 3.0f, ry, room, rule ), juce::Justification::centredLeft );
        };
        if ( m_proc.playingKind() != 0 )
        {
            const double     s0 = m_tempo.secondsAt( a ), s1 = m_tempo.secondsAt( b );
            static const int ClockSteps[] = { 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600 };
            int              step = 3600;
            for ( int k : ClockSteps )
                if ( static_cast<double>( k ) * w / std::max( 1.0, s1 - s0 ) >= room )
                {
                    step = k;
                    break;
                }
            for ( double t = std::ceil( s0 / step ) * step; t <= s1; t += step )
                tick( m_tempo.beatAt( t ), clock( t ) );
        }
        else
        {
            const double perBar = w / std::max( 1.0e-9, ( b - a ) / BeatsPerBar );
            int          step = 1;
            while ( step * perBar < room && step < 4096 ) step *= 2;
            for ( int bar = static_cast<int>( std::ceil( a / BeatsPerBar / step ) ) * step;
                  static_cast<double>( BeatsPerBar ) * bar <= b; bar += step )
                tick( static_cast<double>( BeatsPerBar ) * bar, juce::String( bar + 1 ) );
        }
    }
    // The lanes: zoomed in far enough the notes themselves, else the activity under each pixel.
    const float  laneH = ( ry - top - 1.0f ) / static_cast<float>( Lanes );
    const double pxPerBeat = w / std::max( 1.0e-9, b - a );
    if ( laneH > 2.0f )
    {
        for ( int l = 0; l < Lanes; ++l )
        {
            const float y = top + laneH * static_cast<float>( l ) + 1.0f, hh = laneH - 2.0f;
            if ( pxPerBeat >= 2.0 )
            {
                const std::vector<Hit> &row = m_hits[l];
                auto                    it = std::lower_bound( row.begin(), row.end(), a - m_longest,
                                                               []( const Hit &x, double v ) { return x.m_from < v; } );
                for ( ; it != row.end() && it->m_from < b; ++it )
                {
                    if ( it->m_to <= a ) continue;
                    const float x0 = std::max( -1.0f, xOf( it->m_from ) ), x1 = std::min( w + 1.0f, xOf( it->m_to ) );
                    g.setColour( Ink.withAlpha( 0.25f + 0.45f * it->m_velocity ) );
                    g.fillRect( x0, y, std::max( 1.2f, x1 - x0 - ( x1 - x0 > 3.0f ? 1.0f : 0.0f ) ), hh );
                }
                continue;
            }
            const float *lane = m_lanes.data() + static_cast<size_t>( l ) * static_cast<size_t>( m_bins );
            const int    cols = static_cast<int>( std::ceil( w ) );
            float        runV = 0.0f;
            int          runX = 0;
            for ( int x = 0; x <= cols; ++x )
            {
                float v = 0.0f;
                if ( x < cols )
                {
                    const double f0 = ( a + ( b - a ) * x / w ) / m_beats,
                                 f1 = ( a + ( b - a ) * ( x + 1 ) / w ) / m_beats;
                    const int    b0 = std::clamp( static_cast<int>( f0 * m_bins ), 0, m_bins - 1 );
                    const int    b1 = std::clamp( static_cast<int>( std::ceil( f1 * m_bins ) ), b0 + 1, m_bins );
                    for ( int k = b0; k < b1; ++k ) v = std::max( v, lane[k] );
                }
                if ( x < cols && v == runV ) continue;
                if ( runV > 0.0f )
                {
                    g.setColour( Ink.withAlpha( 0.55f * runV ) );
                    g.fillRect( static_cast<float>( runX ), y, static_cast<float>( x - runX ), hh );
                }
                runV = v;
                runX = x;
            }
        }
        static const char *const names[Lanes] = { "rows", "lead", "tape", "str+poly", "drone", "drums" };
        g.setFont( juce::FontOptions( 9.0f ) );
        for ( int l = 0; l < Lanes; ++l )
        {
            const juce::Rectangle<float> r( 2.0f, top + laneH * static_cast<float>( l ), 44.0f, laneH );
            g.setColour( Panel.withAlpha( 0.7f ) );
            g.fillRect( r.withWidth( 40.0f ) );
            g.setColour( Dim );
            g.drawText( names[l], r.reduced( 2.0f, 0.0f ), juce::Justification::centredLeft );
        }
    }
    // Zoomed: where the window lies in the whole, under the ruler.
    if ( zoom )
    {
        g.setColour( Ink.withAlpha( 0.12f ) );
        g.fillRect( 0.0f, h - 2.0f, w, 2.0f );
        g.setColour( Accent.withAlpha( 0.85f ) );
        g.fillRect( w * static_cast<float>( a / m_beats ), h - 2.0f,
                    std::max( 3.0f, w * static_cast<float>( ( b - a ) / m_beats ) ), 2.0f );
    }
}

void ArrangeView::mouseDown( const juce::MouseEvent &e )
{
    m_downX = e.position.x;
    m_dragged = false;
    double     a = 0.0, b = 0.0;
    const bool zoom = zoomed( a, b );
    m_downFrom = a;
    if ( !zoom ) m_proc.seekTo( beatAt( e.position.x ) );   // the whole length: at once, as ever
}

void ArrangeView::mouseDrag( const juce::MouseEvent &e )
{
    double a = 0.0, b = 0.0;
    if ( !zoomed( a, b ) ) return;
    const float dx = e.position.x - m_downX;
    if ( !m_dragged && std::abs( dx ) < 4.0f ) return;
    m_dragged = true;
    setMouseCursor( juce::MouseCursor::DraggingHandCursor );
    show( m_downFrom - ( b - a ) * dx / std::max( 1, getWidth() ), b - a );
    repaint();
}

void ArrangeView::mouseUp( const juce::MouseEvent &e )
{
    if ( m_dragged )
    {
        m_dragged = false;
        setMouseCursor( juce::MouseCursor::NormalCursor );
        return;
    }
    double a = 0.0, b = 0.0;
    if ( zoomed( a, b ) ) m_proc.seekTo( beatAt( e.position.x ) );
}

void ArrangeView::mouseDoubleClick( const juce::MouseEvent & )
{
    show( 0.0, 0.0 );
    repaint();
}

void ArrangeView::mouseWheelMove( const juce::MouseEvent &e, const juce::MouseWheelDetails &wheel )
{
    double a = 0.0, b = 0.0;
    window( a, b );
    // Sideways (a trackpad, a tilting wheel) or with Shift: along the length; else in and out, about 1.4 times a notch.
    const bool sideways = std::abs( wheel.deltaX ) > std::abs( wheel.deltaY );
    if ( sideways || e.mods.isShiftDown() )
    {
        const float d = sideways ? wheel.deltaX : wheel.deltaY;
        show( a - ( b - a ) * 0.5 * d, b - a );
        repaint();
        return;
    }
    if ( wheel.deltaY != 0.0f ) zoomAround( e.position.x, std::pow( 2.0, -2.0 * wheel.deltaY ) );
}

void ArrangeView::mouseMagnify( const juce::MouseEvent &e, float scale )
{
    if ( scale > 0.0f ) zoomAround( e.position.x, 1.0 / scale );
}

// ---------------------------------------------------------------------------------------------------

ScrollingPage::ScrollingPage( std::unique_ptr<ParamPage> page ) : m_page( std::move( page ) )
{
    m_view.setViewedComponent( m_page.get(), false );
    m_view.setScrollBarsShown( true, false );
    addAndMakeVisible( m_view );
}

void ScrollingPage::resized()
{
    fit( *m_page, m_view, getLocalBounds() );
}

void ScrollingPage::fit( ParamPage &page, juce::Viewport &view, juce::Rectangle<int> area )
{
    view.setBounds( area );
    page.setAvailableHeight( area.getHeight() );   // the sections are cut to it (01.10.2026)
    int w = area.getWidth(), h = page.heightFor( w );
    if ( h > area.getHeight() )
    {
        w -= view.getScrollBarThickness();
        h = page.heightFor( w );
    }
    page.setSize( w, std::max( h, area.getHeight() ) );
}

/**
 * @brief The logo (Deploy/make_icon.py, drawn as vectors): the Rack page's orrery -- a warm sun, orbits and their
 *        planets on a dark tile; two orbits up to 24 px, three up to 48, five above.
 * @param[in,out] g  the graphics context to draw into
 * @param[in]     r  where the logo goes
 */
void drawLogo( juce::Graphics &g, juce::Rectangle<float> r )
{
    const float s = std::min( r.getWidth(), r.getHeight() );
    r = r.withSizeKeepingCentre( s, s );
    const juce::Point<float> c = r.getCentre();
    g.setColour( juce::Colour( 21, 23, 28 ) );
    g.fillRoundedRectangle( r, s / 5.0f );
    const int                 orbits = s <= 24.0f ? 2 : ( s <= 48.0f ? 3 : 5 );
    const float               sun = s * ( s > 24.0f ? 0.16f : 0.2f ), width = std::max( 1.0f, s / 64.0f );
    static const juce::uint32 planets[5] = { 0xffe0a458, 0xffd9825b, 0xff9fbf6f, 0xff6fb8ae, 0xff6f8fb8 };
    for ( int k = 0; k < orbits; ++k )
    {
        const float rad = sun + ( s * 0.42f - sun ) * static_cast<float>( k + 1 ) / static_cast<float>( orbits );
        g.setColour( juce::Colour( 201, 164, 92 ).withAlpha( 150.0f / 255.0f ) );
        g.drawEllipse( c.x - rad, c.y - rad, 2.0f * rad, 2.0f * rad, width );
        const float a = -juce::MathConstants<float>::halfPi + 2.1f * static_cast<float>( k + 1 );
        const float pr = std::max( s * 0.035f, width * 2.2f );
        g.setColour( juce::Colour( planets[k % 5] ) );
        g.fillEllipse( c.x + rad * std::cos( a ) - pr, c.y + rad * std::sin( a ) - pr, 2.0f * pr, 2.0f * pr );
    }
    g.setColour( juce::Colour( 232, 178, 92 ) );
    g.fillEllipse( c.x - sun, c.y - sun, 2.0f * sun, 2.0f * sun );
}

// ---------------------------------------------------------------------------------------------------

namespace
{
/**
 * @brief The documents folder of Ephemeris.
 * @return the folder
 */
juce::File documents()
{
    return juce::File::getSpecialLocation( juce::File::userDocumentsDirectory ).getChildFile( "Ephemeris" );
}

/**
 * @brief A tab of several pages (the frame's sub-tabs).
 * @return the sub-tabs
 */
std::unique_ptr<frame::SubTabs> subTabs()
{
    return std::make_unique<frame::SubTabs>( ephui::skin() );
}
}   // namespace

ArrangePage::ArrangePage( EphemerisProcessor &p )
    : m_proc( p ), m_view( std::make_unique<ArrangeView>( p ) ), m_moves( std::make_unique<GestureView>( p ) )
{
    m_which.setColour( juce::Label::textColourId, Ink );
    m_which.setMinimumHorizontalScale( 0.6f );
    addAndMakeVisible( m_which );
    for ( const char *unit : UnitNames )
    {
        // "hands" is the stream of the composed player's moves on the knobs: named so on the panel, where a hand means
        // the headset's (01.10.2026). The stream keeps its name, the sets store it.
        auto *b = m_rerolls.add(
            new juce::TextButton( juce::String( "reroll " ) + ( juce::String( unit ) == "hands" ? "moves" : unit ) ) );
        const juce::String u( unit );
        b->onClick = [this, u] { m_proc.reroll( u ); };
        addAndMakeVisible( b );
    }
    addAndMakeVisible( *m_view );
    addAndMakeVisible( *m_moves );
    startTimerHz( 2 );
}

void ArrangePage::timerCallback()
{
    m_which.setText( m_proc.status(), juce::dontSendNotification );
}

void ArrangePage::resized()
{
    auto r = getLocalBounds().reduced( 10 );
    m_which.setBounds( r.removeFromTop( 22 ) );
    r.removeFromTop( 4 );
    auto      row = r.removeFromTop( 28 );
    const int w = row.getWidth() / std::max( 1, m_rerolls.size() );
    for ( auto *b : m_rerolls ) b->setBounds( row.removeFromLeft( w ).reduced( 2 ) );
    r.removeFromTop( 8 );
    m_view->setBounds( r.removeFromTop( r.getHeight() * 2 / 5 ) );
    r.removeFromTop( 26 );   // the automation's caption (paint)
    m_moves->setBounds( r );
}

void ArrangePage::paint( juce::Graphics &g )
{
    g.setColour( ephui::familyColour( ephui::Family::MOTION ) );
    g.setFont( juce::FontOptions( 11.5f, juce::Font::bold ) );
    g.drawText( "AUTOMATION", m_moves->getX() + 2, m_moves->getY() - 22, 120, 18, juce::Justification::centredLeft );
    g.setColour( Dim );
    g.setFont( juce::FontOptions( 12.0f ) );
    g.drawText( "the composed player's two hands on the knobs, over the whole piece (reroll moves draws them again; "
                "Hold Moves on the Perform page holds them)",
                m_moves->getX() + 110, m_moves->getY() - 22, m_moves->getWidth() - 110, 18,
                juce::Justification::centredLeft );
}

// ---------------------------------------------------------------------------------------------------

ExportPage::ExportPage( EphemerisProcessor &p ) : m_proc( p )
{
    m_wav.onClick = [this] { exportWith( false ); };
    m_stems.onClick = [this] { exportWith( true ); };
    m_save.onClick = [this] { save(); };
    m_load.onClick = [this] { load(); };
    m_wav.setTooltip( "Export what plays: the mix and its MIDI (Ctrl+E)" );
    m_stems.setTooltip( "The mix, its MIDI and a WAV per channel strip and the rooms (large files)" );
    m_save.setTooltip( "Save the set: seed, lengths, rerolls and every changed knob (Ctrl+S)" );
    m_load.setTooltip( "Load a set and compose it (Ctrl+O)" );
    for ( auto *b : { &m_wav, &m_stems } ) addAndMakeVisible( b );
    addAndMakeVisible( m_save );
    addAndMakeVisible( m_load );
    m_status.setColour( juce::Label::textColourId, Ink );
    addAndMakeVisible( m_status );
    m_cue = std::make_unique<ParamPage>( m_proc, std::vector<std::pair<ModuleE, int>>{ { ModuleE::CUE, 0 } }, 1 );
    addAndMakeVisible( *m_cue );
    startTimerHz( 4 );
}

void ExportPage::exportWith( bool stems )
{
    documents().createDirectory();
    m_chooser = std::make_unique<juce::FileChooser>( "Export", documents().getChildFile( "ephemeris.wav" ), "*.wav" );
    m_chooser->launchAsync( juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                            [this, stems]( const juce::FileChooser &fc )
                            {
                                if ( fc.getResult() != juce::File() )
                                    m_proc.exportTo( fc.getResult().withFileExtension( ".wav" ), stems );
                            } );
}

void ExportPage::save()
{
    documents().createDirectory();
    m_chooser = std::make_unique<juce::FileChooser>( "Save set", documents().getChildFile( "set.ephset" ), "*.ephset" );
    m_chooser->launchAsync( juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                            [this]( const juce::FileChooser &fc )
                            {
                                if ( fc.getResult() != juce::File() )
                                    m_proc.saveSet( fc.getResult().withFileExtension( ".ephset" ) );
                            } );
}

void ExportPage::load()
{
    m_chooser = std::make_unique<juce::FileChooser>( "Load set", documents(), "*.ephset" );
    m_chooser->launchAsync( juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                            [this]( const juce::FileChooser &fc )
                            {
                                if ( fc.getResult().existsAsFile() ) m_proc.loadSet( fc.getResult() );
                            } );
}

void ExportPage::timerCallback()
{
    m_status.setText( m_proc.status(), juce::dontSendNotification );
}

void ExportPage::resized()
{
    auto r = getLocalBounds().reduced( 16 );
    auto row = r.removeFromTop( 30 );
    for ( auto *b : { &m_wav, &m_stems } ) b->setBounds( row.removeFromLeft( 170 ).reduced( 3 ) );
    row.removeFromLeft( 24 );
    m_save.setBounds( row.removeFromLeft( frame::IconWidth ).reduced( 3 ) );
    m_load.setBounds( row.removeFromLeft( frame::IconWidth ).reduced( 3 ) );
    r.removeFromTop( 6 );
    m_status.setBounds( r.removeFromTop( 22 ) );
    r.removeFromTop( 110 );   // the text (paint)
    m_cue->setBounds( r.removeFromTop( m_cue->heightFor( r.getWidth() ) ).withWidth( std::min( r.getWidth(), 520 ) ) );
}

void ExportPage::paint( juce::Graphics &g )
{
    g.setColour( Dim );
    g.setFont( juce::FontOptions( 13.0f ) );
    const juce::String text = "The export renders what plays as it was composed (the performer's filter, "
                              "transposition, hold and throw are live "
                              "only) at 48 kHz:\n"
                              "- a 24-bit WAV of the mix and the MIDI file beside it, a channel per part,\n"
                              "- with stems: a 24-bit WAV per channel strip and one for the rooms into <name>_stems; "
                              "their sum is the mix before the master.\n"
                              "An .ephset holds the seed, the lengths, the rerolls and every changed knob.";
    g.drawFittedText( text, getLocalBounds().reduced( 16 ).withTrimmedTop( 64 ).withHeight( 100 ),
                      juce::Justification::topLeft, 6 );
}

// ---------------------------------------------------------------------------------------------------

EphemerisEditor::EphemerisEditor( EphemerisProcessor &p ) : juce::AudioProcessorEditor( p ), m_proc( p ), m_arrange( p )
{
    setLookAndFeel( &m_lnf );
    addAndMakeVisible( m_body );
    // The backdrop (strong behind the header, faint behind the pages), the logo and the name: the frame's (Frame.h).
    m_body.m_painter = [this]( juce::Graphics &g )
    {
        m_backdrop.paint( g, m_body.getLocalBounds(), m_headerBottom, ephui::skin(),
                          frame::Settings::of( "Ephemeris" ).backdrop() );
        drawLogo( g, m_logo );
        frame::drawTitle( g, ephui::skin(), m_title.getBounds().toFloat(), 21.0f );
    };
    m_body.m_onResize = [this] { layoutBody(); };
    frame::Settings::of( "Ephemeris" ).addChangeListener( this );
    ParamStore &s = m_proc.store();
    m_title.setInterceptsMouseClicks( false, false );
    m_body.addAndMakeVisible( m_title );

    auto combo = [&]( juce::ComboBox &box, int id )
    {
        const ParamDesc &d = s.desc( id );
        for ( int c = 0; c <= static_cast<int>( d.m_maxValue ); ++c ) box.addItem( d.m_choices[c], c + 1 );
        m_combos.push_back( std::make_unique<juce::ComboBoxParameterAttachment>( *m_proc.parameter( id ), box ) );
        m_body.addAndMakeVisible( box );
    };
    combo( m_style, s.id( ModuleE::COMPOSE, 0, compose::STYLE ) );
    combo( m_key, s.id( ModuleE::COMPOSE, 0, compose::KEY ) );
    combo( m_scale, s.id( ModuleE::COMPOSE, 0, compose::SCALE ) );
    m_style.setTooltip( "The style the next piece is composed in" );
    m_key.setTooltip( "The key" );
    m_scale.setTooltip( "The scale" );
    // A piece, a concert or a night set (01.10.2026 -- the user: "In der GUI ist es etwas verwirrend, ob man jetzt einen
    // Einzeltrack erzeugt oder einen Mix"; as Parhelion and Totality have it): three buttons that compose what they name,
    // and one length, the one of what is chosen (compose.piece_minutes or compose.concert_minutes).
    m_pieceMode.setTooltip( "Compose a single piece (its length beside)" );
    m_concertMode.setTooltip(
        "Compose a concert: pieces one after another through related keys, along an arc of tension (its "
        "length beside; with Morph To on the Set page from one style to another)" );
    m_nightMode.setTooltip(
        "Compose a night set: pieces in mixed styles along waves of energy, each mixed into the next as a DJ "
        "does it (its length beside, up to 12 hours)" );
    m_pieceMode.onClick = [this] { m_proc.chooseKind( 0 ); };
    m_concertMode.onClick = [this] { m_proc.chooseKind( 1 ); };
    m_nightMode.onClick = [this] { m_proc.chooseKind( 2 ); };
    frame::connectModes( { &m_pieceMode, &m_concertMode, &m_nightMode }, Accent.withAlpha( 0.42f ) );
    for ( auto *b : { &m_pieceMode, &m_concertMode, &m_nightMode } ) m_body.addAndMakeVisible( b );
    m_length.setSliderStyle( juce::Slider::LinearHorizontal );
    m_length.setTextBoxStyle( juce::Slider::TextBoxRight, false, 62, 20 );
    m_length.onValueChange = [this]
    {
        if ( m_syncing ) return;
        const ParamStore &st = m_proc.store();
        m_proc.setFromUi( m_lengthKind == 0 ? st.id( ModuleE::COMPOSE, 0, compose::PIECE_MINUTES )
                                            : st.id( ModuleE::COMPOSE, 0, compose::CONCERT_MINUTES ),
                          static_cast<float>( m_length.getValue() ) );
    };
    m_length.onDragStart = [this] { m_proc.beginStep( "Length" ); };   // a drag is one step, not one per pixel
    m_length.onDragEnd = [this] { m_proc.endStep(); };
    m_lengthLabel.setText( "Length", juce::dontSendNotification );
    m_lengthLabel.setColour( juce::Label::textColourId, Dim );
    m_lengthLabel.setJustificationType( juce::Justification::centredRight );
    m_body.addAndMakeVisible( m_length );
    m_body.addAndMakeVisible( m_lengthLabel );
    showLength( m_proc.chosenKind() );

    m_compose.onClick = [this] { m_proc.compose(); };
    m_sets.onClick = [this] { browseSets(); };
    m_sets.setTooltip( "The concerts: the factory's 256 -- each style as a concert, along an arc, as an album, on a "
                       "journey to another style, through a long night, and the night sets mixing the styles -- and "
                       "yours. The one taken is composed and plays." );
    m_body.addAndMakeVisible( m_sets );
    m_seed.onClick = [this] { m_proc.newSeed(); };
    m_play.onClick = [this] { m_proc.setPlaying( !m_proc.isPlaying() ); };
    m_compose.setTooltip( "Compose with the knobs as they stand, the seed and the rerolls" );
    m_seed.setTooltip( "A new seed: another piece (or concert) from the same settings" );
    m_play.setTooltip( "Play and stop (Space)" );
    m_body.addAndMakeVisible( m_compose );
    for ( auto *b : { &m_seed, &m_play } ) m_body.addAndMakeVisible( b );
    // Mute, as in Phosphene: silence at the output; EPH_MUTE (or the screenshot mode) holds it on.
    m_mute.setClickingTogglesState( true );
    m_mute.setToggleState( m_proc.muted(), juce::dontSendNotification );
    m_mute.setEnabled( !m_proc.muteForced() ||
                       std::getenv( "EPH_SHOT" ) != nullptr );   // (a picture shows it as a player finds it)
    m_mute.setColour( juce::TextButton::buttonOnColourId, ephui::colour::Red.withAlpha( 0.55f ) );
    m_mute.setTooltip( m_proc.muteForced() ? "Muted by EPH_MUTE: an automated run makes no sound"
                                           : "Silence the output" );
    m_mute.onClick = [this] { m_proc.setMuted( m_mute.getToggleState() ); };
    m_body.addAndMakeVisible( m_mute );
    m_play.setColour( juce::TextButton::buttonColourId, Accent.withAlpha( 0.2f ) );
    m_compose.setColour( juce::TextButton::buttonColourId, Accent.withAlpha( 0.12f ) );
    // The update check (the settings switch it): once a day it asks GitHub for the latest release; a newer one shows here.
    m_update.setColour( juce::HyperlinkButton::textColourId, Accent );
    m_update.setTooltip( "Open the release page" );
    m_body.addChildComponent( m_update );
    // The tools at the right end of the second row: undo, redo, help, settings -- and the headset, while one sends.
    m_undo.onClick = [this] { m_proc.undo(); };
    m_redo.onClick = [this] { m_proc.redo(); };
    m_help.onClick = [this] { showHelp( m_helpView == nullptr || !m_helpView->isVisible() ); };
    m_settings.onClick = [this] { showSettings(); };
    m_headsetIcon.onClick = [this]
    {
        for ( int i = 0; i < m_tabs.getNumTabs(); ++i )
            if ( m_tabs.getTabNames()[i] == "Perform" ) m_tabs.setCurrentTabIndex( i );
    };
    m_search.onClick = [this] { openPalette(); };
    for ( auto *b : { &m_undo, &m_redo, &m_search, &m_help, &m_settings } ) m_body.addAndMakeVisible( b );
    m_body.addChildComponent( m_headsetIcon );
    m_rerolls.setColour( juce::Label::textColourId, Dim );
    m_rerolls.setJustificationType( juce::Justification::centredRight );
    m_status.setColour( juce::Label::textColourId, Ink );
    m_status.setMinimumHorizontalScale( 0.8f );
    m_body.addAndMakeVisible( m_rerolls );
    m_body.addAndMakeVisible( m_status );

    m_body.addAndMakeVisible( m_arrange );
    using MT = ModuleE;
    auto scrolling = [this]( std::vector<std::pair<MT, int>> groups, int instances )
    {
        return std::make_unique<ScrollingPage>( std::make_unique<ParamPage>( m_proc, std::move( groups ), instances ) );
    };
    auto page = [&]( const char *name, std::vector<std::pair<MT, int>> groups, int instances )
    { m_tabs.addTab( name, Panel, scrolling( std::move( groups ), instances ).release(), true ); };
    page( "Set", { { MT::COMPOSE, 0 } }, 1 );
    m_tabs.addTab( "Arrange", Panel, new ArrangePage( m_proc ), true );
    m_tabs.addTab( "Rack", Panel,
                   new RackPage( m_proc, std::make_unique<ParamPage>(
                                             m_proc, std::vector<std::pair<MT, int>>{ { MT::ROW, 0 } }, Rows ) ),
                   true );
    {
        // The synths of the rack and around it, one tab with a page each (the frame's sub-tabs).
        auto synths = subTabs();
        auto add = [&]( const char *name, MT m, int instances, ephui::Family family )
        {
            synths->add(
                name, [scrolling, m, instances]
                { return std::unique_ptr<juce::Component>( scrolling( { { m, 0 } }, instances ) ); },
                ephui::familyColour( family ) );
        };
        add( "Row Voices", MT::VOICE, Rows, ephui::Family::SOURCE );
        add( "Lead", MT::LEAD, 1, ephui::Family::FILTER );
        add( "Drone", MT::DRONE, 1, ephui::Family::SPACE );
        add( "Poly", MT::POLY, 1, ephui::Family::MOTION );
        add( "Tape Keys", MT::TAPE, 1, ephui::Family::SOURCE );
        add( "Strings", MT::STRINGS, 1, ephui::Family::ENVELOPE );
        m_tabs.addTab( "Synths", Panel, synths.release(), true );
    }
    page( "Atmosphere", { { MT::ATMOS, 0 } }, 1 );
    page( "Drums", { { MT::DRUMS, 0 } }, 1 );
    {
        auto rooms = subTabs();
        rooms->add(
            "Echo + Spring",
            [scrolling]
            {
                return std::unique_ptr<juce::Component>(
                    scrolling( { { MT::ECHO, 0 }, { MT::SPRING, 0 }, { MT::ECHO2, 0 } }, 1 ) );
            },
            ephui::familyColour( ephui::Family::MOTION ) );
        rooms->add(
            "Hall",
            [scrolling]
            {
                return std::unique_ptr<juce::Component>(
                    scrolling( { { MT::REVERB, 0 }, { MT::BLEND, 0 }, { MT::EARLY, 0 }, { MT::SHIMMER, 0 } }, 1 ) );
            },
            ephui::familyColour( ephui::Family::SPACE ) );
        m_tabs.addTab( "Rooms", Panel, rooms.release(), true );
    }
    {
        auto mixer = subTabs();
        mixer->add( "Console",
                    [this] { return std::unique_ptr<juce::Component>( std::make_unique<MixerConsole>( m_proc ) ); } );
        mixer->add( "Master",
                    [scrolling] { return std::unique_ptr<juce::Component>( scrolling( { { MT::MASTER, 0 } }, 1 ) ); } );
        m_tabs.addTab( "Mixer", Panel, mixer.release(), true );
    }
    m_tabs.addTab( "Perform", Panel,
                   new PerformPage( m_proc, std::make_unique<ParamPage>(
                                                m_proc, std::vector<std::pair<MT, int>>{ { MT::PERFORM, 0 } }, 1 ) ),
                   true );
    m_tabs.addTab( "Export", Panel, new ExportPage( m_proc ), true );
    m_tabs.addTab( "Style", Panel,
                   new StylePage( m_proc, std::make_unique<ParamPage>(
                                              m_proc, std::vector<std::pair<MT, int>>{ { MT::CUSTOM, 0 } }, 1 ) ),
                   true );
    m_tabs.setOutline( 0 );
    m_body.addAndMakeVisible( m_tabs );
    // The family's ways around the panel (15.10.2026, Frame.h): the info line at the bottom, and over everything while
    // open the command palette, the preset browser and the first steps.
    m_info = std::make_unique<frame::InfoLine>( ephui::skin(), m_body );
    m_info->m_describe = [this]( int id, juce::String &name, juce::String &text )
    { return describeControl( id, name, text ); };
    m_body.addAndMakeVisible( *m_info );
    for ( juce::Component *c :
          { static_cast<juce::Component *>( &m_palette ), static_cast<juce::Component *>( &m_browser ),
            static_cast<juce::Component *>( &m_tour ) } )
        m_body.addChildComponent( c );
    m_tour.m_onDone = [] { frame::Settings::of( "Ephemeris" ).setValue( "tourDone", "1" ); };
    frame::keepKeysForEditor( m_body );
    setWantsKeyboardFocus( true );

    setResizable( true, true );
    setResizeLimits( 800, 520, 4800, 3100 );
    setSize( 1280, 860 );   // the family's window size (01.10.2026)

    if ( const char *shot = std::getenv( "EPH_SHOT" ) )
    {
        m_shotPath = shot;
        if ( const char *tab = std::getenv( "EPH_TAB" ) )
            m_tabs.setCurrentTabIndex( juce::String( tab ).getIntValue() );
        // EPH_SUBTAB: the page of a tab of several (Synths, Rooms, Mixer).
        if ( const char *sub = std::getenv( "EPH_SUBTAB" ) )
            if ( auto *st = dynamic_cast<frame::SubTabs *>( m_tabs.getCurrentContentComponent() ) )
                st->show( juce::String( sub ).getIntValue() );
        // EPH_SHOT_SIZE ("1600x2400"): a larger window, so a long page shows whole.
        if ( const char *size = std::getenv( "EPH_SHOT_SIZE" ) )
        {
            const juce::String sz( size );
            setSize( sz.upToFirstOccurrenceOf( "x", false, false ).getIntValue(),
                     sz.fromFirstOccurrenceOf( "x", false, false ).getIntValue() );
        }
        if ( std::getenv( "EPH_SHOT_HELP" ) != nullptr ) showHelp( true );
        if ( std::getenv( "EPH_SHOT_SETS" ) != nullptr ) browseSets();   // the concerts' browser (17.10.2026)
        if ( std::getenv( "EPH_SHOT_HEADSET" ) != nullptr )
        {   // the headset's controls in the pictures: hands as if one sent them
            frame::Hands hands;
            hands.m_height[0] = 0.62f;
            hands.m_height[1] = 0.8f;
            hands.m_tracked[0] = hands.m_tracked[1] = true;
            m_proc.headset().inject( hands );
            m_shotHands = true;
        }
    }
    startTimerHz( 15 );
}

EphemerisEditor::~EphemerisEditor()
{
    stopTimer();
    frame::Settings::of( "Ephemeris" ).removeChangeListener( this );
    setLookAndFeel( nullptr );
}

void EphemerisEditor::paint( juce::Graphics &g )
{
    g.fillAll( Back );
}

void EphemerisEditor::resized()
{
    // The body at the design size (1180 x 760), scaled to the window by its height, and as wide as the window then
    // allows: a larger window shows the panel larger, a wider one gives the pages more room.
    const float scale = juce::jlimit(
        0.5f, 4.0f,
        std::min( static_cast<float>( getWidth() ) / 1180.0f, static_cast<float>( getHeight() ) / 760.0f ) );
    m_body.setTransform( juce::AffineTransform::scale( scale ) );
    m_body.setBounds( 0, 0, juce::roundToInt( static_cast<float>( getWidth() ) / scale ),
                      juce::roundToInt( static_cast<float>( getHeight() ) / scale ) );
}

void EphemerisEditor::parentHierarchyChanged()
{
    // As Phosphene and Noctuary: a maximise button beside the other two, on the next turn of the message loop (the
    // standalone's window is still putting its content in when this is called). A host's window finds nothing here.
    juce::MessageManager::callAsync(
        [safe = juce::Component::SafePointer<EphemerisEditor>( this )]
        {
            if ( safe == nullptr ) return;
            auto *window = safe->findParentComponentOfClass<juce::DocumentWindow>();
            if ( window != nullptr )
                window->setTitleBarButtonsRequired( juce::DocumentWindow::minimiseButton |
                                                        juce::DocumentWindow::maximiseButton |
                                                        juce::DocumentWindow::closeButton,
                                                    false );
            // The standalone's window back where it was left (15.10.2026; not in the screenshot and report modes).
            if ( std::getenv( "EPH_SHOT" ) == nullptr && std::getenv( "EPH_PAGE_REPORT" ) == nullptr )
                safe->m_window.attach( *safe, "Ephemeris" );
            safe->layoutBody();
        } );
}

bool EphemerisEditor::standalone() const
{
    return findParentComponentOfClass<juce::DocumentWindow>() != nullptr;
}

bool EphemerisEditor::fullScreen() const
{
    auto *window = findParentComponentOfClass<juce::DocumentWindow>();
    return window != nullptr && juce::Desktop::getInstance().getKioskModeComponent() == window;
}

void EphemerisEditor::toggleFullScreen()
{
    auto *window = findParentComponentOfClass<juce::DocumentWindow>();
    if ( window == nullptr ) return;
    juce::Desktop::getInstance().setKioskModeComponent( fullScreen() ? nullptr : window, false );
    grabKeyboardFocus();
}

ExportPage *EphemerisEditor::exportPage() const
{
    for ( int i = 0; i < m_tabs.getNumTabs(); ++i )
        if ( auto *e = dynamic_cast<ExportPage *>( m_tabs.getTabContentComponent( i ) ) ) return e;
    return nullptr;
}

bool EphemerisEditor::keyPressed( const juce::KeyPress &key )
{
    frame::Keys k;
    if ( standalone() )
    {   // in a host, Space and F11 are the host's
        k.m_playStop = [this] { m_proc.setPlaying( !m_proc.isPlaying() ); };
        k.m_fullScreen = [this] { toggleFullScreen(); };
    }
    k.m_undo = [this] { m_proc.undo(); };
    k.m_redo = [this] { m_proc.redo(); };
    k.m_help = [this] { showHelp( m_helpView == nullptr || !m_helpView->isVisible() ); };
    k.m_escape = [this]
    {
        if ( m_helpView != nullptr && m_helpView->isVisible() )
            showHelp( false );
        else if ( fullScreen() )
            toggleFullScreen();
    };
    k.m_save = [this]
    {
        if ( auto *e = exportPage() ) e->save();
    };
    k.m_open = [this]
    {
        if ( auto *e = exportPage() ) e->load();
    };
    k.m_exportFile = [this]
    {
        if ( auto *e = exportPage() ) e->exportWith( false );
    };
    k.m_palette = [this] { openPalette(); };
    return frame::handleKey( key, k );
}

void EphemerisEditor::showHelp( bool on )
{
    if ( on && m_helpView == nullptr )
    {
        m_helpView = std::make_unique<frame::HelpView>(
            juce::String::fromUTF8( EphemerisData::chapters_txt, EphemerisData::chapters_txtSize ), ephui::skin() );
        m_helpView->m_onClose = [this] { showHelp( false ); };
        m_helpView->m_extraTopics = [this]
        {
            std::vector<std::pair<juce::String, juce::String>> t;
            juce::Component                                   *page = m_tabs.getCurrentContentComponent();
            juce::String                                       name = m_tabs.getCurrentTabName();
            if ( auto *st = dynamic_cast<frame::SubTabs *>( page ) )
            {
                name << ": " << st->name( st->current() );
                page = st->page();
            }
            if ( auto *sp = dynamic_cast<ScrollingPage *>( page ) )
                t.emplace_back( "This tab: " + name, sp->page().describe() );
            t.emplace_back( "Keys", frame::keysText() );
            t.emplace_back( "Headset (Meta Quest)",
                            frame::headsetGrammar( "the moves held and let go (Hold Moves)", {} ) + "\n\n" +
                                m_proc.headset().statusText() );
            return t;
        };
        m_body.addChildComponent( *m_helpView );
    }
    m_help.setToggleState( on, juce::dontSendNotification );
    if ( m_helpView == nullptr ) return;
    if ( on ) m_helpView->refresh();
    m_helpView->setVisible( on );
    layoutBody();
    if ( on )
        m_helpView->grabKeyboardFocus();
    else
        grabKeyboardFocus();
}

void EphemerisEditor::showSettings()
{
    frame::SettingsMenu m;
    m.m_app = "Ephemeris";
    m.m_version = JucePlugin_VersionString;
    m.m_updatesOn = [this] { return m_updates->enabled(); };
    m.m_setUpdates = [this]( bool on ) { m_updates->setEnabled( on ); };
    m.m_canFullScreen = [this] { return standalone(); };
    m.m_isFullScreen = [this] { return fullScreen(); };
    m.m_toggleFullScreen = [this] { toggleFullScreen(); };
    m.m_setWindowScale = [this]( float k )
    {
        if ( !fullScreen() ) setSize( juce::roundToInt( 1280.0f * k ), juce::roundToInt( 860.0f * k ) );
    };
    m.m_headsetStatus = [this] { return m_proc.headset().statusText(); };
    if ( m_proc.wrapperType ==
         juce::AudioProcessor::wrapperType_Standalone )   // Ableton Link, the standalone only (02.10.2026)
        m.m_linkStatus = [this] { return m_proc.linkStatus(); };
    m.m_jamStatus = [this] { return m_proc.jamStatus(); };   // the family jam (02.10.2026), plugin and standalone
    m.m_about = []
    {
        return juce::String(
            "Berlin School sequencer music, composed and synthesised.\ngithub.com/reneweller-coding/Ephemeris" );
    };
    m.m_moreItems = [this]( juce::PopupMenu &pm )
    {
        pm.addItem( "Search: any control, preset or action (Ctrl+K) ...", [this] { openPalette(); } );
        pm.addItem( "First steps (the tour) ...", [this] { startTour(); } );
    };
    m.show( m_settings );
}

void EphemerisEditor::changeListenerCallback( juce::ChangeBroadcaster * )
{
    layoutBody();
    m_body.repaint();
}   // the settings changed

void EphemerisEditor::showLength( int kind )
{
    // A piece: 4 to 40 minutes; a concert or a night set: 20 minutes to 12 hours, its first two hours over most of the way.
    const juce::ScopedValueSetter<bool> quiet( m_syncing, true );
    m_lengthKind = kind;
    if ( kind != 0 )
    {
        m_length.setRange( 20.0, 720.0, 1.0 );
        m_length.setSkewFactorFromMidPoint( 90.0 );
        m_length.textFromValueFunction = []( double v ) { return juce::String( juce::roundToInt( v ) ) + " min"; };
        m_length.setTooltip( kind == 2 ? "The night set's length (Compose night set makes it)"
                                       : "The concert's length (Compose concert makes it)" );
    }
    else
    {
        m_length.setRange( 4.0, 40.0, 0.5 );
        m_length.setSkewFactor( 1.0 );
        m_length.textFromValueFunction = []( double v ) { return juce::String( v, 1 ) + " min"; };
        m_length.setTooltip( "The piece's length (Compose piece makes it)" );
    }
    m_length.valueFromTextFunction = []( const juce::String &t ) { return t.getDoubleValue(); };
    const ParamStore &st = m_proc.store();
    m_length.setValue( st.get( kind != 0 ? st.id( ModuleE::COMPOSE, 0, compose::CONCERT_MINUTES )
                                         : st.id( ModuleE::COMPOSE, 0, compose::PIECE_MINUTES ) ),
                       juce::dontSendNotification );
    m_length.updateText();
}

void EphemerisEditor::layoutBody()
{
    auto          area = m_body.getLocalBounds().reduced( 10 );
    frame::Header h;
    h.m_logo = &m_logo;
    h.m_title = &m_title;
    h.m_titleWidth = static_cast<int>( frame::titleWidth( ephui::skin(), 21.0f ) ) + 12;
    h.m_choices = { { &m_style, 100 }, { &m_key, 56 }, { &m_scale, 104 } };
    h.m_modes = { { &m_pieceMode, 54 }, { &m_concertMode, 68 }, { &m_nightMode, 74 } };
    h.m_lengthLabel = &m_lengthLabel;
    h.m_length = &m_length;
    h.m_actions = { { &m_sets, 80 }, { &m_compose, 126 }, { &m_seed, frame::IconWidth } };
    h.m_play = &m_play;
    h.m_mute = &m_mute;
    h.m_status = &m_status;
    h.m_curation = &m_rerolls;
    h.m_update = &m_update;
    h.m_tools = { &m_headsetIcon, nullptr, &m_undo, &m_redo, nullptr, &m_search, &m_help, &m_settings };
    frame::layoutHeader( area, h );
    m_headerBottom = area.getY();
    // The info line along the bottom (15.10.2026); the overlays over the whole body.
    if ( m_info != nullptr )
    {
        m_info->setBounds( area.removeFromBottom( frame::InfoLine::Height ) );
        area.removeFromBottom( 4 );
    }
    for ( juce::Component *c :
          { static_cast<juce::Component *>( &m_palette ), static_cast<juce::Component *>( &m_browser ),
            static_cast<juce::Component *>( &m_tour ) } )
        c->setBounds( m_body.getLocalBounds() );
    // The overview, unless the settings fold it away (01.10.2026): then the pages get its height.
    const bool overview = frame::Settings::of( "Ephemeris" ).overview();
    m_arrange.setVisible( overview );
    m_arrange.setBounds( area.removeFromTop( overview ? 96 : 0 ) );   // 96 as the family's (110 until 01.10.2026)
    area.removeFromTop( overview ? 8 : 4 );
    m_tabs.setBounds( area );
    if ( m_helpView != nullptr ) m_helpView->setBounds( area );
}

void EphemerisEditor::timerCallback()
{
    // The first steps on the first start (not in an automated run: the screenshots, the report, EPH_MUTE).
    if ( m_tourTicks >= 0 && ++m_tourTicks > 25 )
    {
        m_tourTicks = -1;
        if ( m_shotPath.isEmpty() && std::getenv( "EPH_PAGE_REPORT" ) == nullptr && !m_proc.muteForced() &&
             frame::Settings::of( "Ephemeris" ).value( "tourDone" ).isEmpty() )
            startTour();
    }
    if ( m_shotHands ) m_proc.headset().inject( m_proc.headset().hands() );
    m_status.setText( m_proc.status(), juce::dontSendNotification );
    m_rerolls.setText( m_proc.curationText(), juce::dontSendNotification );
    {
        // A newer version, where the check found one.
        const juce::String v = m_updates->enabled() ? m_updates->newer() : juce::String();
        if ( v.isNotEmpty() && m_update.getButtonText() != "Version " + v + " available" )
        {
            m_update.setButtonText( "Version " + v + " available" );
            m_update.setURL( juce::URL( m_updates->page() ) );
        }
        if ( m_update.isVisible() != v.isNotEmpty() )
        {
            m_update.setVisible( v.isNotEmpty() );
            layoutBody();
        }
    }
    // Undo and redo say what they would take back; the headset's sign shows while one is there (or always, if asked).
    {
        const juce::String u = m_proc.undoName(), r = m_proc.redoName();
        m_undo.setEnabled( u.isNotEmpty() );
        m_redo.setEnabled( r.isNotEmpty() );
        m_undo.setTooltip( u.isEmpty() ? juce::String( "Undo (Ctrl+Z)" ) : "Undo: " + u + " (Ctrl+Z)" );
        m_redo.setTooltip( r.isEmpty() ? juce::String( "Redo (Ctrl+Y)" ) : "Redo: " + r + " (Ctrl+Y)" );
        const bool hs = m_proc.headset().shown( frame::Settings::of( "Ephemeris" ).headset() );
        if ( m_headsetIcon.isVisible() != hs )
        {
            m_headsetIcon.setVisible( hs );
            layoutBody();
        }
    }
    m_play.show( m_proc.isPlaying() ? frame::IconButton::IconE::STOP : frame::IconButton::IconE::PLAY,
                 m_proc.isPlaying() ? "Stop (Space)" : "Play (Space)" );
    // The choice and its length as the parameters have them (a loaded set and a host move them too); Compose names
    // what it makes, and is lit while what plays is something else.
    {
        const int kind = m_proc.chosenKind();
        if ( kind != m_lengthKind ) showLength( kind );
        m_pieceMode.setToggleState( kind == 0, juce::dontSendNotification );
        m_concertMode.setToggleState( kind == 1, juce::dontSendNotification );
        m_nightMode.setToggleState( kind == 2, juce::dontSendNotification );
        const ParamStore &st = m_proc.store();
        const double      v = st.get( kind != 0 ? st.id( ModuleE::COMPOSE, 0, compose::CONCERT_MINUTES )
                                                : st.id( ModuleE::COMPOSE, 0, compose::PIECE_MINUTES ) );
        if ( !m_length.isMouseButtonDown() && std::abs( m_length.getValue() - v ) > 1.0e-3 )
        {
            const juce::ScopedValueSetter<bool> quiet( m_syncing, true );
            m_length.setValue( v, juce::dontSendNotification );
        }
        static const char *const Compose[3] = { "Compose piece", "Compose concert", "Compose night set" };
        m_compose.setButtonText( Compose[kind] );
        const juce::Colour c =
            Accent.withAlpha( kind != m_proc.playingKind() && !m_proc.isComposing() ? 0.42f : 0.12f );
        if ( m_compose.findColour( juce::TextButton::buttonColourId ) != c )
            m_compose.setColour( juce::TextButton::buttonColourId, c );
    }
    // A screenshot shows the switch as a player finds it: the run is muted all the same (EPH_SHOT forces it).
    const bool shown = m_proc.muted() && m_shotPath.isEmpty();
    m_mute.setToggleState( shown, juce::dontSendNotification );
    m_mute.show( shown ? frame::IconButton::IconE::SPEAKER_OFF : frame::IconButton::IconE::SPEAKER,
                 shown ? ( m_proc.muteForced() ? "Muted by EPH_MUTE: an automated run makes no sound"
                                               : "Muted: click to hear the output again" )
                       : "Silence the output" );
    m_compose.setEnabled( !m_proc.isComposing() );
    // The test mode: the recording, when full, is written and the standalone quits.
    if ( m_proc.recordingDone() )
    {
        m_proc.writeRecording();
        if ( juce::JUCEApplicationBase::isStandaloneApp() ) juce::JUCEApplicationBase::quit();
        return;
    }
    // The screenshot mode: wait for the first piece, then draw the panel into a file and quit.
    // EPH_SHOT_AT (a beat) moves the playhead there first, so the picture can show the middle of a piece.
    if ( m_shotPath.isNotEmpty() && !m_proc.isComposing() && m_shotTicks == 0 )
        if ( const char *at = std::getenv( "EPH_SHOT_AT" ) ) m_proc.seekTo( std::atof( at ) );
    // EPH_PAGE_REPORT=\<file\> (01.10.2026): how far every page reaches past the window, a line each (Frame.h, pageReport),
    // then quit -- which page still scrolls at the usual size.
    if ( const char *report = std::getenv( "EPH_PAGE_REPORT" ); report != nullptr && !m_proc.isComposing() )
    {
        static int reportTicks = 0;
        if ( ++reportTicks == 8 )
        {
            juce::File( report ).replaceWithText( frame::pageReport( m_tabs ) );
            if ( juce::JUCEApplicationBase::isStandaloneApp() ) juce::JUCEApplicationBase::quit();
        }
    }
    // EPH_SHOT_FULL: the window grows until nothing of the page in front scrolls, so the picture shows all of it.
    if ( m_shotPath.isNotEmpty() && !m_proc.isComposing() && ( m_shotTicks == 6 || m_shotTicks == 12 ) &&
         std::getenv( "EPH_SHOT_FULL" ) != nullptr )
    {
        std::function<int( juce::Component & )> overflow = [&]( juce::Component &c )
        {
            int most = 0;
            if ( auto *v = dynamic_cast<juce::Viewport *>( &c ) )
                if ( auto *inner = v->getViewedComponent() ) most = inner->getHeight() - v->getMaximumVisibleHeight();
            for ( auto *child : c.getChildren() ) most = std::max( most, overflow( *child ) );
            return most;
        };
        if ( auto *page = m_tabs.getCurrentContentComponent() )
            if ( const int more = overflow( *page ); more > 0 ) setSize( getWidth(), getHeight() + more + 4 );
    }
    if ( m_shotPath.isNotEmpty() && !m_proc.isComposing() && ++m_shotTicks > 20 )
    {
        const juce::Image img = createComponentSnapshot( getLocalBounds() );
        juce::File        f( m_shotPath );
        f.deleteFile();
        juce::FileOutputStream out( f );
        juce::PNGImageFormat().writeImageToStream( img, out );
        out.flush();
        m_shotPath = {};
        if ( juce::JUCEApplicationBase::isStandaloneApp() ) juce::JUCEApplicationBase::quit();
    }
}

// --------------------------------------------------------------------------------------------------- ways around the panel

void EphemerisEditor::openPalette()
{
    if ( m_browser.isVisible() ) m_browser.close();
    m_palette.open( paletteCommands() );
}

bool EphemerisEditor::revealId( int id )
{
    return frame::revealControl( m_tabs, id, Accent );
}

std::vector<frame::CommandPalette::Command> EphemerisEditor::paletteCommands()
{
    using Command = frame::CommandPalette::Command;
    std::vector<Command> out;
    auto action = [&out]( const juce::String &title, const juce::String &detail, std::function<void()> f )
    { out.push_back( { title, detail, "Action", false, [f]( const juce::String & ) { f(); } } ); };
    action( "Play / stop", "Space", [this] { m_proc.setPlaying( !m_proc.isPlaying() ); } );
    action( "Compose piece", "a single piece, its length in the header",
            [this]
            {
                m_proc.chooseKind( 0 );
                m_proc.compose();
            } );
    action( "Compose concert", "pieces in a row, an evening's concert",
            [this]
            {
                m_proc.chooseKind( 1 );
                m_proc.compose();
            } );
    action( "Compose night set", "a concert through the night",
            [this]
            {
                m_proc.chooseKind( 2 );
                m_proc.compose();
            } );
    action( "New seed", "another piece from the same settings", [this] { m_proc.newSeed(); } );
    action( "Undo", "Ctrl+Z", [this] { m_proc.undo(); } );
    action( "Redo", "Ctrl+Y", [this] { m_proc.redo(); } );
    action( "Mute / unmute the output", "the speaker", [this] { m_proc.setMuted( !m_proc.muted() ); } );
    action( "Save the set", "Ctrl+S",
            [this]
            {
                if ( auto *e = exportPage() ) e->save();
            } );
    action( "Open a set", "Ctrl+O",
            [this]
            {
                if ( auto *e = exportPage() ) e->load();
            } );
    action( "Export WAV + MIDI", "Ctrl+E",
            [this]
            {
                if ( auto *e = exportPage() ) e->exportWith( false );
            } );
    action( "Export with stems", "a WAV per voice",
            [this]
            {
                if ( auto *e = exportPage() ) e->exportWith( true );
            } );
    action( "Help", "F1: the manual by topic", [this] { showHelp( true ); } );
    action( "Settings", "the window, the headset, the jam, Link", [this] { showSettings(); } );
    action( "First steps", "the tour of the panel", [this] { startTour(); } );
    action( "Overview above the tabs", "show it or fold it away",
            []
            {
                auto &s = frame::Settings::of( "Ephemeris" );
                s.setOverview( !s.overview() );
            } );
    if ( standalone() ) action( "Full screen", "F11", [this] { toggleFullScreen(); } );
    // The pages, and the small tabs of the pages of several.
    const juce::StringArray names = m_tabs.getTabNames();
    for ( int t = 0; t < m_tabs.getNumTabs(); ++t )
    {
        out.push_back( { names[t], "page", "Page", false,
                         [this, t]( const juce::String & ) { m_tabs.setCurrentTabIndex( t ); } } );
        if ( auto *st = findSubTabs( m_tabs.getTabContentComponent( t ) ) )
            for ( int j = 0; j < st->count(); ++j )
                out.push_back( { names[t] + ": " + st->name( j ), "page", "Page", false,
                                 [this, t, j]( const juce::String & )
                                 {
                                     m_tabs.setCurrentTabIndex( t );
                                     if ( auto *s2 = findSubTabs( m_tabs.getTabContentComponent( t ) ) ) s2->show( j );
                                 } } );
    }
    // Every control: its page opens and it lights up; a number after its name sets it.
    const ParamStore &s = m_proc.store();
    for ( int id = 0; id < s.count(); ++id )
    {
        if ( m_proc.parameter( id ) == nullptr ) continue;
        const ParamDesc &d = s.desc( id );
        const bool       number = d.m_curve != CurveE::CHOICE && d.m_curve != CurveE::TOGGLE;
        out.push_back( { d.m_name, moduleLabel( s, id ), "Control", number, [this, id]( const juce::String &n )
                         {
                             if ( n.isNotEmpty() )
                                 m_proc.applyKnobs( juce::String( m_proc.store().desc( id ).m_name ),
                                                    { { id, n.getFloatValue() } } );
                             revealId( id );
                             if ( m_info != nullptr ) m_info->showControl( id );
                         } } );
    }
    // Every preset of every synth (on the first row or instance), the player's own first.
    for ( int mi = 0; mi < static_cast<int>( ModuleE::COUNT ); ++mi )
    {
        const auto                      m = static_cast<ModuleE>( mi );
        const std::vector<SoundPreset> &list = factoryPresets( m );
        if ( list.empty() ) continue;
        const juce::String             where = moduleLabel( s, s.id( m, 0, 0 ) );
        const std::vector<SoundPreset> yours = m_proc.userPresets( m );
        for ( size_t k = 0; k < yours.size(); ++k )
            out.push_back( { yours[k].m_name, where + " preset, yours", "Yours", false,
                             [this, m, k]( const juce::String & )
                             {
                                 const std::vector<SoundPreset> now = m_proc.userPresets( m );
                                 if ( k >= now.size() ) return;
                                 m_proc.applyPresetValues( m, 0, now[k] );
                                 frame::PresetBrowser::played( "Ephemeris", synthKey( m_proc.store(), m ),
                                                               juce::String( now[k].m_name ) );
                                 revealId( m_proc.store().id( m, 0, 0 ) );
                             } } );
        for ( size_t i = 0; i < list.size(); ++i )
            out.push_back( { list[i].m_name, where + " preset, " + list[i].m_group, "Preset", false,
                             [this, m, i]( const juce::String & )
                             {
                                 m_proc.applyPreset( m, 0, static_cast<int>( i ) );
                                 frame::PresetBrowser::played( "Ephemeris", synthKey( m_proc.store(), m ),
                                                               juce::String( factoryPresets( m )[i].m_name ) );
                                 revealId( m_proc.store().id( m, 0, 0 ) );
                             } } );
    }
    return out;
}

bool EphemerisEditor::describeControl( int id, juce::String &name, juce::String &text )
{
    const ParamStore &s = m_proc.store();
    if ( id < 0 || id >= s.count() ) return false;
    const ParamDesc &d = s.desc( id );
    auto             fmt = [&d]( float v )
    {
        return juce::String( v, std::abs( v ) >= 100.0f  ? 0
                                : std::abs( v ) >= 10.0f ? 1
                                                         : 2 ) +
               ( d.m_unit[0] != 0 ? juce::String( " " ) + d.m_unit : juce::String() );
    };
    name = d.m_name;
    text = moduleLabel( s, id ) + "    now " + juce::String( s.format( id ) );
    if ( d.m_curve != CurveE::CHOICE && d.m_curve != CurveE::TOGGLE )
    {
        text << "    " << fmt( d.m_minValue ) << " .. " << fmt( d.m_maxValue ) << ", default "
             << fmt( s.defaultValue( id ) );
        // Where it plays now, when the composed player's moves take it away from where it stands.
        const float played = m_proc.playedNormalised( id );
        if ( !std::isnan( played ) && std::abs( played - s.toNormalised( id, s.get( id ) ) ) > 0.004f )
            text << "    plays at " << fmt( s.fromNormalised( id, played ) ) << " (the player's moves)";
    }
    // What moves it: the voice's matrix rows on it.
    {
        juce::String       by;
        const std::string &k = s.key( id );
        const std::string  prefix = k.substr( 0, k.find( '.' ) );
        for ( const char *stem : { ".mx", ".mod" } )
            for ( int n = 1; n <= 32; ++n )
            {
                const int src = s.find( prefix + stem + std::to_string( n ) + "_src" );
                if ( src < 0 || src + 2 >= s.count() ) break;
                const int which = s.getInt( src );
                if ( which <= 0 ) continue;
                const auto &dd = s.desc( src + 1 );
                const int   t = s.getInt( src + 1 );
                if ( dd.m_choices == nullptr || t <= 0 || t > static_cast<int>( dd.m_maxValue ) ) continue;
                const juce::String target = frame::detail::loose( dd.m_choices[t] ),
                                   knob = frame::detail::loose( d.m_name );
                if ( !( knob == target || knob.endsWith( target ) ||
                        ( target.length() >= 4 && knob.startsWith( target ) ) ) )
                    continue;
                const auto &sd = s.desc( src );
                by << ( by.isEmpty() ? "" : ", " )
                   << ( sd.m_choices != nullptr ? juce::String( sd.m_choices[which] ) : juce::String( which ) ) << " ("
                   << ( s.get( src + 2 ) >= 0.0f ? "+" : "" ) << juce::String( s.get( src + 2 ), 2 ) << ")";
            }
        if ( by.isNotEmpty() ) text << "    moved by " << by;
    }
    if ( const int cc = m_proc.controllerFor( id ); cc >= 0 ) text << "    MIDI CC " << cc;
    text << "    [" << juce::String( s.key( id ) ) << "]";
    return true;
}

void EphemerisEditor::browsePresets( ModuleE m, int inst, std::function<void( int )> chosen )
{
    if ( m_palette.isVisible() ) m_palette.close();
    const ParamStore          &s = m_proc.store();
    frame::PresetBrowser::Bank bank;
    bank.m_key = synthKey( s, m );
    bank.m_title = moduleLabel( s, s.id( m, inst, 0 ) ) + " presets";
    const std::vector<SoundPreset> yours = m_proc.userPresets( m );
    for ( size_t k = 0; k < yours.size(); ++k )
        bank.m_entries.push_back( { yours[k].m_name, "Yours", {}, 5001 + static_cast<int>( k ), true, {} } );
    const std::vector<SoundPreset> &list = factoryPresets( m );
    for ( size_t i = 0; i < list.size(); ++i )
        bank.m_entries.push_back( { list[i].m_name, list[i].m_group, {}, static_cast<int>( i ) + 1, false, {} } );
    bank.m_preview = [this, m, inst]( const frame::PresetBrowser::Entry &e )
    { m_proc.previewKnobs( knobsOf( m_proc, m, inst, e.m_index ) ); };
    bank.m_endPreview = [this] { m_proc.endPreview(); };
    bank.m_choose = [this, m, inst, chosen]( const frame::PresetBrowser::Entry &e )
    {
        if ( e.m_index > 5000 )
        {
            const std::vector<SoundPreset> now = m_proc.userPresets( m );
            if ( e.m_index - 5001 < static_cast<int>( now.size() ) )
                m_proc.applyPresetValues( m, inst, now[static_cast<size_t>( e.m_index - 5001 )] );
        }
        else
        {
            m_proc.applyPreset( m, inst, e.m_index - 1 );
        }
        if ( chosen ) chosen( e.m_index );
    };
    m_browser.open( std::move( bank ) );
}

void EphemerisEditor::browseSets()
{
    if ( m_palette.isVisible() ) m_palette.close();
    frame::PresetBrowser::Bank bank;
    bank.m_key = "sets";
    bank.m_title = "Concerts";
    // the player's own first, numbered after the factory's
    juce::Array<juce::File> yours = documents().findChildFiles( juce::File::findFiles, true, "*.ephset" );
    yours.sort();
    const int factory = numFactorySets();
    for ( int k = 0; k < yours.size(); ++k )
        bank.m_entries.push_back( { yours[k].getFileNameWithoutExtension(), "Yours", {}, factory + k, true, {} } );
    for ( int i = 0; i < factory; ++i )
    {
        const FactorySet &f = factorySet( i );
        bank.m_entries.push_back( { f.m_name, f.m_group, f.m_section, i, false, f.m_note } );
    }
    bank.m_choose = [this, yours, factory]( const frame::PresetBrowser::Entry &e )
    {
        if ( e.m_index >= factory )
            m_proc.loadSet( yours[e.m_index - factory] );
        else
            m_proc.loadFactorySet( e.m_index );
    };
    m_browser.open( std::move( bank ) );
}

void EphemerisEditor::startTour()
{
    if ( m_helpView != nullptr && m_helpView->isVisible() ) showHelp( false );
    std::vector<frame::Tour::Step> steps;
    steps.push_back(
        { &m_style, "The style",
          "What the next piece is composed in -- the Berlin School's ways, from the sequencer's pulse to the "
          "drifting space -- with its key and scale beside it." } );
    steps.push_back(
        { &m_compose, "Compose",
          "Composes a piece (or a concert, or a night set) from the knobs, the seed and the rerolls. The die "
          "beside it: a new seed, another piece from the same settings." } );
    steps.push_back( { &m_play, "Play", "Plays and stops (Space). The speaker beside it silences the output." } );
    if ( m_arrange.isVisible() )
        steps.push_back(
            { &m_arrange, "The overview",
              "The piece's sections, its voices lit where they play. Click to jump there, the wheel zooms." } );
    steps.push_back( { &m_tabs.getTabbedButtonBar(), "The pages",
                       "Every part of the instrument: the set, the arrangement, the rack of sequencer "
                       "rows, the synths (each with its presets and your own, and its modulation), the "
                       "atmosphere, the drums, the rooms, the mixer, Perform for playing live, Export." } );
    steps.push_back(
        { &m_search, "Search",
          "Ctrl+K: any knob, preset or action by a few letters -- its page opens and it lights up. A number after a "
          "knob's name sets it (\"cutoff 800\")." } );
    if ( m_info != nullptr )
        steps.push_back(
            { m_info.get(), "The info line",
              "What the control under the mouse is: its value, its range, what moves it, its MIDI controller." } );
    steps.push_back( { &m_help, "Help", "F1: the manual by topic, and the knobs of the page in front." } );
    steps.push_back(
        { &m_settings, "Settings",
          "The window's size, the headset, the family jam, Ableton Link -- and these first steps again." } );
    m_tour.start( std::move( steps ) );
}
