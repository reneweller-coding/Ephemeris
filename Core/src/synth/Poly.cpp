/**
 * @file Poly.cpp
 * @brief The pad synth (Poly.h).
 *
 * Everything runs sample by sample on the sample count's grid -- the control values every sixteen samples, the
 * drift every 512, each key drawing its drift from its own generator -- so the sound does not depend on how the
 * host cuts the blocks (the engine's rule).
 * @author Rene Weller
 */
#include "eph/synth/Poly.h"
#include "eph/Vec.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace eph
{

namespace
{
constexpr int ControlStep = 16;   ///< samples between two control updates
constexpr int Drift = 512;        ///< samples between two steps of the drift

/**
 * @brief One sample of the keys' filter lanes of model @p m (not the comb), a register at a time: the arithmetic of
 *        FilterLane::tick lane by lane, so the sound is the same to the bit; the makeup is the caller's.
 * @param[in]     m     the filter model
 * @param[in,out] fv    the lanes' first state, per register
 * @param[in,out] fs    the lanes' second state, per register
 * @param[in]     x     the lanes' input samples
 * @param[in]     g     the lanes' cutoff coefficients
 * @param[in]     k     the lanes' resonance coefficients
 * @param[in]     mode  the lanes' modes
 * @param[out]    y     the lanes' output samples
 */
template <class VT, int N>
void runLanes( FilterModelE m, float ( *fv )[N], float ( *fs )[N], const float *x, const float *g, const float *k,
               const float *mode, float *y )
{
    constexpr int W = laneWidth<VT>();
    for ( int o = 0; o < N; o += W )
    {
        VT v[4], s[4];
        for ( int j = 0; j < 4; ++j )
        {
            v[j] = loadLanes<VT>( fv[j] + o );
            s[j] = loadLanes<VT>( fs[j] + o );
        }
        const VT xs = loadLanes<VT>( x + o ), gg = loadLanes<VT>( g + o ), kk = loadLanes<VT>( k + o );
        VT       out = lanes<VT>( 0.0f );
        switch ( m )
        {
        case FilterModelE::MOOG: out = ladderMoog<VT>( v, s, xs, gg, kk ); break;
        case FilterModelE::PROPHET:
        case FilterModelE::JUNO:
            out = otaCascade<VT>( v, s, xs, gg, kk, FilterVoicing::otaDrive( m ), FilterVoicing::otaRes( m ) );
            break;
        case FilterModelE::XPANDER:
        {
            otaCascade<VT>( v, s, xs, gg, kk, FilterVoicing::otaDrive( m ), FilterVoicing::otaRes( m ) );
            const float         r = FilterVoicing::otaRes( m );
            const VT            a0 = xs - kk * ftanh<VT>( lanes<VT>( r ) * v[3] ) / lanes<VT>( r );
            alignas( 32 ) float w[5][W];   // the pole mix of every lane's mode
            for ( int l = 0; l < W; ++l )
            {
                const float *p = poleMix( mode[o + l] );
                for ( int j = 0; j < 5; ++j ) w[j][l] = p[j];
            }
            out = loadLanes<VT>( w[0] ) * a0 + loadLanes<VT>( w[1] ) * v[0] + loadLanes<VT>( w[2] ) * v[1] +
                  loadLanes<VT>( w[3] ) * v[2] + loadLanes<VT>( w[4] ) * v[3];
            break;
        }
        case FilterModelE::SEM:
        case FilterModelE::WASP:
            out = svfNonlinear<VT>( v, s, xs, gg, kk, FilterVoicing::svfRange( m ), FilterVoicing::svfAsym( m ),
                                    loadLanes<VT>( mode + o ) );
            break;
        case FilterModelE::POLIVOKS:
            out = svfNonlinear<VT>( v, s, xs, gg, kk, FilterVoicing::svfRange( m ), 0.0f, loadLanes<VT>( mode + o ),
                                    true );
            break;
        case FilterModelE::DIODE: out = diodeLadder<VT>( v, s, xs, gg * lanes<VT>( 0.70710678f ), kk ); break;
        case FilterModelE::KORG35: out = korg35<VT>( v, s, xs, gg, kk ); break;
        default: break;
        }
        vstore( y + o, out );
        for ( int j = 0; j < 4; ++j )
        {
            vstore( fv[j] + o, v[j] );
            vstore( fs[j] + o, s[j] );
        }
    }
}
}   // namespace

void PolySynth::clearLanes( int key )
{
    for ( int j = 0; j < 4; ++j )
        m_fv[j][2 * key] = m_fv[j][2 * key + 1] = m_fs[j][2 * key] = m_fs[j][2 * key + 1] = 0.0f;
}

void PolySynth::prepare( double sampleRate, uint64_t seed )
{
    m_sr = sampleRate;
    prepareWavetables();
    m_rng.seed( seed );
    for ( int i = 0; i < Keys; ++i )
    {
        m_keys[i] = Key{};
        m_keys[i].m_rng.seed( mixSeed( seed, 100 + static_cast<uint64_t>( i ) ) );
        for ( FilterLane &f : m_keys[i].m_filt ) f.allocate( 2048 );   // the comb's lines (24 Hz at 48 kHz)
        m_keys[i].m_mod.prepare( sampleRate, mixSeed( seed, 200 + static_cast<uint64_t>( i ) ) );
        m_keys[i].m_mod.set( m_s.m_mod );
    }
    m_times.setSampleRate( sampleRate );
    m_ftimes.setSampleRate( sampleRate );
    for ( int i = 0; i < Keys; ++i ) clearLanes( i );
    size_t len = 1;
    while ( len < static_cast<size_t>( 0.05 * sampleRate ) ) len <<= 1;
    m_line.assign( len, 0.0f );
    m_mask = len - 1;
    m_write = 0;
    m_order = 0;
    m_scanPhase = m_chorusPhase = 0.0;
    m_ring = 0.0f;
    m_count = 0;
    set( m_s );
}

void PolySynth::set( const PolySettings &s )
{
    // A new filter model starts from rest.
    if ( s.m_filter != m_s.m_filter )
    {
        for ( Key &k : m_keys )
            for ( FilterLane &f : k.m_filt ) f.clear();
        for ( int i = 0; i < Keys; ++i ) clearLanes( i );
    }
    const bool modMoved = std::memcmp( &s.m_mod, &m_s.m_mod, sizeof( ModSettings ) ) != 0;
    m_s = s;
    m_table = &wavetable( s.m_table );
    m_times.setTimes( std::max( 0.005f, s.m_attackS ), s.m_ampDecayS, std::clamp( s.m_ampSustain, 0.0f, 1.0f ),
                      std::max( 0.05f, s.m_releaseS ) );
    m_ftimes.setTimes( s.m_filtAttackS, s.m_filtDecayS, std::clamp( s.m_filtSustain, 0.0f, 1.0f ), s.m_filtReleaseS );
    for ( Key &k : m_keys )
    {
        k.m_env.copyTimes( m_times );
        k.m_fenv.copyTimes( m_ftimes );
        if ( modMoved ) k.m_mod.set( s.m_mod );
    }
}

void PolySynth::retune( Key &k )
{
    for ( int o = 0; o < 2; ++o )
    {
        double cents =
            ( o == 0 ? -0.5 : 0.5 ) * static_cast<double>( m_s.m_detuneCents ) + static_cast<double>( k.m_drift[o] );
        if ( k.m_mod.active() && k.m_mod.targets( ModDestE::PITCH ) )
            cents += 100.0 * static_cast<double>( k.m_mo[static_cast<int>( ModDestE::PITCH )] );
        const double hz = midiToHz( static_cast<double>( k.m_pitch ) + cents / 100.0 );
        k.m_inc[o] = hz / m_sr;
        k.m_level[o] = cycleLevelFor( hz, m_sr, k.m_level[o] );
    }
}

void PolySynth::noteOn( int pitch, float velocity, int id )
{
    Key *k = nullptr;
    for ( Key &c : m_keys )
        if ( !c.m_on )
        {
            k = &c;
            break;
        }
    if ( k == nullptr )
    {
        k = &m_keys[0];
        for ( Key &c : m_keys )
            if ( c.m_order < k->m_order ) k = &c;
    }
    const bool fresh = !k->m_on;
    k->m_on = true;
    k->m_held = true;
    k->m_pitch = pitch;
    k->m_id = id;
    k->m_velocity = velocity;
    k->m_order = ++m_order;
    if ( fresh )
    {
        // Free-running oscillators: a new key starts wherever, so a chord is never phase-locked.
        k->m_phase[0] = m_rng.uniform();
        k->m_phase[1] = m_rng.uniform();
        k->m_drift[0] = m_s.m_driftCents * m_rng.bipolar();
        k->m_drift[1] = m_s.m_driftCents * m_rng.bipolar();
        k->m_filt[0].clear();
        k->m_filt[1].clear();
        clearLanes( static_cast<int>( k - m_keys ) );
        k->m_level[0] = k->m_level[1] = -1;
    }
    k->m_scanOffset = m_rng.uniform();
    k->m_env.copyTimes( m_times );
    k->m_env.noteOn();
    k->m_fenv.copyTimes( m_ftimes );
    k->m_fenv.noteOn();
    k->m_mod.noteOn( m_count, beatAt( m_count ) );
    retune( *k );
}

void PolySynth::noteOff( int id )
{
    for ( Key &k : m_keys )
        if ( k.m_on && k.m_held && k.m_id == id )
        {
            k.m_held = false;
            k.m_env.noteOff();
            k.m_fenv.noteOff();
            k.m_mod.noteOff();
        }
}

void PolySynth::silence()
{
    for ( Key &k : m_keys )
    {
        k.m_on = false;
        k.m_held = false;
        k.m_env.kill();
        k.m_fenv.kill();
        k.m_mod.kill();
    }
    std::fill( m_line.begin(), m_line.end(), 0.0f );
    m_ring = 0.0f;
}

bool PolySynth::active() const
{
    for ( const Key &k : m_keys )
        if ( k.m_on ) return true;
    return m_ring > 1e-5f;
}

void PolySynth::process( float *L, float *R, int n )
{
    const double       scanStep = static_cast<double>( m_s.m_scanHz ) * ControlStep / m_sr;
    const double       chorusInc = 0.45 / m_sr;
    const float        spread = std::clamp( m_s.m_spread, 0.0f, 1.0f ), cross = 1.0f - spread;
    const float        chorus = std::clamp( m_s.m_chorus, 0.0f, 1.0f );
    const float        base = static_cast<float>( 0.007 * m_sr ), depth = static_cast<float>( 0.0025 * m_sr ) * chorus;
    const float        wet = 0.7f * chorus, norm = 1.0f / ( 1.0f + 0.5f * chorus );
    const float        keyGain = 0.3f / ( 1.0f + cross );
    const bool         hasTable = m_table != nullptr && !m_table->empty();
    const FilterModelE model = static_cast<FilterModelE>( std::clamp( m_s.m_filter, 0, FilterModels - 1 ) );
    const float        fk = FilterVoicing::feedback( model, std::clamp( m_s.m_resonance, 0.0f, 1.0f ) );
    for ( int i = 0; i < n; ++i )
    {
        const int64_t t = m_count + i;
        const bool    control = t % ControlStep == 0, drift = t % Drift == 0;
        if ( control )
        {
            m_scanPhase += scanStep;
            if ( m_scanPhase >= 1.0 ) m_scanPhase -= 1.0;
        }
        float l = 0.0f, r = 0.0f;
        // The filters' inputs and knobs per lane (runLanes after the keys): a key that sounds sets its two.
        alignas( 32 ) float xin[Lanes], gl[Lanes], kl[Lanes], ml[Lanes], yl[Lanes];
        bool                live[Keys] = {}, moved[Keys] = {};
        for ( int ki = 0; ki < Keys; ++ki )
        {
            xin[2 * ki] = xin[2 * ki + 1] = 0.0f;
            gl[2 * ki] = gl[2 * ki + 1] = m_keys[ki].m_g;
            kl[2 * ki] = kl[2 * ki + 1] = fk;
            ml[2 * ki] = ml[2 * ki + 1] = m_s.m_filterMode;
        }
        for ( int ki = 0; ki < Keys; ++ki )
        {
            Key &k = m_keys[ki];
            if ( !k.m_on ) continue;
            if ( drift && m_s.m_driftCents > 0.0f )
            {
                for ( float &d : k.m_drift ) d += 0.3f * ( m_s.m_driftCents * k.m_rng.bipolar() - d );
                retune( k );
            }
            const bool linked = m_s.m_filtLink != 0;
            const bool mod = k.m_mod.active();
            if ( control )
            {
                const float fe = linked ? k.m_env.level() : k.m_fenv.level();
                if ( mod )
                {
                    // The key's matrix at this control step (Modulation.h).
                    float ext[ModSources] = {};
                    ext[static_cast<int>( ModSourceE::FILTER_ENV )] = fe;
                    ext[static_cast<int>( ModSourceE::VELOCITY )] = k.m_velocity;
                    k.m_mod.evaluate( t, beatAt( t ), ext, k.m_mo );
                    if ( k.m_mod.targets( ModDestE::PITCH ) ) retune( k );
                    k.m_fk = FilterVoicing::feedback(
                        model,
                        std::clamp( m_s.m_resonance + k.m_mod.offset( k.m_mo, ModDestE::RESONANCE ), 0.0f, 1.0f ) );
                    k.m_mode =
                        std::clamp( m_s.m_filterMode + k.m_mod.offset( k.m_mo, ModDestE::FILTER_MODE ), 0.0f, 1.0f );
                    k.m_lv = std::clamp( 1.0f + k.m_mod.offset( k.m_mo, ModDestE::LEVEL ), 0.0f, 2.0f );
                    const float p = std::clamp( k.m_mod.offset( k.m_mo, ModDestE::PAN ), -1.0f, 1.0f );
                    k.m_panL = std::min( 1.0f, 1.0f - p );
                    k.m_panR = std::min( 1.0f, 1.0f + p );
                }
                const float scan =
                    0.5f * m_s.m_scan *
                    static_cast<float>( std::sin( 6.283185307179586 * ( m_scanPhase + k.m_scanOffset ) ) );
                float pos = m_s.m_position + scan;
                if ( mod && k.m_mod.targets( ModDestE::TABLE_POS ) )
                    pos += k.m_mo[static_cast<int>( ModDestE::TABLE_POS )];
                k.m_position = std::clamp( pos, 0.0f, 1.0f );
                float envOct = m_s.m_envOctaves * fe;
                if ( m_s.m_envVelocity > 0.0f ) envOct *= 1.0f - m_s.m_envVelocity + m_s.m_envVelocity * k.m_velocity;
                float octs = 0.5f * static_cast<float>( k.m_pitch - 60 ) / 12.0f + envOct;
                if ( mod && k.m_mod.targets( ModDestE::CUTOFF ) ) octs += k.m_mo[static_cast<int>( ModDestE::CUTOFF )];
                const float fc = std::min( m_s.m_cutoffHz * std::exp2( octs ), static_cast<float>( 0.45 * m_sr ) );
                k.m_g = std::tan( 3.14159265f * fc / static_cast<float>( m_sr ) );
            }
            const float env = k.m_env.process();
            if ( !linked ) k.m_fenv.process();
            k.m_mod.tick();
            if ( !k.m_env.isActive() )
            {
                k.m_on = false;
                continue;
            }
            const float a = hasTable ? m_table->at( k.m_level[0], k.m_position, k.m_phase[0] ) : 0.0f;
            const float b = hasTable ? m_table->at( k.m_level[1], k.m_position, k.m_phase[1] ) : 0.0f;
            for ( int o = 0; o < 2; ++o )
            {
                k.m_phase[o] += k.m_inc[o];
                if ( k.m_phase[o] >= 1.0 ) k.m_phase[o] -= 1.0;
            }
            float amp = env * k.m_velocity * keyGain;
            live[ki] = true;
            moved[ki] = mod;
            gl[2 * ki] = gl[2 * ki + 1] = k.m_g;
            if ( mod )
            {
                // What the matrix moves: resonance, mode and level per key, and the key's place between the sides.
                amp *= k.m_lv;
                kl[2 * ki] = kl[2 * ki + 1] = k.m_mod.targets( ModDestE::RESONANCE ) ? k.m_fk : fk;
                ml[2 * ki] = ml[2 * ki + 1] = k.m_mod.targets( ModDestE::FILTER_MODE ) ? k.m_mode : m_s.m_filterMode;
            }
            xin[2 * ki] = ( a + cross * b ) * amp;
            xin[2 * ki + 1] = ( cross * a + b ) * amp;
        }
        // The filters: every key's two in lanes, the comb key by key (its lines); then the keys summed in their order.
        if ( model == FilterModelE::COMB )
        {
            for ( int ki = 0; ki < Keys; ++ki )
            {
                if ( !live[ki] ) continue;
                yl[2 * ki] = m_keys[ki].m_filt[0].tick( model, xin[2 * ki], gl[2 * ki], kl[2 * ki], ml[2 * ki] );
                yl[2 * ki + 1] =
                    m_keys[ki].m_filt[1].tick( model, xin[2 * ki + 1], gl[2 * ki + 1], kl[2 * ki + 1], ml[2 * ki + 1] );
            }
        }
        else
        {
            runLanes<VecF, Lanes>( model, m_fv, m_fs, xin, gl, kl, ml, yl );
            for ( int c = 0; c < Lanes; ++c ) yl[c] *= FilterVoicing::makeup( model, kl[c] );
        }
        for ( int ki = 0; ki < Keys; ++ki )
        {
            if ( !live[ki] ) continue;
            if ( !moved[ki] )
            {
                l += yl[2 * ki];
                r += yl[2 * ki + 1];
            }
            else
            {
                l += m_keys[ki].m_panL * yl[2 * ki];
                r += m_keys[ki].m_panR * yl[2 * ki + 1];
            }
        }
        // The ensemble: one line, read on either side with the sweep turned against itself.
        m_line[m_write] = 0.5f * ( l + r );
        const float sw = depth * static_cast<float>( std::sin( 6.283185307179586 * m_chorusPhase ) );
        m_chorusPhase += chorusInc;
        if ( m_chorusPhase >= 1.0 ) m_chorusPhase -= 1.0;
        auto tap = [&]( float d )
        {
            const float  pos = static_cast<float>( m_write + m_mask + 1 ) - d;   // one line length on: never negative
            const size_t i0 = static_cast<size_t>( pos );
            const float  frac = pos - static_cast<float>( i0 );
            return m_line[i0 & m_mask] + frac * ( m_line[( i0 + 1 ) & m_mask] - m_line[i0 & m_mask] );
        };
        const float outL = ( l + wet * tap( base + sw ) ) * norm, outR = ( r + wet * tap( base - sw ) ) * norm;
        m_write = ( m_write + 1 ) & m_mask;
        L[i] = outL;
        R[i] = outR;
        m_ring = std::max( m_ring * 0.9999f, std::max( std::fabs( outL ), std::fabs( outR ) ) );
    }
    m_count += n;
}

}   // namespace eph
