/**
 * @file Style.h
 * @brief The five style profiles (PLAN 2.8) as the numbers the composer draws from.
 *
 * A profile is a vector of ranges and chances: tempo, length, how many sequence phases a piece has and
 * how long its intro and coda are; how many rows play at the peak, of which lengths, how much they
 * mutate; how slowly the transposer moves; which layers a piece is likely to have; how the hands move;
 * how dark the voices sit; how long the hall is; how loud the piece ends up.
 *
 * **Calibration** (PLAN, Stand 24.09.2026, first reference measurement): the level offsets follow the
 * measured RMS of the references (Cosmic, Melodic and Modern around -14 to -16 dBFS, Doom and Drift
 * around -20 with far more dynamics), and the tempo ranges were lowered after the measurement hinted
 * that PLAN 2.8's first ranges were high. Lengths and layer chances are still from listening and the
 * literature; the drums are drawn but not yet played (they come with the kit, later in Phase 4).
 * @author Rene Weller
 */
#pragma once
#include "eph/compose/GestureEngine.h"
#include "eph/Params.h"
#include "eph/synth/TapeKeys.h"
#include <vector>

namespace eph
{

/** @brief One style profile. */
struct StyleProfile
{
    const char      *m_name;                 ///< the profile's name, as in compose.style
    float            m_bpmLow;               ///< lowest tempo
    float            m_bpmHigh;              ///< highest tempo
    float            m_minutesLow;           ///< shortest piece in a concert
    float            m_minutesHigh;          ///< longest piece in a concert
    int              m_phasesLow;            ///< fewest sequence phases per piece
    int              m_phasesHigh;           ///< most sequence phases per piece
    float            m_introShare;           ///< share of the piece in the atmosphere before the first phase
    float            m_codaShare;            ///< share of the piece in the coda after the last phase
    float            m_newTempoChance;       ///< a later phase in a tempo of its own
    float            m_newKeyChance;         ///< a later phase in a key of its own
    int              m_peakRows;             ///< note rows sounding at the peak (bass included)
    std::vector<int> m_counterLengths;       ///< lengths the counter rows are drawn from (in their division)
    float            m_mutation;             ///< mutation chance per cycle of the note rows
    RowDivisionE     m_transposerDivision;   ///< how slowly the roots move
    int              m_transposerLength;     ///< steps of the transposer row
    float            m_tapeChance;           ///< chance that a piece has the tape keys
    float            m_stringsChance;        ///< chance of the string machine
    float            m_leadChance;           ///< chance of a lead section in a phase
    float            m_bleepChance;          ///< chance of the atmosphere's bleeps
    float            m_drumsChance;          ///< chance of drums
    TapeSetE         m_tape;                 ///< the tape set of the tape keys
    float            m_leadIntensity;        ///< density of the lead's phrases
    HandStyle        m_hands;                ///< timing of the hands
    float            m_darkness;             ///< offset of the voices' cutoffs at rest (negative: darker)
    float            m_hallSeconds;          ///< decay of the hall
    float            m_levelDb;              ///< master offset towards the profile's measured loudness
    float            m_grainChance = 0.0f;   ///< chance of the atmosphere's granular cloud in a piece (25.09.2026)
    float            m_peakLufs = -12.0f;    ///< what a piece's loudest part should measure (26.09.2026, Leveler.h)
    // The night sets (25.09.2026, composeNightSet): a piece that overlaps its neighbours as a DJ mixes two tracks.
    int  m_introBars = 0;      ///< the atmosphere before the first phase in bars (0: introShare)
    int  m_entryBars = 0;      ///< the first phase's entry (the bass alone) in bars (0: its share)
    int  m_codaBars = 0;       ///< the coda in bars (0: codaShare)
    bool m_djOutro = false;    ///< the coda an outro to mix out of: the rows play on into it (below)
    int  m_bassHandover = 0;   ///< bars into the outro where the next piece's bass comes in: the counter rows
                               ///< stop there, the bass gives up its lows and recedes (0: halfway)
    int m_mainHandover = 0;    ///< bars into the outro where the next piece's main sequence comes in: this
                               ///< one's recedes from there to the end (0: at the bass's handover)
};

/**
 * @brief The profile of a style.
 * @param[in] style  the style
 * @return its profile
 */
const StyleProfile &styleProfile( StyleE style );

/**
 * @brief A profile between @p a and @p b (compose.morph_to, a concert that wanders from one style to another):
 *        at @p t = 0 exactly @p a, at 1 exactly @p b. The numbers in between are interpolated (counts rounded);
 *        what cannot be halfway -- the counter rows' lengths, the transposer, the tape set, the name -- is the
 *        nearer profile's.
 * @param[in] a  the profile at 0
 * @param[in] b  the profile at 1
 * @param[in] t  the place between them, 0..1
 * @return the profile between
 */
StyleProfile morphProfile( const StyleProfile &a, const StyleProfile &b, float t );

/**
 * @brief Whether the user's own style is on (custom.use).
 * @param[in] p  the parameters
 * @return true when the user's own style is on
 */
bool customStyleOn( const ParamStore &p );
/**
 * @brief The user's own style (module custom): compose.style's profile (@p base) with the numbers of the custom
 *        module; the counter rows' lengths, the transposer, the tape set and the name stay @p base's.
 * @param[in] p     the parameters
 * @param[in] base  compose.style's profile
 * @return the user's own profile
 */
StyleProfile customProfile( const ParamStore &p, const StyleProfile &base );
/**
 * @brief Writes @p s's numbers into the custom module (the Style tab's "copy").
 * @param[in]     s  the profile
 * @param[in,out] p  the parameters written to
 */
void copyToCustom( const StyleProfile &s, ParamStore &p );

/**
 * @brief Where a concert's arc of tension stands at @p t (0..1 of the concert): 0 at the ends, 1 at 60 %.
 * @param[in] t  the place in the concert, 0..1
 * @return the tension, 0..1
 */
float concertArc( float t );

/**
 * @brief A profile moved along a concert's arc of tension (compose.concert_arc): @p tension from -0.5 (the
 *        quiet ends) to +0.5 (the height), times @p strength (0..1). Towards the height: more rows at the peak,
 *        the layers and the lead more likely and denser, faster hands, a brighter sound, a few per cent more
 *        tempo; towards the ends the other way. At tension or strength 0 the profile itself.
 * @param[in] p         the profile
 * @param[in] tension   the tension, -0.5..0.5
 * @param[in] strength  how strongly, 0..1
 * @return the profile moved
 */
StyleProfile arcProfile( const StyleProfile &p, float tension, float strength );

}   // namespace eph
