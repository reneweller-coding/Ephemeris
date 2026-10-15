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
 * @author Rene Weller
 */
#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace eph {

class ParamStore;

/** @brief The rerolls of a piece or concert: unit name -> how often it was drawn again. */
struct Curation {
    std::map<std::string, int> m_rerolls;   ///< unit name -> reroll count
    /**
     * @brief The counter of @p unit (0 if never rerolled).
     * @param[in] unit  the unit's name
     * @return how often it was rerolled
     */
    int count(const std::string& unit) const { const auto it = m_rerolls.find(unit); return it == m_rerolls.end() ? 0 : it->second; }
    /**
     * @brief Draws @p unit once more.
     * @param[in] unit  the unit's name
     */
    void reroll(const std::string& unit) { ++m_rerolls[unit]; }
};

/** @brief Everything a set file holds. */
struct SetFile {
    uint64_t m_seed = 1;       ///< the seed of the piece or concert
    double m_minutes = 0.0;    ///< piece length (0: compose.piece_minutes)
    double m_concert = 0.0;    ///< concert length (0: one piece)
    Curation m_curation;       ///< the rerolls
    std::string m_params;      ///< the parameters that differ from their defaults, as ParamStore text
    /** 26.09.2026: the parameters hold the composer's sounds (Score::knobs) as they were played; an older set has
     *  them as offsets in its score, so its piece puts them on the knobs when it loads. */
    bool m_soundsInParams = false;
};

/**
 * @brief Writes a set; @p params' changed values are taken as they are now.
 * @param[in] path    the file
 * @param[in] set     the set's seed, lengths and rerolls
 * @param[in] params  the knobs
 * @return false when the file cannot be written
 */
bool saveSet(const char* path, const SetFile& set, const ParamStore& params);
/**
 * @brief Reads a set and applies its parameters to @p params; @p error says why it failed.
 * @param[in]  path    the file
 * @param[out] set     receives the set's seed, lengths and rerolls
 * @param[out] params  receives the knobs
 * @param[out] error   receives why it failed, or null
 * @return false when the file cannot be read
 */
bool loadSet(const char* path, SetFile& set, ParamStore& params, std::string* error = nullptr);

} // namespace eph
