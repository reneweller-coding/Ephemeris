/**
 * @file RenderWindow.cpp
 * @brief The render app's window and its length thread.
 * @author Rene Weller
 */
#include "RenderWindow.h"

#include <algorithm>
#include <cmath>

namespace renderkit
{

namespace
{

/**
 * @brief A seed that differs every time it is drawn.
 * @return the seed
 */
std::uint64_t freshSeed()
{
    return static_cast<std::uint64_t>( juce::Time::currentTimeMillis() ) * 2654435761ull + 1ull;
}

/**
 * @brief A folder as the panel shows it: from "Documents" on when it lies there, else its whole path.
 * @param[in] f  the folder
 * @return the text
 */
juce::String folderText( const juce::File &f )
{
    const juce::File docs = juce::File::getSpecialLocation( juce::File::userDocumentsDirectory );
    return f.isAChildOf( docs ) ? docs.getFileName() + juce::File::getSeparatorString() + f.getRelativePathFrom( docs )
                                : f.getFullPathName();
}

/**
 * @brief A word with its first letter capital ("journey" -> "Journey").
 * @param[in] s  the word
 * @return it
 */
juce::String capital( const juce::String &s )
{
    return s.substring( 0, 1 ).toUpperCase() + s.substring( 1 );
}

}   // namespace

// ---- the lengths

LengthWorker::LengthWorker( Source &source ) : juce::Thread( "render app lengths", 64u << 20 ), m_source( source ) {}

LengthWorker::~LengthWorker()
{
    signalThreadShouldExit();
    m_wake.signal();
    stopThread( 60000 );
}

void LengthWorker::ask( const std::vector<Item> &items, const Request &request )
{
    {
        const std::lock_guard<std::mutex> hold( m_lock );
        m_items = items;
        m_request = request;
        m_lengths.assign( items.size(), 0.0 );
        m_known.assign( items.size(), 0 );
        ++m_generation;
        ++m_news;
    }
    if ( !isThreadRunning() ) startThread();
    m_wake.signal();
}

void LengthWorker::prefer( int index )
{
    const std::lock_guard<std::mutex> hold( m_lock );
    m_prefer = index;
}

double LengthWorker::get( int index, bool &known )
{
    const std::lock_guard<std::mutex> hold( m_lock );
    known = index >= 0 && index < static_cast<int>( m_known.size() ) && m_known[static_cast<size_t>( index )] != 0;
    return known ? m_lengths[static_cast<size_t>( index )] : 0.0;
}

int LengthWorker::takeNews()
{
    const std::lock_guard<std::mutex> hold( m_lock );
    const int                         n = m_news;
    m_news = 0;
    return n;
}

void LengthWorker::run()
{
    while ( !threadShouldExit() )
    {
        Item    item;
        Request request;
        int     index = -1, generation = 0;
        {
            const std::lock_guard<std::mutex> hold( m_lock );
            if ( m_prefer >= 0 && m_prefer < static_cast<int>( m_known.size() ) &&
                 m_known[static_cast<size_t>( m_prefer )] == 0 )
                index = m_prefer;
            for ( size_t i = 0; index < 0 && i < m_known.size(); ++i )
                if ( m_known[i] == 0 ) index = static_cast<int>( i );
            if ( index >= 0 )
            {
                item = m_items[static_cast<size_t>( index )];
                request = m_request;
                generation = m_generation;
            }
        }
        if ( index < 0 )
        {   // every row known: wait for the next pass
            m_wake.wait( 500 );
            continue;
        }
        const double                      seconds = m_source.length( item, request );
        const std::lock_guard<std::mutex> hold( m_lock );
        if ( generation != m_generation ) continue;   // the dice changed meanwhile
        m_lengths[static_cast<size_t>( index )] = seconds;
        m_known[static_cast<size_t>( index )] = 1;
        ++m_news;
    }
}

// ---- the window

RenderWindow::RenderWindow( Source &source )
    : m_source( source ), m_palette( source.palette() ), m_columns( source.columns() ), m_queue( source ),
      m_lengths( source )
{
    const Palette &c = m_palette;
    m_look.setColourScheme(
        { c.m_back, c.m_raised, c.m_panel, c.m_edge, c.m_text, c.m_raised, c.m_back, c.m_accent, c.m_text } );
    m_look.setColour( juce::TableHeaderComponent::backgroundColourId, c.m_raised );
    m_look.setColour( juce::TableHeaderComponent::textColourId, c.m_text );
    m_look.setColour( juce::TableHeaderComponent::outlineColourId, c.m_edge );
    m_look.setColour( juce::TextEditor::backgroundColourId, c.m_panel );
    m_look.setColour( juce::TextEditor::outlineColourId, c.m_edge );
    m_look.setColour( juce::ToggleButton::textColourId, c.m_text );
    m_look.setColour( juce::ToggleButton::tickColourId, c.m_accent );
    m_look.setColour( juce::ProgressBar::foregroundColourId, c.m_accent );
    m_look.setColour( juce::ListBox::backgroundColourId, c.m_panel );
    setLookAndFeel( &m_look );

    juce::PropertiesFile::Options o;
    o.applicationName = "render_app";
    o.filenameSuffix = ".settings";
    o.folderName = source.product();
    m_settings = std::make_unique<juce::PropertiesFile>( o );
    m_folder =
        juce::File( m_settings->getValue( "folder", juce::File::getSpecialLocation( juce::File::userDocumentsDirectory )
                                                        .getChildFile( source.product() )
                                                        .getChildFile( "Renders" )
                                                        .getFullPathName() ) );
    m_seed = freshSeed();

    m_title.setText( source.title(), juce::dontSendNotification );
    m_title.setFont( juce::FontOptions( 24.0f, juce::Font::bold ) );
    m_title.setColour( juce::Label::textColourId, c.m_text );
    addAndMakeVisible( m_title );
    m_search.setText( "Search", juce::dontSendNotification );
    m_search.setColour( juce::Label::textColourId, c.m_dim );
    addAndMakeVisible( m_search );
    m_filter.setTextToShowWhenEmpty( "a name, a word: its kind, its style ...", c.m_dim );
    m_filter.onTextChange = [this] { refilter(); };
    addAndMakeVisible( m_filter );
    m_refresh.setTooltip( "Reads your files again (saved in the instrument meanwhile)." );
    m_refresh.onClick = [this] { reload(); };
    addAndMakeVisible( m_refresh );

    m_table.setModel( this );
    m_table.setMultipleSelectionEnabled( true );
    m_table.setRowHeight( 24 );
    auto &h = m_table.getHeader();
    h.addColumn( capital( source.noun() ), NAME, 300, 120, 800 );
    h.addColumn( "Length", LENGTH, 80, 60, 140 );
    for ( size_t k = 0; k < m_columns.size(); ++k )
        h.addColumn( m_columns[k].m_name, FIRST + static_cast<int>( k ), m_columns[k].m_width, 40, 400 );
    h.setSortColumnId( NAME, true );
    h.setStretchToFitActive( true );   // the columns share the table's width: no sideways scrolling
    addAndMakeVisible( m_table );

    m_chosenName.setFont( juce::FontOptions( 18.0f, juce::Font::bold ) );
    m_chosenName.setColour( juce::Label::textColourId, c.m_accent );
    m_chosenName.setMinimumHorizontalScale( 0.7f );
    addAndMakeVisible( m_chosenName );
    m_chosenInfo.setColour( juce::Label::textColourId, c.m_text );
    m_chosenInfo.setJustificationType( juce::Justification::topLeft );
    addAndMakeVisible( m_chosenInfo );

    m_formatLabel.setText( "Format", juce::dontSendNotification );
    m_folderLabel.setText( "Folder", juce::dontSendNotification );
    for ( juce::Label *l : { &m_formatLabel, &m_folderLabel } )
    {
        l->setColour( juce::Label::textColourId, c.m_dim );
        addAndMakeVisible( *l );
    }
    for ( int f = 0; f < static_cast<int>( FormatE::COUNT ); ++f )
        if ( formatAvailable( static_cast<FormatE>( f ) ) )
            m_format.addItem( formatName( static_cast<FormatE>( f ) ), f + 1 );
    m_format.setSelectedId( m_settings->getIntValue( "format", 1 ), juce::dontSendNotification );
    if ( m_format.getSelectedId() == 0 ) m_format.setSelectedId( 1, juce::dontSendNotification );
    addAndMakeVisible( m_format );

    for ( const Option &op : source.options() )
    {
        Control ctl;
        ctl.m_option = op;
        const int value = m_settings->getIntValue( "option_" + op.m_key, op.m_default );
        if ( op.m_choices.isEmpty() )
        {
            ctl.m_toggle = std::make_unique<juce::ToggleButton>( op.m_label );
            ctl.m_toggle->setToggleState( value != 0, juce::dontSendNotification );
            ctl.m_toggle->setTooltip( op.m_tooltip );
            ctl.m_toggle->onClick = [this, length = op.m_length]
            {
                if ( length ) relength();
                updateChosen();
            };
            addAndMakeVisible( *ctl.m_toggle );
        }
        else
        {
            ctl.m_label = std::make_unique<juce::Label>( juce::String(), op.m_label );
            ctl.m_label->setColour( juce::Label::textColourId, c.m_dim );
            ctl.m_label->setTooltip( op.m_tooltip );
            addAndMakeVisible( *ctl.m_label );
            ctl.m_menu = std::make_unique<juce::ComboBox>();
            for ( int k = 0; k < op.m_choices.size(); ++k ) ctl.m_menu->addItem( op.m_choices[k], k + 1 );
            ctl.m_menu->setSelectedId( std::clamp( value, 0, op.m_choices.size() - 1 ) + 1,
                                       juce::dontSendNotification );
            ctl.m_menu->setTooltip( op.m_tooltip );
            ctl.m_menu->onChange = [this, length = op.m_length]
            {
                if ( length ) relength();
                updateChosen();
            };
            addAndMakeVisible( *ctl.m_menu );
        }
        m_controls.push_back( std::move( ctl ) );
    }

    m_dice.setTooltip(
        "Everything the instrument draws -- times, glides, notes, forms -- comes from the dice: new dice, "
        "a new rendering (and a new length). The same dice give the same rendering." );
    m_dice.onClick = [this]
    {
        m_seed = freshSeed();
        relength();
        updateChosen();
    };
    addAndMakeVisible( m_dice );
    m_diceNote.setColour( juce::Label::textColourId, c.m_dim );
    addAndMakeVisible( m_diceNote );

    m_folderText.setText( folderText( m_folder ), juce::dontSendNotification );
    m_folderText.setTooltip( m_folder.getFullPathName() );
    m_folderText.setColour( juce::Label::textColourId, c.m_text );
    m_folderText.setMinimumHorizontalScale( 0.6f );
    addAndMakeVisible( m_folderText );
    m_chooseFolder.onClick = [this]
    {
        m_chooser = std::make_unique<juce::FileChooser>( "Where the renderings go", m_folder );
        m_chooser->launchAsync( juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                [this]( const juce::FileChooser &fc )
                                {
                                    if ( fc.getResult() == juce::File() ) return;
                                    m_folder = fc.getResult();
                                    m_folderText.setText( folderText( m_folder ), juce::dontSendNotification );
                                    m_folderText.setTooltip( m_folder.getFullPathName() );
                                    m_settings->setValue( "folder", m_folder.getFullPathName() );
                                } );
    };
    addAndMakeVisible( m_chooseFolder );
    m_openFolder.onClick = [this]
    {
        m_folder.createDirectory();
        m_folder.startAsProcess();
    };
    addAndMakeVisible( m_openFolder );
    m_render.setColour( juce::TextButton::buttonColourId, c.m_chosen );
    m_render.onClick = [this] { renderChosen(); };
    addAndMakeVisible( m_render );
    m_cancel.onClick = [this] { m_queue.cancelAll(); };
    addAndMakeVisible( m_cancel );
    addAndMakeVisible( m_bar );
    m_status.setColour( juce::Label::textColourId, c.m_text );
    m_status.setJustificationType( juce::Justification::topLeft );
    m_status.setText( "Choose a " + source.noun() +
                          " (several with Ctrl or Shift), then Render. A double click renders "
                          "one at once.",
                      juce::dontSendNotification );
    addAndMakeVisible( m_status );

    reload();
    setSize( 1120, 700 );
    startTimerHz( 10 );
}

RenderWindow::~RenderWindow()
{
    stopTimer();
    m_settings->setValue( "format", m_format.getSelectedId() );
    for ( const Control &ctl : m_controls )
        m_settings->setValue( "option_" + ctl.m_option.m_key, ctl.m_toggle ? ( ctl.m_toggle->getToggleState() ? 1 : 0 )
                                                                           : ctl.m_menu->getSelectedId() - 1 );
    m_settings->saveIfNeeded();
    m_table.setModel( nullptr );
    setLookAndFeel( nullptr );
}

bool RenderWindow::choose( const juce::String &name )
{
    for ( size_t r = 0; r < m_rows.size(); ++r )
        if ( m_items[static_cast<size_t>( m_rows[r] )].m_name == name )
        {
            m_table.selectRow( static_cast<int>( r ) );
            m_table.scrollToEnsureRowIsOnscreen( static_cast<int>( r ) );
            updateChosen();
            return true;
        }
    return false;
}

void RenderWindow::settle()
{
    for ( int t = 0; t < 1200; ++t )
    {
        bool all = true;
        for ( size_t i = 0; all && i < m_items.size(); ++i ) m_lengths.get( static_cast<int>( i ), all );
        if ( all ) break;
        juce::Thread::sleep( 50 );
    }
    m_lengths.takeNews();
    m_table.updateContent();
    updateChosen();
}

void RenderWindow::paint( juce::Graphics &g )
{
    g.fillAll( m_palette.m_back );
    const juce::Rectangle<int> panel = getLocalBounds().removeFromRight( 400 ).reduced( 12 ).withTrimmedTop( 52 );
    g.setColour( m_palette.m_panel );
    g.fillRoundedRectangle( panel.toFloat(), 8.0f );
    g.setColour( m_palette.m_dim );
    g.setFont( juce::FontOptions( 13.0f ) );
    const int x = 24 +
                  juce::GlyphArrangement::getStringWidthInt( juce::Font( juce::FontOptions( 24.0f, juce::Font::bold ) ),
                                                             m_source.title() ) +
                  24;
    g.drawText( m_source.subtitle(), x, 22, std::max( 100, getWidth() - x - 24 ), 22,
                juce::Justification::centredLeft );
}

void RenderWindow::resized()
{
    juce::Rectangle<int> r = getLocalBounds().reduced( 12 );
    m_title.setBounds( r.removeFromTop( 40 ).removeFromLeft( 500 ) );
    juce::Rectangle<int> side = r.removeFromRight( 388 ).reduced( 14, 12 );
    r.removeFromRight( 12 );
    juce::Rectangle<int> search = r.removeFromTop( 30 );
    m_search.setBounds( search.removeFromLeft( 60 ) );
    m_refresh.setBounds( search.removeFromRight( 80 ) );
    search.removeFromRight( 8 );
    m_filter.setBounds( search );
    r.removeFromTop( 8 );
    m_table.setBounds( r );

    auto row = [&side]( int h )
    {
        juce::Rectangle<int> x = side.removeFromTop( h );
        side.removeFromTop( 8 );
        return x;
    };
    m_chosenName.setBounds( side.removeFromTop( 28 ) );
    m_chosenInfo.setBounds( side.removeFromTop( 100 ) );
    juce::Rectangle<int> f = row( 28 );
    m_formatLabel.setBounds( f.removeFromLeft( 80 ) );
    m_format.setBounds( f );
    for ( Control &ctl : m_controls )
        if ( ctl.m_menu )
        {
            juce::Rectangle<int> m = row( 28 );
            ctl.m_label->setBounds( m.removeFromLeft( 80 ) );
            ctl.m_menu->setBounds( m );
        }
    juce::Rectangle<int> d = row( 28 );
    m_dice.setBounds( d.removeFromLeft( 120 ) );
    d.removeFromLeft( 10 );
    m_diceNote.setBounds( d );
    for ( Control &ctl : m_controls )
        if ( ctl.m_toggle ) ctl.m_toggle->setBounds( row( 24 ) );
    juce::Rectangle<int> fo = row( 24 );
    m_folderLabel.setBounds( fo.removeFromLeft( 80 ) );
    m_folderText.setBounds( fo );
    juce::Rectangle<int> b = row( 28 );
    m_chooseFolder.setBounds( b.removeFromLeft( b.getWidth() / 2 - 4 ) );
    b.removeFromLeft( 8 );
    m_openFolder.setBounds( b );
    side.removeFromTop( 6 );
    juce::Rectangle<int> go = row( 36 );
    m_render.setBounds( go.removeFromLeft( go.getWidth() * 2 / 3 - 4 ) );
    go.removeFromLeft( 8 );
    m_cancel.setBounds( go );
    m_bar.setBounds( row( 22 ) );
    m_status.setBounds( side );
}

void RenderWindow::reload()
{
    const juce::String keep = chosen().empty() ? juce::String() : m_items[static_cast<size_t>( chosen()[0] )].m_name;
    m_table.deselectAllRows();
    m_items = m_source.list();
    bool dice = false;
    for ( const Item &it : m_items ) dice = dice || it.m_dice;
    m_dice.setVisible( dice );
    m_diceNote.setVisible( dice );
    relength();   // the new rows' lengths asked for before the rows are sorted by them
    refilter();
    if ( keep.isNotEmpty() ) choose( keep );
}

void RenderWindow::refilter()
{
    // The rows chosen, by what they are: the table keeps row numbers, which a new order or a new search moves to
    // other rows.
    std::vector<char> was( m_items.size(), 0 );
    for ( int i : chosen() )
        if ( i >= 0 && i < static_cast<int>( was.size() ) ) was[static_cast<size_t>( i )] = 1;
    const juce::StringArray words = juce::StringArray::fromTokens( m_filter.getText().toLowerCase(), " ", "" );
    m_rows.clear();
    for ( size_t i = 0; i < m_items.size(); ++i )
    {
        const Item  &it = m_items[i];
        juce::String hay = it.m_name + " " + it.m_kind + " " + it.m_detail + " " + it.m_cells.joinIntoString( " " );
        hay = hay.toLowerCase();
        bool all = true;
        for ( const juce::String &w : words ) all = all && ( w.isEmpty() || hay.contains( w ) );
        if ( all ) m_rows.push_back( static_cast<int>( i ) );
    }
    // The lengths as they stand now (the length thread goes on filling them in): an order that cannot change while
    // it is being sorted.
    std::vector<double> lengths( m_items.size(), -1.0 );
    if ( m_sortColumn == LENGTH )
        for ( size_t i = 0; i < m_items.size(); ++i )
        {
            bool         known = false;
            const double len = m_lengths.get( static_cast<int>( i ), known );
            lengths[i] = known ? len : -1.0;
        }
    const auto key = [this]( int i, int k )
    {
        const Item &it = m_items[static_cast<size_t>( i )];
        return static_cast<int>( it.m_keys.size() ) > k ? it.m_keys[static_cast<size_t>( k )] : std::nan( "" );
    };
    const auto less = [this, &lengths, &key]( int a, int b )
    {
        const Item &x = m_items[static_cast<size_t>( a )], &y = m_items[static_cast<size_t>( b )];
        if ( m_sortColumn == LENGTH )
        {
            const double lx = lengths[static_cast<size_t>( a )], ly = lengths[static_cast<size_t>( b )];
            if ( ( lx < 0.0 ) != ( ly < 0.0 ) ) return ly < 0.0;   // the known first
            return lx < ly;
        }
        const int k = m_sortColumn - FIRST;
        if ( k >= 0 )
        {
            const double kx = key( a, k ), ky = key( b, k );
            if ( std::isnan( kx ) != std::isnan( ky ) ) return std::isnan( ky );   // the numbers first
            if ( !std::isnan( kx ) ) return kx < ky;
            const juce::String tx = k < x.m_cells.size() ? x.m_cells[k] : juce::String();
            const juce::String ty = k < y.m_cells.size() ? y.m_cells[k] : juce::String();
            return tx.compareNatural( ty ) < 0;
        }
        return x.m_name.compareNatural( y.m_name ) < 0;
    };
    std::stable_sort( m_rows.begin(), m_rows.end(),
                      [&]( int a, int b ) { return m_sortForward ? less( a, b ) : less( b, a ); } );
    m_table.updateContent();
    juce::SparseSet<int> rows;
    for ( size_t r = 0; r < m_rows.size(); ++r )
        if ( was[static_cast<size_t>( m_rows[r] )] != 0 )
            rows.addRange( { static_cast<int>( r ), static_cast<int>( r ) + 1 } );
    m_table.setSelectedRows( rows, juce::dontSendNotification );
    m_table.repaint();
    updateChosen();
}

Request RenderWindow::request() const
{
    Request r;
    r.m_seed = m_seed;
    for ( const Control &ctl : m_controls )
        r.m_options[ctl.m_option.m_key] =
            ctl.m_toggle ? ( ctl.m_toggle->getToggleState() ? 1 : 0 ) : ctl.m_menu->getSelectedId() - 1;
    return r;
}

void RenderWindow::relength()
{
    m_lengths.ask( m_items, request() );
    m_table.repaint();
}

std::vector<int> RenderWindow::chosen() const
{
    std::vector<int>           out;
    const juce::SparseSet<int> rows = m_table.getSelectedRows();
    for ( int k = 0; k < rows.size(); ++k )
    {
        const int row = rows[k];
        if ( row >= 0 && row < static_cast<int>( m_rows.size() ) ) out.push_back( m_rows[static_cast<size_t>( row )] );
    }
    return out;
}

void RenderWindow::updateChosen()
{
    const std::vector<int> c = chosen();
    const Request          r = request();
    m_diceNote.setText( "dice " + juce::String( static_cast<juce::int64>( r.m_seed % 1000000ull ) ),
                        juce::dontSendNotification );
    for ( Control &ctl : m_controls )
    {   // a choice that means nothing for what is chosen rests
        bool any = c.empty();
        for ( int i : c ) any = any || m_source.applies( m_items[static_cast<size_t>( i )], ctl.m_option.m_key );
        if ( ctl.m_toggle ) ctl.m_toggle->setEnabled( any );
        if ( ctl.m_menu ) ctl.m_menu->setEnabled( any );
        if ( ctl.m_label ) ctl.m_label->setEnabled( any );
    }
    if ( c.empty() )
    {
        m_lengths.prefer( -1 );
        m_chosenName.setText( "Nothing chosen", juce::dontSendNotification );
        m_chosenInfo.setText( juce::String( static_cast<int>( m_items.size() ) ) + " to choose from.",
                              juce::dontSendNotification );
        return;
    }
    const Item &first = m_items[static_cast<size_t>( c.front() )];
    m_lengths.prefer( c.front() );
    bool         known = false;
    const double len = m_lengths.get( c.front(), known );
    juce::String info = m_source.describe( first, r );
    if ( info.isNotEmpty() ) info << "\n";
    info << "This rendering: "
         << ( known ? ( len > 0.0 ? clockText( len ) : juce::String( "cannot be rendered" ) )
                    : juce::String( "working it out ..." ) );
    if ( c.size() > 1 )
    {
        double all = 0.0;
        bool   every = true;
        for ( int i : c )
        {
            bool k = false;
            all += m_lengths.get( i, k );
            every = every && k;
        }
        info << "\nAll " << juce::String( static_cast<int>( c.size() ) ) << ": "
             << ( every ? clockText( all ) : juce::String( "..." ) );
    }
    m_chosenName.setText( c.size() == 1
                              ? first.m_name
                              : juce::String( static_cast<int>( c.size() ) ) + " chosen, the first: " + first.m_name,
                          juce::dontSendNotification );
    m_chosenInfo.setText( info, juce::dontSendNotification );
}

void RenderWindow::renderChosen()
{
    juce::String name;
    int          waiting = 0, finished = 0;
    if ( !m_queue.status( name, waiting, finished ) && waiting == 0 )
    {   // a new batch: its failures counted afresh
        m_failed = 0;
        m_lastFailure.clear();
    }
    const std::vector<int> c = chosen();
    if ( c.empty() )
    {
        m_status.setText( "Choose a " + m_source.noun() + " first.", juce::dontSendNotification );
        return;
    }
    if ( !m_folder.createDirectory() )
    {
        m_status.setText( "Cannot write into " + m_folder.getFullPathName(), juce::dontSendNotification );
        return;
    }
    std::vector<Job> jobs;
    for ( int i : c )
    {
        Job job;
        job.m_item = m_items[static_cast<size_t>( i )];
        job.m_request = request();
        job.m_format = static_cast<FormatE>( m_format.getSelectedId() - 1 );
        job.m_folder = m_folder;
        jobs.push_back( std::move( job ) );
    }
    m_queue.add( jobs );
}

void RenderWindow::timerCallback()
{
    if ( m_lengths.takeNews() > 0 )
    {
        if ( m_sortColumn == LENGTH )
            refilter();   // sorted by length: the order again as the lengths come in
        else
            m_table.repaint();
        updateChosen();
    }
    juce::String       name;
    int                waiting = 0, finished = 0;
    const bool         busy = m_queue.status( name, waiting, finished );
    const juce::String message = m_queue.takeMessage();
    if ( message.isNotEmpty() && !message.startsWith( "Written" ) )
    {   // kept: the next rendering begins at once and would hide it
        ++m_failed;
        m_lastFailure = message;
    }
    m_cancel.setEnabled( busy || waiting > 0 );
    if ( busy )
    {
        const Renderer &rr = m_queue.m_renderer;
        juce::String    s;
        s << name << "\n";
        if ( rr.m_setup.load() )
        {
            m_progress = -1.0;   // the bar's busy stripes
            s << "Setting it up ...";
        }
        else
        {
            const double done = rr.m_done.load(), total = rr.m_total.load(), speed = rr.m_speed.load();
            m_progress = total > 0.0 ? done / total : 0.0;
            s << clockText( done ) << " of " << clockText( total ) << "   " << juce::String( speed, 1 )
              << "x real time";
            if ( speed > 0.0 ) s << "\nabout " << clockText( ( total - done ) / speed ) << " left";
            const juce::String now = rr.now();
            if ( now.isNotEmpty() ) s << "\n" << now;
        }
        if ( waiting > 0 ) s << "\n" << juce::String( waiting ) << " more waiting";
        if ( finished > 0 ) s << "\n" << juce::String( finished ) << " written";
        if ( m_failed > 0 ) s << "\n" << juce::String( m_failed ) << " failed, the last: " << m_lastFailure;
        m_status.setText( s, juce::dontSendNotification );
    }
    else if ( message.isNotEmpty() )
    {
        m_progress = message.startsWith( "Written" ) ? 1.0 : 0.0;
        juce::String s = message;
        if ( m_failed > 0 && message.startsWith( "Written" ) )
            s << "\n" << juce::String( m_failed ) << " failed, the last: " << m_lastFailure;
        m_status.setText( s, juce::dontSendNotification );
    }
}

int RenderWindow::getNumRows()
{
    return static_cast<int>( m_rows.size() );
}

void RenderWindow::paintRowBackground( juce::Graphics &g, int row, int, int, bool selected )
{
    g.fillAll( selected ? m_palette.m_chosen : ( row % 2 ? m_palette.m_row : m_palette.m_panel ) );
}

void RenderWindow::paintCell( juce::Graphics &g, int row, int column, int w, int h, bool )
{
    if ( row < 0 || row >= static_cast<int>( m_rows.size() ) ) return;
    const int    index = m_rows[static_cast<size_t>( row )];
    const Item  &it = m_items[static_cast<size_t>( index )];
    juce::String text;
    bool         right = false, dim = column != NAME;
    if ( column == NAME )
        text = it.m_name;
    else if ( column == LENGTH )
    {
        bool         known = false;
        const double len = m_lengths.get( index, known );
        text = known ? ( len > 0.0 ? clockText( len ) : juce::String( "--" ) ) : juce::String( "..." );
        right = true;
        dim = !known;
    }
    else
    {
        const int k = column - FIRST;
        if ( k >= 0 && k < it.m_cells.size() ) text = it.m_cells[k];
        right = k >= 0 && k < static_cast<int>( m_columns.size() ) && m_columns[static_cast<size_t>( k )].m_right;
    }
    g.setColour( dim ? m_palette.m_dim : m_palette.m_text );
    g.setFont( juce::FontOptions( 14.0f ) );
    g.drawText( text, 6, 0, w - 10, h, right ? juce::Justification::centredRight : juce::Justification::centredLeft,
                true );
}

void RenderWindow::sortOrderChanged( int column, bool forward )
{
    m_sortColumn = column;
    m_sortForward = forward;
    refilter();
}

void RenderWindow::selectedRowsChanged( int )
{
    updateChosen();
}

void RenderWindow::cellDoubleClicked( int, int, const juce::MouseEvent & )
{
    renderChosen();
}

}   // namespace renderkit
