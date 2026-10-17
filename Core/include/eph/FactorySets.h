/**
 * @file FactorySets.h
 * @brief The factory's concerts (17.10.2026): 256 concerts to begin a night with, each a `.ephset` with a name, its
 *        style (or "Night sets"), its form and its length.
 *
 * A factory concert is what a player would save after an evening of curating: a seed and the concert's knobs -- the
 * style, the concert's length, a piece's length, and its form: pieces flowing into each other, an arc of tension
 * (compose.concert_arc), an album (compose.album), a journey to another style (compose.morph_to), a long night, or a
 * night set mixing the styles (compose.night_set). Everything else stays at its default, so the composer picks every
 * piece's sounds as always. Loaded in the plugin (the Sets browser), listed by the render app (Ephemeris Sets) and by
 * Heliacal. Tools/sets/make_sets.py draws them (FactorySetsData.inc) and writes in how long each plays, measured by the
 * render app (its --catalog composes every one); every eighth is composed again in the self-test.
 * @author Rene Weller
 */
#pragma once

namespace eph
{

/** @brief A concert of the factory. */
struct FactorySet
{
    const char *m_name;      ///< its name
    const char *m_section;   ///< its style (StyleNames), or "Night sets"
    const char *m_group;     ///< its form ("Concert", "Arc", "Album", "Journey", "Long night"), a night set's length
    const char *m_note;      ///< a few words: its length, its form
    const char *m_text;      ///< the `.ephset` (loadSetText)
    double      m_seconds;   ///< how long it plays, as composed when the bank was made (0: not measured)
};

/**
 * @brief How many concerts the factory has.
 * @return the count
 */
int numFactorySets();

/**
 * @brief A concert of the factory.
 * @param[in] index  0 .. numFactorySets() - 1 (clamped)
 * @return the concert
 */
const FactorySet &factorySet( int index );

/**
 * @brief A concert of the factory by its name.
 * @param[in] name  the name
 * @return its index, -1 when there is none of that name
 */
int findFactorySet( const char *name );

}   // namespace eph
