/**
 * @file Rack.cpp
 * @brief The sequencer rack.
 * @author Rene Weller
 */
#include "eph/Rack.h"
#include "eph/compose/Harmony.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace eph
{

namespace
{

/// compose.scale order: Aeolian, Dorian, Phrygian, Harmonic Minor, Minor Pentatonic (its own table below),
/// Mixolydian, Lydian, Locrian.
const int Scale7[Scales][7] = {
    { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 1, 3, 5, 7, 8, 10 },
    { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 3, 5, 7, 8, 10 },   // (the pentatonic's place)
    { 0, 2, 4, 5, 7, 9, 10 }, { 0, 2, 4, 6, 7, 9, 11 }, { 0, 1, 3, 5, 6, 8, 10 },
};
const int Pentatonic[5] = { 0, 3, 5, 7, 10 };   ///< the minor pentatonic's semitones

/**
 * @brief @p a / @p b rounded down (towards minus infinity).
 * @param[in] a  the dividend
 * @param[in] b  the divisor, not 0
 * @return the quotient rounded towards minus infinity
 */
int floorDiv( int a, int b )
{
    return a >= 0 ? a / b : -( ( -a + b - 1 ) / b );
}

}   // namespace

int scaleSize( int scale )
{
    return scale == 4 ? 5 : 7;
}

int scaleSemitones( int scale, int degree )
{
    const int n = scaleSize( scale );
    const int oct = floorDiv( degree, n );
    const int d = degree - oct * n;
    const int semis = scale == 4 ? Pentatonic[d] : Scale7[std::clamp( scale, 0, Scales - 1 )][d];
    return oct * 12 + semis;
}

int conjunctionSteps( int a, int b )
{
    return a <= 0 || b <= 0 ? 0 : std::lcm( a, b );
}

void Rack::setup( const ParamStore &p, uint64_t seed )
{
    m_keyRoot = p.getInt( p.id( ModuleE::COMPOSE, 0, compose::KEY ) );
    m_scale = p.getInt( p.id( ModuleE::COMPOSE, 0, compose::SCALE ) );
    m_style = static_cast<StyleE>( p.getInt( p.id( ModuleE::COMPOSE, 0, compose::STYLE ) ) );
    m_shift = m_base = m_degree = 0;
    m_shiftLog.assign( 1, { 0.0, 0 } );
    m_scaleLog.assign( 1, { 0.0, m_scale } );
    for ( int i = 0; i < Rows; ++i )
    {
        Row &r = m_rows[i];
        auto get = [&]( int index ) { return p.get( p.id( ModuleE::ROW, i, index ) ); };
        r.m_length = std::clamp( static_cast<int>( get( row::LENGTH ) ), 1, MaxSteps );
        r.m_divBeats = rowDivisionBeats( static_cast<RowDivisionE>( static_cast<int>( get( row::DIVISION ) ) ) );
        r.m_direction = static_cast<RowDirectionE>( static_cast<int>( get( row::DIRECTION ) ) );
        r.m_octave = static_cast<int>( get( row::OCTAVE ) );
        r.m_transpose = static_cast<int>( get( row::TRANSPOSE ) );
        r.m_chord = 0;
        r.m_mutation = get( row::MUTATION );
        r.m_gate = get( row::GATE ) * 0.01f;
        r.m_running = get( row::ACTIVE ) >= 0.5f;
        r.m_transposer = static_cast<int>( get( row::MODE ) ) == static_cast<int>( RowModeE::TRANSPOSER );
        r.m_startBeat = 0.0;
        r.m_step = 0;
        r.m_pos = 0;
        r.m_dir = 1;
        r.m_mutations = 0;
        r.m_rng.seed( mixSeed( seed, static_cast<uint64_t>( i ) + 1 ) );
        r.m_dice.seed( mixSeed( seed, static_cast<uint64_t>( i ) + 101 ) );
        r.m_lanes.seed( mixSeed( seed, static_cast<uint64_t>( i ) + 201 ) );
        r.m_figure = FigureE::CLASSIC;
        for ( Step &s : r.m_steps ) s = Step{};
    }
    m_position = 0.0;
}

namespace
{

constexpr int Rest = -99;   ///< a rest in a shape

/** @brief The archetypes as eight steps: degrees of a seven-note mode (Rest a rest) and octaves. */
struct Shape
{
    int m_degree[8];   ///< the steps' degrees (Rest a rest)
    int m_octave[8];   ///< their octaves
};

/**
 * @brief The eight steps of figure @p f.
 * @param[in] f  the figure
 * @return its eight steps
 */
const Shape &shapeOf( FigureE f )
{
    static const Shape Shapes[] = {
        { { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 } },         // Classic (unused)
        { { 0, 0, 0, 0, 0, 0, 4, 0 }, { 0, 1, 0, 1, 0, 1, 0, 1 } },         // octave pendulum
        { { 0, 4, 0, 4, 0, 4, 6, 4 }, { 0, 0, 0, 1, 0, 0, 0, 0 } },         // fifth anchor
        { { 0, 0, 0, Rest, 0, 0, 4, Rest }, { 0, 1, 0, 0, 0, 1, 0, 0 } },   // 3+1 pulse
        { { 0, 1, 2, 3, 4, 6, 7, 4 }, { 0, 0, 0, 0, 0, 0, 0, 0 } },         // stairs up
        { { 7, 6, 4, 3, 2, 1, 0, 4 }, { 0, 0, 0, 0, 0, 0, 0, 0 } },         // stairs down
        { { 0, 2, 4, 5, 7, 4, 2, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0 } },         // colour (the 5 is the mode's degree)
        { { 0, 1, 0, 4, 0, 1, 0, 6 }, { 0, 0, 0, 0, 0, 0, 0, 0 } },         // Phrygian push
        { { 0, 2, 4, 7, -2, 0, 2, 5 }, { 0, 0, 0, 0, 0, 0, 0, 0 } },        // spiral: i, then VI
    };
    return Shapes[std::clamp( static_cast<int>( f ), 0, static_cast<int>( FigureE::SPIRAL ) )];
}

/**
 * @brief A degree of a seven-note figure in @p scale: itself, or the nearest of the pentatonic's.
 * @param[in] scale  the scale
 * @param[in] d      the degree of a seven-note figure
 * @return the degree in the scale
 */
int fromSeven( int scale, int d )
{
    if ( scaleSize( scale ) == 7 ) return d;
    static const int Pent[7] = { 0, 1, 1, 2, 3, 3, 4 };
    const int        oct = floorDiv( d, 7 );
    return Pent[d - 7 * oct] + 5 * oct;
}

/**
 * @brief Share of a row's steps that are probability gates, by style (the style guide's 4.6).
 * @param[in] style  the style
 * @return the share of probability gates, 0..1
 */
float chanceShare( StyleE style )
{
    static const float Share[] = { 0.05f, 0.05f, 0.08f, 0.20f, 0.15f };   // Cosmic, Doom, Melodic, Modern, Drift
    return Share[std::clamp( static_cast<int>( style ), 0, 4 )];
}

/**
 * @brief Share of a row's steps that play a quantised random note, by style (the style guide's 4.6).
 * @param[in] style  the style
 * @return the share of random notes, 0..1
 */
float randomShare( StyleE style )
{
    static const float Share[] = { 0.03f, 0.03f, 0.05f, 0.12f, 0.20f };   // Cosmic, Doom, Melodic, Modern, Drift
    return Share[std::clamp( static_cast<int>( style ), 0, 4 )];
}

/**
 * @brief A note through the quantiser: root, fifth, octave, seventh, fourth or third, the lower ones likelier.
 * @param[in]     scale  the scale
 * @param[in,out] dice   the random stream
 * @return the degree drawn
 */
int quantisedDegree( int scale, Rng &dice )
{
    static const int   Degrees[6] = { 0, 4, 7, 6, 3, 2 };
    static const float Weights[6] = { 3.0f, 3.0f, 2.0f, 1.0f, 1.0f, 1.0f };
    float              u = dice.uniform() * 11.0f;
    int                k = 0;
    while ( k < 5 && ( u -= Weights[k] ) > 0.0f ) ++k;
    return fromSeven( scale, Degrees[k] );
}

template <size_t N>
/**
 * @brief One of the figures @p f, drawn from @p g with the weights @p w.
 * @param[in]     f  the figures
 * @param[in]     w  their weights
 * @param[in,out] g  the random stream
 * @return the figure drawn
 */
FigureE drawFigure( const FigureE ( &f )[N], const float ( &w )[N], Rng &g )
{
    float total = 0.0f;
    for ( float x : w ) total += x;
    float u = g.uniform() * total;
    for ( size_t i = 0; i < N; ++i )
    {
        u -= w[i];
        if ( u <= 0.0f && w[i] > 0.0f ) return f[i];
    }
    return f[N - 1];
}

}   // namespace

const char *figureName( FigureE f )
{
    static const char *const Names[] = { "Classic",     "Octave Pendulum", "Fifth Anchor",  "3+1 Pulse", "Stairs Up",
                                         "Stairs Down", "Colour",          "Phrygian Push", "Spiral",    "Canon" };
    return Names[std::clamp( static_cast<int>( f ), 0, static_cast<int>( FigureE::COUNT ) - 1 )];
}

int characterDegree( int scale )
{
    static const int Char[Scales] = { 5, 5, 1, 6, 4, 6, 3, 4 };
    return Char[std::clamp( scale, 0, Scales - 1 )];
}

void Rack::generate( int row, RowRoleE role )
{
    Row &r = m_rows[row];
    for ( bool &t : r.m_thinned ) t = false;
    // The modulation lane: a length against the row's (3, 5, 7 or 12, never the row's own), a few bright steps over
    // a darker ground -- accents of timbre that wander against the notes and make the groove. Its depth by style.
    {
        static const float Depth[] = { 0.8f, 0.6f, 0.7f, 1.0f, 0.5f };   // Cosmic, Doom, Melodic, Modern, Drift
        const float        depth =
            role == RowRoleE::TRANSPOSER ? 0.0f : Depth[std::clamp( static_cast<int>( m_style ), 0, 4 )];
        const int lengths[4] = { 3, 5, 7, 12 };
        int       len = lengths[r.m_rng.below( 4 )];
        if ( len == r.m_length ) len = len == 12 ? 7 : len + 2;
        r.m_modLength = len;
        r.m_modPos = 0;
        for ( int i = 0; i < MaxSteps; ++i )
        {
            const float u = r.m_rng.uniform();
            r.m_mod[i] = i >= len ? 0.0f
                                  : depth * ( u < 0.3f ? 0.9f + 0.6f * r.m_rng.uniform()
                                                       : ( u < 0.6f ? 0.0f : -0.5f + 0.3f * r.m_rng.uniform() ) );
        }
        // The decay lane: long open notes, plucked short ones, a length against both (Boddy's "the shape of the AR
        // envelopes" as the main control). From its own stream, so the patterns stay as drawn.
        static const int dLengths[5] = { 3, 5, 7, 9, 12 };
        int              dl = dLengths[r.m_lanes.below( 5 )];
        for ( int tries = 0; ( dl == r.m_length || dl == len ) && tries < 5; ++tries )
            dl = dLengths[( tries + 1 + r.m_lanes.below( 4 ) ) % 5];
        r.m_decayLength = dl;
        r.m_decayPos = 0;
        for ( int i = 0; i < MaxSteps; ++i )
        {
            const float u = r.m_lanes.uniform();
            r.m_decay[i] = i >= dl || role == RowRoleE::TRANSPOSER
                               ? 0.0f
                               : ( u < 0.35f ? 0.6f + 0.6f * r.m_lanes.uniform()
                                             : ( u < 0.7f ? 0.0f : -0.8f + 0.4f * r.m_lanes.uniform() ) );
        }
    }
    Rng             &g = r.m_rng;
    const int        n = scaleSize( m_scale );
    int              walk = 0;   // the Walk role's current degree
    std::vector<int> roots;
    if ( role == RowRoleE::TRANSPOSER )
        roots = drawProgression( m_style, m_scale, r.m_length,
                                 std::max( 1, static_cast<int>( std::lround( r.m_divBeats / BeatsPerBar ) ) ), g );
    // The figure (the style guide's 4.2).
    using F = FigureE;
    const bool phrygian = m_scale == 2 || m_scale == 7;
    const bool canon = row > 0 && !m_rows[row - 1].m_transposer;
    switch ( role )
    {
    case RowRoleE::BASS:
    {
        static const F     f[] = { F::CLASSIC, F::OCTAVE_PENDULUM, F::FIFTH_ANCHOR, F::THREE_PLUS_ONE };
        static const float w[] = { 0.25f, 0.30f, 0.30f, 0.15f };
        r.m_figure = drawFigure( f, w, g );
        break;
    }
    case RowRoleE::COUNTER:
    {
        static const F f[] = { F::CLASSIC,       F::STAIRS_UP, F::STAIRS_DOWN, F::COLOUR,
                               F::PHRYGIAN_PUSH, F::SPIRAL,    F::CANON,       F::FIFTH_ANCHOR };
        const float w[] = { 0.15f, 0.12f, 0.10f, 0.18f, phrygian ? 0.20f : 0.0f, 0.12f, canon ? 0.15f : 0.0f, 0.08f };
        r.m_figure = drawFigure( f, w, g );
        break;
    }
    case RowRoleE::WALK:
    {
        static const F f[] = { F::CLASSIC, F::STAIRS_UP, F::STAIRS_DOWN, F::COLOUR, F::SPIRAL, F::CANON };
        const float    w[] = { 0.35f, 0.15f, 0.15f, 0.15f, 0.10f, canon ? 0.10f : 0.0f };
        r.m_figure = drawFigure( f, w, g );
        break;
    }
    default: r.m_figure = F::CLASSIC; break;
    }
    // The canon: the row before, three steps later or a fifth up.
    const bool canonLater = g.uniform() < 0.5f;
    const Row &before = m_rows[std::max( 0, row - 1 )];
    for ( int i = 0; i < MaxSteps; ++i )
    {
        Step s;
        s.m_velocity = 0.72f + 0.12f * g.uniform();
        if ( role == RowRoleE::TRANSPOSER )
        {
            s.m_degree = i < static_cast<int>( roots.size() ) ? roots[static_cast<size_t>( i )] : 0;
            r.m_steps[i] = s;
            continue;
        }
        if ( r.m_figure == F::CANON )
        {
            const int   L = std::max( 1, before.m_length );
            const Step &src = before.m_steps[canonLater ? ( ( i - 3 ) % L + L ) % L : i % L];
            s.m_degree = src.m_degree + ( canonLater ? 0 : fromSeven( m_scale, 4 ) );
            s.m_octave = src.m_octave;
            s.m_gate = src.m_gate;
        }
        else if ( r.m_figure != F::CLASSIC )
        {
            const Shape &sh = shapeOf( r.m_figure );
            int          d = sh.m_degree[i % 8];
            if ( d == Rest )
            {
                s.m_gate = false;
                d = 0;
            }
            s.m_degree = fromSeven( m_scale, d );
            if ( r.m_figure == F::COLOUR && i % 8 == 3 )
            {
                // The mode's own degree, above the fifth: B in D Dorian, Eb' in D Phrygian.
                const int c = characterDegree( m_scale );
                s.m_degree = n == 7 && c < 4 ? c + 7 : c;
            }
            s.m_octave = sh.m_octave[i % 8];
            // The second eight and on: the same figure with the odd step an octave away.
            if ( i >= 8 && i % 8 != 0 && g.uniform() < 0.2f ) s.m_octave = s.m_octave == 0 ? 1 : 0;
        }
        else
        {
            switch ( role )
            {
            case RowRoleE::BASS:
            {
                // Mostly the root; an octave up on about a third of the steps; now and then the fifth,
                // the seventh or the third.
                const float u = g.uniform();
                if ( i == 0 )
                {
                    s.m_degree = 0;
                    s.m_octave = 0;
                }
                else if ( u < 0.30f )
                    s.m_octave = 1;
                else if ( u < 0.42f )
                {
                    const int pick[3] = { 4, 6, 2 };
                    s.m_degree = pick[g.below( 3 )] % n;
                }
                break;
            }
            case RowRoleE::COUNTER:
            {
                // Chord tones of the tonic (1, 3, 5, octave) as a rising and falling figure.
                const int chord[4] = { 0, 2, 4, n };
                const int up = i % 6 < 3 ? i % 6 : 6 - i % 6;
                s.m_degree = chord[( up + ( g.uniform() < 0.2f ? 1 : 0 ) ) % 4];
                break;
            }
            case RowRoleE::WALK:
            {
                const float u = g.uniform();
                if ( i > 0 ) walk += u < 0.4f ? 1 : ( u < 0.8f ? -1 : ( g.uniform() < 0.5f ? 3 : -2 ) );
                walk = std::clamp( walk, -2, n + 2 );
                s.m_degree = walk;
                break;
            }
            default: break;
            }
        }
        if ( role == RowRoleE::BASS ) s.m_slide = i != 0 && r.m_figure != F::THREE_PLUS_ONE && g.uniform() < 0.05f;
        r.m_steps[i] = s;
    }
    if ( role == RowRoleE::TRANSPOSER ) return;
    // The first step is the root or the fifth: it names the centre.
    if ( r.m_figure != F::CANON && r.m_steps[0].m_degree != 0 && r.m_steps[0].m_degree != fromSeven( m_scale, 4 ) )
        r.m_steps[0].m_degree = 0;
    r.m_steps[0].m_gate = true;
    // Rests, accents and probability gates per eight steps.
    const bool  threeThreeTwo = g.uniform() < 0.25f;
    const float share = chanceShare( m_style ), randomly = randomShare( m_style );
    for ( int b = 0; b < MaxSteps; b += 8 )
    {
        int rests = 0;
        for ( int i = b; i < b + 8; ++i ) rests += r.m_steps[i].m_gate ? 0 : 1;
        const float u = g.uniform();
        const int   want = role == RowRoleE::BASS ? ( u < 0.3f ? 0 : ( u < 0.8f ? 1 : 2 ) )
                                                  : ( u < 0.45f ? 1 : ( u < 0.85f ? 2 : 3 ) );
        for ( int tries = 0; rests < want && tries < 16; ++tries )
        {
            const int i = b + 1 + g.below( 7 );
            if ( i % 4 == 0 || !r.m_steps[i].m_gate ) continue;   // the first of every four always sounds
            r.m_steps[i].m_gate = false;
            ++rests;
        }
        for ( int i = b; i < b + 8; ++i )
        {
            Step     &s = r.m_steps[i];
            const int k = i - b;
            s.m_accent = threeThreeTwo ? ( k == 0 || k == 3 || k == 6 ) : ( k == 0 || k == 4 );
            if ( role != RowRoleE::BASS && !threeThreeTwo && k == 4 ) s.m_accent = g.uniform() < 0.5f;
            // The chance and the random notes only on the counter rows after the first: the bass and the main
            // sequence stay exactly what they are, the ground the ear locks onto.
            const bool loose = role != RowRoleE::BASS && row >= 2;
            if ( i % 4 != 0 && g.uniform() < share && loose ) s.m_chance = 0.6f + 0.3f * g.uniform();
            if ( i != 0 && g.uniform() < randomly && loose ) s.m_random = true;
        }
    }
    std::copy( r.m_steps, r.m_steps + MaxSteps, r.m_theme );
}

int Rack::rootNote( const Row &r ) const
{
    // The key's root in A2 .. G#3, then the row's octave: A minor with octave -1 is A1 (55 Hz).
    return 45 + ( ( m_keyRoot - 9 + 12 ) % 12 ) + 12 * r.m_octave;
}

void Rack::mutate( Row &r, int index, double beat, std::vector<RackEvent> &log )
{
    const int   i = r.m_rng.below( r.m_length );
    Step       &s = r.m_steps[i];
    const float what = r.m_rng.uniform();
    if ( r.m_transposer )
    {
        // A transposer mutates its roots only, and never its first step: the piece keeps its tonic.
        const std::vector<int> &roots = progressionRoots( m_style );
        if ( i != 0 )
            s.m_degree = r.m_rng.uniform() < 0.4f
                             ? 0
                             : roots[static_cast<size_t>( r.m_rng.below( static_cast<int>( roots.size() ) ) )];
    }
    else if ( what < 0.45f )
    {
        const int n = scaleSize( m_scale );
        const int choices[5] = { 0, 2, 4, 6 % n, n - 1 };
        s.m_degree = choices[r.m_rng.below( 5 )];
    }
    else if ( what < 0.70f )
        s.m_octave = s.m_octave == 0 ? 1 : 0;
    else if ( what < 0.88f )
    {
        if ( i != 0 ) s.m_gate = !s.m_gate;
    }
    else
        s.m_accent = !s.m_accent;
    ++r.m_mutations;
    log.push_back( { beat, index, RackOpE::MUTATE, i } );
}

void Rack::advance( Row &r, int index, double beat, std::vector<RackEvent> &log )
{
    const int L = r.m_length;
    switch ( r.m_direction )
    {
    case RowDirectionE::FORWARD: r.m_pos = ( r.m_pos + 1 ) % L; break;
    case RowDirectionE::BACKWARD: r.m_pos = ( r.m_pos - 1 + L ) % L; break;
    case RowDirectionE::PENDULUM:
        if ( L == 1 )
            r.m_pos = 0;
        else
        {
            if ( r.m_pos + r.m_dir < 0 || r.m_pos + r.m_dir >= L ) r.m_dir = -r.m_dir;
            r.m_pos += r.m_dir;
        }
        break;
    case RowDirectionE::RANDOM_WALK: r.m_pos = ( r.m_pos + ( r.m_rng.uniform() < 0.5f ? L - 1 : 1 ) ) % L; break;
    default: r.m_pos = ( r.m_pos + 1 ) % L; break;
    }
    ++r.m_step;
    r.m_modPos = ( r.m_modPos + 1 ) % std::max( 1, r.m_modLength );
    r.m_decayPos = ( r.m_decayPos + 1 ) % std::max( 1, r.m_decayLength );
    // A cycle ends every `length` steps, whatever the direction: that is when the register shifts.
    // Hypnosis (25.09.2026): a row changes only where a 16-bar block begins -- at the first end of its cycle on or
    // after the mark -- so a pattern holds for sixteen bars and one thing changes at a time; the chance is the
    // row's mutation, doubled for the longer wait.
    if ( r.m_step % L == 0 && r.m_mutation > 0.0f && beat + r.m_divBeats >= r.m_nextMutation - 1e-9 )
    {
        while ( r.m_nextMutation <= beat + r.m_divBeats + 1e-9 ) r.m_nextMutation += 64.0;
        if ( r.m_rng.uniform() < std::min( 1.0f, 2.0f * r.m_mutation ) ) mutate( r, index, beat, log );
    }
}

void Rack::playStep( int index, Row &r, Score &score, std::vector<RackEvent> &log )
{
    const double beat = nextStepBeat( r );
    const Step  &s = r.m_steps[r.m_pos];
    if ( r.m_transposer )
    {
        if ( s.m_gate )
        {
            m_degree = s.m_degree + 12 * s.m_octave;
            m_shift = m_base + m_degree;
            if ( m_shiftLog.empty() || m_shiftLog.back().second != m_shift ) m_shiftLog.push_back( { beat, m_shift } );
        }
    }
    else if ( s.m_gate && ( s.m_chance >= 1.0f || r.m_dice.uniform() < s.m_chance ) )
    {
        // A ratchet splits the step into quick triggers, each a little softer.
        const int sub = std::max( 1, s.m_ratchet );
        const int degree = s.m_random ? quantisedDegree( m_scale, r.m_dice ) : s.m_degree;
        for ( int k = 0; k < sub; ++k )
        {
            NoteEvent e;
            e.m_beat = beat + r.m_divBeats * static_cast<double>( k ) / sub;
            e.m_part = rowPart( index );
            e.m_pitch = std::clamp( rootNote( r ) + r.m_transpose + m_shift +
                                        scaleSemitones( m_scale, degree + r.m_chord ) + 12 * s.m_octave,
                                    0, 127 );
            e.m_accent = s.m_accent && k == 0;
            e.m_bright = r.m_mod[r.m_modPos];
            e.m_decay = r.m_decay[r.m_decayPos];
            e.m_velocity =
                std::min( 1.0f, s.m_velocity + ( e.m_accent ? 0.15f : 0.0f ) - 0.06f * static_cast<float>( k ) );
            e.m_slide = s.m_slide && sub == 1;
            // A slide holds into the next step so the voice glides instead of retriggering.
            e.m_length = sub > 1
                             ? 0.5 * r.m_divBeats / sub
                             : r.m_divBeats * ( s.m_slide ? 1.05 : static_cast<double>( std::max( 0.05f, r.m_gate ) ) );
            score.m_notes.push_back( e );
        }
    }
    advance( r, index, beat, log );
}

void Rack::run( Score &score, double endBeat )
{
    std::vector<RackEvent> log;
    // The composer's events in this span, by beat. Chosen by beat rather than by index, so the score
    // may be re-sorted between two runs (the mutations appended below move) without an event being
    // applied twice.
    std::vector<RackEvent> ev;
    for ( const RackEvent &e : score.m_rack )
        if ( e.m_op != RackOpE::MUTATE && e.m_beat >= m_position && e.m_beat < endBeat ) ev.push_back( e );
    std::stable_sort( ev.begin(), ev.end(),
                      []( const RackEvent &a, const RackEvent &b ) { return a.m_beat < b.m_beat; } );
    size_t cursor = 0;
    for ( ;; )
    {
        // The earliest pending thing: a rack event or a step of a running row.
        double best = endBeat;
        int    bestRow = -1;
        for ( int i = 0; i < Rows; ++i )
        {
            if ( !m_rows[i].m_running ) continue;
            const double b = nextStepBeat( m_rows[i] );
            // A transposer steps first at equal beats, so the downbeat already sounds in the new root.
            if ( b < best || ( b == best && bestRow >= 0 && m_rows[i].m_transposer && !m_rows[bestRow].m_transposer ) )
            {
                best = b;
                bestRow = i;
            }
        }
        if ( cursor < ev.size() && ev[cursor].m_beat <= best )
        {
            // Rack events first at equal beats: a transposition applies to the step on its beat.
            const RackEvent &e = ev[cursor++];
            if ( e.m_op == RackOpE::KEY )
            {
                m_base = e.m_value;
                m_shift = m_base + m_degree;
                if ( m_shiftLog.back().second != m_shift ) m_shiftLog.push_back( { e.m_beat, m_shift } );
                continue;
            }
            if ( e.m_op == RackOpE::SCALE )
            {
                m_scale = std::clamp( e.m_value, 0, Scales - 1 );
                if ( m_scaleLog.back().second != m_scale ) m_scaleLog.push_back( { e.m_beat, m_scale } );
                continue;
            }
            const int lo = e.m_row < 0 ? 0 : e.m_row, hi = e.m_row < 0 ? Rows - 1 : e.m_row;
            for ( int i = lo; i <= hi && i < Rows; ++i )
            {
                Row &r = m_rows[i];
                switch ( e.m_op )
                {
                case RackOpE::START:
                    r.m_running = true;
                    r.m_startBeat = e.m_beat;
                    r.m_step = 0;
                    r.m_pos = 0;
                    r.m_dir = 1;
                    r.m_nextMutation = e.m_beat + 64.0;
                    break;
                case RackOpE::STOP:
                    r.m_running = false;
                    // A transposer that stops takes the rows home to the tonic.
                    if ( r.m_transposer && m_degree != 0 )
                    {
                        m_degree = 0;
                        m_shift = m_base;
                        m_shiftLog.push_back( { e.m_beat, m_shift } );
                    }
                    break;
                case RackOpE::TRANSPOSE: r.m_transpose = e.m_value; break;
                case RackOpE::CHORD: r.m_chord = e.m_value; break;
                case RackOpE::RATCHET:
                {
                    // Steps drawn anew from the row's dice: two triggers mostly, three or four now and then.
                    for ( Step &st : r.m_steps ) st.m_ratchet = 1;
                    for ( int k = 0, tries = 0; k < e.m_value && tries < 32; ++tries )
                    {
                        Step &st = r.m_steps[1 + r.m_dice.below( std::max( 1, r.m_length - 1 ) )];
                        if ( !st.m_gate || st.m_ratchet > 1 || r.m_length < 2 ) continue;
                        const float u = r.m_dice.uniform();
                        st.m_ratchet = u < 0.7f ? 2 : ( u < 0.9f ? 3 : 4 );
                        ++k;
                    }
                    break;
                }
                case RackOpE::THIN:
                    for ( int k = 0, tries = 0; k < e.m_value && tries < 32 && r.m_length > 1; ++tries )
                    {
                        const int at = 1 + r.m_dice.below( r.m_length - 1 );
                        Step     &st = r.m_steps[at];
                        if ( !st.m_gate ) continue;
                        st.m_gate = false;
                        r.m_thinned[at] = true;
                        ++k;
                    }
                    break;
                case RackOpE::FILL:
                    for ( int k = 0, tries = 0; k < e.m_value && tries < 64 && r.m_length > 1; ++tries )
                    {
                        const int at = 1 + r.m_dice.below( r.m_length - 1 );
                        if ( !r.m_thinned[at] ) continue;
                        r.m_steps[at].m_gate = true;
                        r.m_thinned[at] = false;
                        ++k;
                    }
                    break;
                case RackOpE::DIVISION:
                {
                    // From the next step on: the steps played so far stay where they were.
                    const double div = rowDivisionBeats( static_cast<RowDivisionE>( e.m_value ) );
                    if ( r.m_running ) r.m_startBeat = nextStepBeat( r ) - static_cast<double>( r.m_step ) * div;
                    r.m_divBeats = div;
                    break;
                }
                case RackOpE::SET_LENGTH:
                    r.m_length = std::clamp( e.m_value, 1, MaxSteps );
                    r.m_pos %= r.m_length;
                    break;
                case RackOpE::GATE:
                    // A step switched: never step 0, never a thinned one (a Fill brings those), and half the row keeps
                    // sounding.
                    for ( int k = 0, tries = 0; k < e.m_value && tries < 32 && r.m_length > 2; ++tries )
                    {
                        const int at = 1 + r.m_dice.below( r.m_length - 1 );
                        if ( r.m_thinned[at] ) continue;
                        int sounding = 0;
                        for ( int j = 0; j < r.m_length; ++j ) sounding += r.m_steps[j].m_gate ? 1 : 0;
                        Step &st = r.m_steps[at];
                        if ( st.m_gate && 2 * ( sounding - 1 ) < r.m_length ) continue;
                        st.m_gate = !st.m_gate;
                        ++k;
                    }
                    break;
                case RackOpE::OCTAVE_STEP:
                    // A sounding step an octave off (up mostly, the bass only up), or one that was back where it was.
                    for ( int k = 0, tries = 0; k < e.m_value && tries < 32 && r.m_length > 1; ++tries )
                    {
                        Step &st = r.m_steps[1 + r.m_dice.below( r.m_length - 1 )];
                        if ( !st.m_gate ) continue;
                        st.m_octave = st.m_octave != 0 ? 0 : ( i == 0 || r.m_dice.uniform() < 0.7f ? 1 : -1 );
                        ++k;
                    }
                    break;
                case RackOpE::DIRECTION:
                    r.m_direction = static_cast<RowDirectionE>(
                        std::clamp( e.m_value, 0, static_cast<int>( RowDirectionE::COUNT ) - 1 ) );
                    r.m_dir = 1;
                    break;
                case RackOpE::THEME:
                    // Back to the pattern as drawn; the ratchets of the moment stay, and the thinned steps stay silent.
                    for ( int j = 0; j < MaxSteps; ++j )
                    {
                        const int ratchet = r.m_steps[j].m_ratchet;
                        r.m_steps[j] = r.m_theme[j];
                        r.m_steps[j].m_ratchet = ratchet;
                        if ( r.m_thinned[j] ) r.m_steps[j].m_gate = false;
                    }
                    break;
                default: break;
                }
            }
            continue;
        }
        if ( bestRow < 0 ) break;
        playStep( bestRow, m_rows[bestRow], score, log );
    }
    m_position = endBeat;
    score.m_rack.insert( score.m_rack.end(), log.begin(), log.end() );
}

}   // namespace eph
