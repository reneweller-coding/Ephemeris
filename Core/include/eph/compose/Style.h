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
 */
#pragma once
#include "eph/compose/GestureEngine.h"
#include "eph/Params.h"
#include "eph/synth/TapeKeys.h"
#include <vector>

namespace eph {

/** @brief One style profile. */
struct StyleProfile {
    const char* name;                    ///< the profile's name, as in compose.style
    float bpmLow;                        ///< lowest tempo
    float bpmHigh;                       ///< highest tempo
    float minutesLow;                    ///< shortest piece in a concert
    float minutesHigh;                   ///< longest piece in a concert
    int phasesLow;                       ///< fewest sequence phases per piece
    int phasesHigh;                      ///< most sequence phases per piece
    float introShare;                    ///< share of the piece in the atmosphere before the first phase
    float codaShare;                     ///< share of the piece in the coda after the last phase
    float newTempoChance;                ///< a later phase in a tempo of its own
    float newKeyChance;                  ///< a later phase in a key of its own
    int peakRows;                        ///< note rows sounding at the peak (bass included)
    std::vector<int> counterLengths;     ///< lengths the counter rows are drawn from (in their division)
    float mutation;                      ///< mutation chance per cycle of the note rows
    RowDivision transposerDivision;      ///< how slowly the roots move
    int transposerLength;                ///< steps of the transposer row
    float tapeChance;                    ///< chance that a piece has the tape keys
    float stringsChance;                 ///< chance of the string machine
    float leadChance;                    ///< chance of a lead section in a phase
    float bleepChance;                   ///< chance of the atmosphere's bleeps
    float drumsChance;                   ///< chance of drums
    TapeSet tape;                        ///< the tape set of the tape keys
    float leadIntensity;                 ///< density of the lead's phrases
    HandStyle hands;                     ///< timing of the hands
    float darkness;                      ///< offset of the voices' cutoffs at rest (negative: darker)
    float hallSeconds;                   ///< decay of the hall
    float levelDb;                       ///< master offset towards the profile's measured loudness
    float grainChance = 0.0f;            ///< chance of the atmosphere's granular cloud in a piece (25.09.2026)
};

/** @brief The profile of a style. */
const StyleProfile& styleProfile(Style style);

/**
 * @brief A profile between @p a and @p b (compose.morph_to, a concert that wanders from one style to another):
 *        at @p t = 0 exactly @p a, at 1 exactly @p b. The numbers in between are interpolated (counts rounded);
 *        what cannot be halfway -- the counter rows' lengths, the transposer, the tape set, the name -- is the
 *        nearer profile's.
 */
StyleProfile morphProfile(const StyleProfile& a, const StyleProfile& b, float t);

/** @brief Whether the user's own style is on (custom.use). */
bool customStyleOn(const ParamStore& p);
/**
 * @brief The user's own style (module custom): compose.style's profile (@p base) with the numbers of the custom
 *        module; the counter rows' lengths, the transposer, the tape set and the name stay @p base's.
 */
StyleProfile customProfile(const ParamStore& p, const StyleProfile& base);
/** @brief Writes @p s's numbers into the custom module (the Style tab's "copy"). */
void copyToCustom(const StyleProfile& s, ParamStore& p);

/** @brief Where a concert's arc of tension stands at @p t (0..1 of the concert): 0 at the ends, 1 at 60 %. */
float concertArc(float t);

/**
 * @brief A profile moved along a concert's arc of tension (compose.concert_arc): @p tension from -0.5 (the
 *        quiet ends) to +0.5 (the height), times @p strength (0..1). Towards the height: more rows at the peak,
 *        the layers and the lead more likely and denser, faster hands, a brighter sound, a few per cent more
 *        tempo; towards the ends the other way. At tension or strength 0 the profile itself.
 */
StyleProfile arcProfile(const StyleProfile& p, float tension, float strength);

} // namespace eph
