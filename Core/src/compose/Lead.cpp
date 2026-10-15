/**
 * @file Lead.cpp
 * @brief Lead phrases by rule.
 * @author Rene Weller
 */
#include "eph/compose/Lead.h"
#include "eph/Rack.h"
#include <algorithm>
#include <cmath>

namespace eph
{

namespace
{

/**
 * @brief Pitch classes (relative to the root) of the scale, and of the pentatonic within it.
 * @param[in]  scale       the scale
 * @param[out] inScaleSet  twelve flags, true for the scale's pitch classes
 * @param[out] pentatonic  twelve flags, true for the pentatonic's
 */
void pitchSets( int scale, bool *inScaleSet, bool *pentatonic )
{
    for ( int i = 0; i < 12; ++i ) inScaleSet[i] = pentatonic[i] = false;
    const int n = scaleSize( scale );
    for ( int d = 0; d < n; ++d ) inScaleSet[scaleSemitones( scale, d ) % 12] = true;
    for ( int pc : { 0, 3, 5, 7, 10 } ) pentatonic[pc] = inScaleSet[pc] || pc == 0;
    // Where the scale has no minor third or seventh, the pentatonic keeps what it has; where it has the major
    // sixth (Dorian), the sixth joins it as a colour (the style guide's 3.8).
    if ( inScaleSet[9] ) pentatonic[9] = true;
}

/**
 * @brief The allowed pitches in the register, under a root.
 * @param[in] low     the register's lowest note
 * @param[in] high    its highest note
 * @param[in] rootPc  the root's pitch class
 * @param[in] set     twelve flags of the allowed pitch classes above the root
 * @return the allowed notes, rising
 */
std::vector<int> allowed( int low, int high, int rootPc, const bool *set )
{
    std::vector<int> out;
    for ( int p = low; p <= high; ++p )
        if ( set[pitchClass( p - rootPc )] ) out.push_back( p );
    return out;
}

/**
 * @brief The index of the pitch in @p v nearest @p pitch.
 * @param[in] v      the pitches
 * @param[in] pitch  the pitch
 * @return the index of the nearest
 */
int nearestIndex( const std::vector<int> &v, int pitch )
{
    int best = 0;
    for ( int i = 1; i < static_cast<int>( v.size() ); ++i )
        if ( std::abs( v[static_cast<size_t>( i )] - pitch ) < std::abs( v[static_cast<size_t>( best )] - pitch ) )
            best = i;
    return best;
}

/**
 * @brief A phrase's rhythm over @p span beats: lengths in beats, a negative entry a rest; the last note is held to the
 *        end.
 * @param[in]     span       the phrase's length in beats
 * @param[in]     intensity  how dense, 0..1
 * @param[in,out] rng        the random stream
 * @return the lengths in beats, negative for rests
 */
std::vector<double> drawRhythm( double span, float intensity, Rng &rng )
{
    std::vector<double> rhythm;
    double              pos = rng.uniform() < 0.3f ? 0.5 : 0.0;   // sometimes in on the "and"
    if ( pos > 0.0 ) rhythm.push_back( -pos );
    while ( pos < span - 2.0 )
    {
        const float u = rng.uniform();
        double      len;
        if ( u < 0.25f - 0.15f * intensity )
            len = 2.0;
        else if ( u < 0.5f - 0.1f * intensity )
            len = 1.5;
        else if ( u < 0.8f )
            len = 1.0;
        else
            len = 0.5;
        if ( len == 0.5 )
        {
            rhythm.push_back( 0.5 );
            rhythm.push_back( 0.5 );
            pos += 1.0;
            continue;
        }
        rhythm.push_back( len );
        pos += len;
    }
    rhythm.push_back( std::max( 1.0, span - pos ) );
    return rhythm;
}

/**
 * @brief A step on the pentatonic: mostly up in the first half of a phrase, mostly down in the second, the odd leap.
 * @param[in]     rising  true in the first half of a phrase
 * @param[in,out] rng     the random stream
 * @return the step in degrees of the pentatonic
 */
int drawStep( bool rising, Rng &rng )
{
    const float u = rng.uniform();
    int         step = u < 0.15f ? 0 : ( u < 0.75f ? 1 : ( u < 0.92f ? 2 : 3 ) );
    if ( !rising ) step = -step;
    if ( rng.uniform() < 0.2f ) step = -step;
    return step;
}

/**
 * @brief The nearest chord tone within four semitones, inside the register (@p pitch itself if there is none).
 * @param[in] pitch   the note
 * @param[in] rootPc  the root's pitch class
 * @param[in] chord   twelve flags of the chord's pitch classes above the root
 * @param[in] plan    the lead's plan (its register)
 * @return the chord tone, or the note itself
 */
int toChordTone( int pitch, int rootPc, const bool *chord, const LeadPlan &plan )
{
    for ( int d = 1; d < 5; ++d )
    {
        if ( pitch + d <= plan.m_high && chord[pitchClass( pitch + d - rootPc )] ) return pitch + d;
        if ( pitch - d >= plan.m_low && chord[pitchClass( pitch - d - rootPc )] ) return pitch - d;
    }
    return pitch;
}

/**
 * @brief One lead note; where @p glideSpot allows (a phrase's start, a long note) sometimes with a glide
 *        from the scale tone below (a whole tone, or a half where the scale has no whole).
 * @param[in,out] score      the score written to
 * @param[in]     plan       the lead's plan
 * @param[in]     sc         twelve flags of the scale's pitch classes above the root
 * @param[in]     rootPc     the root's pitch class
 * @param[in]     pitch      the note
 * @param[in]     beat       its start in beats
 * @param[in]     len        the length of its slot in beats
 * @param[in]     noteLen    the note's own length in beats
 * @param[in]     glideSpot  true where a glide may come
 * @param[in,out] rng        the random stream
 */
void writeNote( Score &score, const LeadPlan &plan, const bool *sc, int rootPc, int pitch, double beat, double len,
                double noteLen, bool glideSpot, Rng &rng )
{
    // A little behind the beat, as a player over a machine (the style guide's 5.4): 8 to 23 ms at 120 BPM.
    const double lag = 0.015 + 0.03 * static_cast<double>( rng.uniform() );
    beat += lag;
    noteLen -= lag;
    const int grace =
        sc[pitchClass( pitch - 2 - rootPc )] ? pitch - 2 : ( sc[pitchClass( pitch - 1 - rootPc )] ? pitch - 1 : -1 );
    NoteEvent e;
    e.m_part = PartE::LEAD;
    e.m_pitch = pitch;
    if ( glideSpot && rng.uniform() < 0.3f && grace >= plan.m_low && len >= 1.0 )
    {
        NoteEvent g;
        g.m_beat = beat;
        g.m_length = 0.3;   // overlaps the target, so the voice glides
        g.m_part = PartE::LEAD;
        g.m_pitch = grace;
        g.m_velocity = 0.75f;
        g.m_slide = true;
        score.m_notes.push_back( g );
        e.m_beat = beat + 0.25;
        e.m_length = std::max( 0.2, noteLen - 0.25 - 0.05 );
        e.m_velocity = 0.8f;
    }
    else
    {
        e.m_beat = beat;
        e.m_length = std::max( 0.1, noteLen * ( len <= 0.5 ? 0.85 : 0.95 ) );
        e.m_velocity = 0.72f + 0.12f * rng.uniform();
    }
    score.m_notes.push_back( e );
}

}   // namespace

bool inScale( int pitch, int rootPc, int scale )
{
    bool sc[12], pent[12];
    pitchSets( scale, sc, pent );
    return sc[pitchClass( pitch - rootPc )];
}

void writeLead( Score &score, const LeadPlan &plan, double from, double to, Rng &rng )
{
    bool sc[12], pent[12], chord[12], ending[12];
    int  setsFor = -1;
    // The pitch sets of the scale at a beat (a parallel change of mode moves them).
    auto setsAt = [&]( double beat )
    {
        const int scale = plan.m_scales.empty() ? plan.m_scale : rootShiftAt( plan.m_scales, beat );
        if ( scale == setsFor ) return;
        setsFor = scale;
        pitchSets( scale, sc, pent );
        for ( bool &c : chord ) c = false;
        chord[0] = chord[7] = true;
        chord[sc[3] ? 3 : 4] = true;
        for ( bool &c : ending ) c = false;
        ending[sc[7] ? 7 : 6] = true;
        ending[sc[3] ? 3 : 4] = true;
    };
    setsAt( from );
    const int centre = ( plan.m_low + plan.m_high ) / 2;

    double              t = std::ceil( from / BeatsPerBar - 1e-9 ) * BeatsPerBar;
    int                 cur = centre;
    std::vector<double> lastRhythm;
    std::vector<int>    lastSteps;
    int                 lastBars = 0;
    while ( t < to - BeatsPerBar )
    {
        // A repeated phrase keeps the length of the one it repeats, so its rhythm fits.
        const bool repeat = !lastRhythm.empty() && rng.uniform() < 0.4f;
        // Phrases of four to eight bars (the style guide's 3.8).
        const int    bars = repeat ? lastBars : ( rng.uniform() < 0.6f ? 4 : 8 );
        const double end = std::min( t + bars * BeatsPerBar, to );
        setsAt( t );
        const int              rootPc = pitchClass( plan.m_keyRoot + rootShiftAt( plan.m_shifts, t ) );
        const std::vector<int> pentSet = allowed( plan.m_low, plan.m_high, rootPc, pent );
        const std::vector<int> fullSet = allowed( plan.m_low, plan.m_high, rootPc, sc );
        if ( pentSet.size() < 3 ) break;
        const std::vector<double> rhythm = repeat ? lastRhythm : drawRhythm( end - t, plan.m_intensity, rng );

        // Pitches: an arc over the pentatonic, steps with the odd leap; chord tones on the strong beats.
        int              idx = nearestIndex( pentSet, repeat ? cur : centre + static_cast<int>( rng.below( 7 ) ) - 3 );
        std::vector<int> steps;
        double           pos = 0.0;
        const int        notes = static_cast<int>( rhythm.size() );
        for ( int k = 0; k < notes; ++k )
        {
            const double len = rhythm[static_cast<size_t>( k )];
            if ( len < 0.0 )
            {
                pos += -len;
                steps.push_back( 0 );
                continue;
            }
            const int step = repeat && k < static_cast<int>( lastSteps.size() )
                                 ? lastSteps[static_cast<size_t>( k )]
                                 : drawStep( pos < 0.5 * ( end - t ), rng );
            steps.push_back( step );
            idx = std::clamp( idx + step, 0, static_cast<int>( pentSet.size() ) - 1 );
            int          pitch = pentSet[static_cast<size_t>( idx )];
            const double beat = t + pos;
            const bool   strong = std::fmod( beat, 2.0 ) < 1e-6;
            if ( k == notes - 1 )
            {
                // The phrase ends on the fifth or the minor third, not on the root (3.8).
                if ( !ending[pitchClass( pitch - rootPc )] ) pitch = toChordTone( pitch, rootPc, ending, plan );
            }
            else if ( strong && !chord[pitchClass( pitch - rootPc )] )
                pitch = toChordTone( pitch, rootPc, chord, plan );
            else if ( !strong && len <= 0.5 && rng.uniform() < 0.25f )
            {
                // A passing tone of the full scale, off the beat.
                pitch = fullSet[static_cast<size_t>( nearestIndex( fullSet, pitch + ( step >= 0 ? 1 : -1 ) ) )];
            }
            // Under a root or a mode that changes inside the phrase, the note is snapped into the new one.
            setsAt( beat );
            const int rootNow = pitchClass( plan.m_keyRoot + rootShiftAt( plan.m_shifts, beat ) );
            if ( !sc[pitchClass( pitch - rootNow )] )
            {
                const std::vector<int> now = allowed( plan.m_low, plan.m_high, rootNow, sc );
                if ( !now.empty() ) pitch = now[static_cast<size_t>( nearestIndex( now, pitch ) )];
            }
            const double noteLen = std::min( len, to - beat );
            if ( noteLen <= 0.05 ) break;
            writeNote( score, plan, sc, rootNow, pitch, beat, len, noteLen, k == 0 || len >= 2.0, rng );
            cur = pitch;
            pos += len;
        }
        lastRhythm = rhythm;
        lastSteps = steps;
        lastBars = bars;
        // A rest of two to four bars, shorter with more intensity (3.8).
        const int restBars = 2 + static_cast<int>( rng.below( plan.m_intensity > 0.6f ? 2 : 3 ) );
        t = end + restBars * BeatsPerBar;
    }
}

}   // namespace eph
