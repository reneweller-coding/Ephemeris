/**
 * @file Leveler.cpp
 * @brief Every piece as loud as its style means (Leveler.h).
 * @author Rene Weller
 */
#include "eph/Leveler.h"
#include "eph/Engine.h"
#include "eph/Loudness.h"
#include <algorithm>
#include <memory>
#include <optional>

namespace eph
{

namespace
{
constexpr double Rate = 48000.0;   ///< the rate the parts are rendered at to be measured, Hz
constexpr int    Block = 512;      ///< the block they are rendered in
constexpr double Warm = 4.0;       ///< seconds before the part: the rooms fill, the notes sounding on are found again
constexpr float  MostDb = 4.0f;    ///< the largest correction either way

/**
 * @brief The loudness of @p seconds from @p beat of the score @p e plays (after Warm seconds before it); nothing if
 *         @p stop said so on the way.
 * @param[in,out] e        the engine that plays the score
 * @param[in]     s        the score
 * @param[in]     beat     where the stretch begins
 * @param[in]     seconds  how long it lasts
 * @param[in]     stop     asked now and then; true stops the measurement
 * @return the loudness in LUFS, empty when stopped
 */
std::optional<float> measure( Engine &e, const Score &s, double beat, double seconds,
                              const std::function<bool()> &stop )
{
    const double at = s.m_tempo.secondsAt( beat );
    e.seek( s.m_tempo.beatAt( std::max( 0.0, at - Warm ) ) );
    std::vector<float> l( Block ), r( Block );
    const double       warm = std::min( Warm, at );
    for ( int done = 0; done < static_cast<int>( warm * Rate ); done += Block )
    {
        if ( stop && stop() ) return std::nullopt;
        e.process( l.data(), r.data(), Block );
    }
    LoudnessMeter meter;
    meter.prepare( Rate );
    for ( int done = 0; done < static_cast<int>( seconds * Rate ); done += Block )
    {
        if ( stop && stop() ) return std::nullopt;
        e.process( l.data(), r.data(), Block );
        meter.process( l.data(), r.data(), Block );
    }
    return static_cast<float>( meter.report().m_integrated );
}
}   // namespace

std::vector<LevelReading> levelScore( Score &score, const ParamStore &params, double seconds,
                                      const std::function<bool()> &stop )
{
    std::vector<LevelReading> out;
    if ( score.m_levels.empty() ) return out;
    auto        e = std::make_unique<Engine>();
    ParamStore &p = e->params();
    p.copyValuesFrom( params );
    // The player's master level stays out of it: the correction is the piece's, the master level comes on top.
    const int master = p.id( ModuleE::MASTER, 0, master::LEVEL );
    p.set( master, p.defaultValue( master ) );
    e->prepare( Rate, Block );
    // First as composed, then with the corrections found (the master bus compresses and limits after the gain).
    Score s = score;
    for ( LevelMark &m : s.m_levels ) m.m_trimDb = 0.0f;
    e->load( s, true );
    for ( const LevelMark &m : s.m_levels )
    {
        const std::optional<float> got = measure( *e, s, m.m_peakBeat, seconds, stop );
        if ( !got ) return {};
        const float measured = *got;
        const float trim = measured > -70.0f ? std::clamp( m.m_targetLufs - measured, -MostDb, MostDb ) : 0.0f;
        out.push_back( { m.m_beat, measured, m.m_targetLufs, trim, 0.0f } );
    }
    for ( size_t i = 0; i < s.m_levels.size(); ++i ) s.m_levels[i].m_trimDb = out[i].m_trim;
    e->params().copyValuesFrom( params );
    p.set( master, p.defaultValue( master ) );
    e->load( s, true );
    for ( size_t i = 0; i < s.m_levels.size(); ++i )
    {
        const std::optional<float> got = measure( *e, s, s.m_levels[i].m_peakBeat, seconds, stop );
        if ( !got ) return {};
        const float again = *got;
        if ( again > -70.0f )
            out[i].m_trim = std::clamp( out[i].m_trim + ( out[i].m_target - again ), -MostDb, MostDb );
        out[i].m_after = again;
    }
    for ( size_t i = 0; i < score.m_levels.size() && i < out.size(); ++i ) score.m_levels[i].m_trimDb = out[i].m_trim;
    return out;
}

}   // namespace eph
