/**
 * @file SetFile.h
 * @brief Curation and the `.ephset` file (PLAN 6.10, 7): a piece or concert as seed, settings and rerolls.
 *
 * The composer writes every part of a piece on its own stream (Composer.h): the form, the tempo, the
 * rows' settings, the rack's patterns, the layers, the lead, the chords, the hands. **Rerolling** one of
 * them draws it again and leaves every other bit for bit as it was -- the user curates instead of
 * programming. A reroll is a counter per unit that moves the unit's stream:
 * `form`, `tempo`, `rows`, `rack`, `layers`, `lead`, `pads`, `hands`, and in a concert the same prefixed
 * with the piece (`piece2.lead`) plus `concert` for the order of lengths and keys. What is not rerolled
 * is locked.
 *
 * The `.ephset` is text in the style of Phosphene's `.phosset`:
 * @code
 *   # Ephemeris set
 *   seed=11
 *   minutes=14
 *   concert=0
 *   reroll piece2.lead=1
 *   param compose.style=Cosmic
 * @endcode
 * Only the parameters that differ from their defaults are written.
 */
#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace eph {

class ParamStore;

/** @brief The rerolls of a piece or concert: unit name -> how often it was drawn again. */
struct Curation {
    std::map<std::string, int> rerolls;   ///< unit name -> reroll count
    /** @brief The counter of @p unit (0 if never rerolled). */
    int count(const std::string& unit) const { const auto it = rerolls.find(unit); return it == rerolls.end() ? 0 : it->second; }
    /** @brief Draws @p unit once more. */
    void reroll(const std::string& unit) { ++rerolls[unit]; }
};

/** @brief Everything a set file holds. */
struct SetFile {
    uint64_t seed = 1;       ///< the seed of the piece or concert
    double minutes = 0.0;    ///< piece length (0: compose.piece_minutes)
    double concert = 0.0;    ///< concert length (0: one piece)
    Curation curation;       ///< the rerolls
    std::string params;      ///< the parameters that differ from their defaults, as ParamStore text
};

/** @brief Writes a set; @p params' changed values are taken as they are now. */
bool saveSet(const char* path, const SetFile& set, const ParamStore& params);
/** @brief Reads a set and applies its parameters to @p params; @p error says why it failed. */
bool loadSet(const char* path, SetFile& set, ParamStore& params, std::string* error = nullptr);

} // namespace eph
