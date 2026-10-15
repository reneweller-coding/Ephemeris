/**
 * @file Form.h
 * @brief The form of a piece (PLAN 2.7, 6.2): its sections, their lengths, their energy, their tempi.
 *
 * The grammar of PLAN 6.2, drawn with the numbers of a style profile:
 * @code
 *   Piece      -> Atmo SeqPhase (Bridge SeqPhase)* Coda
 *   SeqPhase   -> Entry Build{1..3} Lead? Peak Breakdown?     (Breakdown only before a Bridge)
 * @endcode
 * Lengths are drawn in seconds -- a share of the piece for the intro and the coda (the profile's), a
 * little for each bridge, the rest split between the phases and, inside a phase, by weights per section
 * -- and rounded to whole bars at the tempo of their phase, never under a floor per section type (a
 * peak is at least eight bars). A later phase may have its own tempo; the change happens at the start
 * of the bridge before it, where no row plays.
 *
 * Every section carries the energy at its start and its end (0..1): the arc the hands, the layers and
 * the lead follow (PLAN 6.8).
 *
 * **Conjunctions** (PLAN 6.2, snapToConjunctions()): once the rows are set up, a build -- which brings
 * in a counter row -- ends where that row and the bass row begin their cycles together again, if such
 * a bar is near its drawn end. The change then falls where the ear hears the pattern come round.
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/compose/Style.h"
#include <cstdint>
#include <vector>

namespace eph {

/** @brief What a section is. */
enum class SectionTypeE : uint8_t { ATMO, ENTRY, BUILD, LEAD, PEAK, BREAKDOWN, BRIDGE, CODA };
/** @brief German names for the markers ("Atmo", "Einsatz", "Aufbau", ...). */
const char* sectionName(SectionTypeE t);

/** @brief One section. */
struct Section {
    SectionTypeE m_type = SectionTypeE::ATMO;   ///< what the section is
    int m_phase = 0;            ///< the sequence phase it belongs to (the Atmo to the first, a Bridge to the next)
    int m_index = 0;            ///< the n-th of its type in the phase (Build 0, 1, 2)
    double m_beat = 0.0;        ///< start
    double m_length = 16.0;     ///< beats
    float m_e0 = 0.0f;          ///< energy at the start, 0..1
    float m_e1 = 0.0f;          ///< energy at the end
};

/** @brief The form of one piece. */
struct PieceForm {
    std::vector<Section> m_sections;   ///< the sections, in order, without gaps
    std::vector<double> m_phaseBpm;   ///< tempo of each phase
    std::vector<int> m_phaseKey;      ///< semitones of each phase's key from the piece's
    double m_lengthBeats = 0.0;       ///< the piece's length
    /** @brief The energy at @p beat, interpolated inside its section. */
    float energy(double beat) const;
    /** @brief The first section of @p type in @p phase, or null. */
    const Section* find(SectionTypeE type, int phase) const;
};

/**
 * @brief Draws a form.
 * @param profile the style profile
 * @param minutes length of the piece
 * @param bpm     tempo of the first phase
 * @param rng     the form's stream
 */
PieceForm drawForm(const StyleProfile& profile, double minutes, double bpm, Rng& rng);

/**
 * @brief Moves the end of every build onto a conjunction of the counter row it brings in with the bass row.
 *
 * The k-th build of a phase starts the k-th counter row (Composer.cpp); both then begin their cycles
 * together every lcm(bass cycle, row cycle) beats from the build's start. The build's end moves to the
 * nearest such beat if it lies within a quarter of the build (at least two bars) of the drawn end, and
 * the section after it gives or takes the difference; a section never falls under its floor, and every
 * other boundary stays where it was.
 * @param form      the form
 * @param bassBeats the bass row's cycle in beats
 * @param rowBeats  the counter rows' cycles in beats, in the order they come in
 * @return the number of builds moved
 */
int snapToConjunctions(PieceForm& form, double bassBeats, const std::vector<double>& rowBeats);

} // namespace eph
