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
 */
#pragma once
#include "eph/Dsp.h"
#include "eph/Style.h"
#include <cstdint>
#include <vector>

namespace eph {

/** @brief What a section is. */
enum class SectionType : uint8_t { Atmo, Entry, Build, Lead, Peak, Breakdown, Bridge, Coda };
/** @brief German names for the markers ("Atmo", "Einsatz", "Aufbau", ...). */
const char* sectionName(SectionType t);

/** @brief One section. */
struct Section {
    SectionType type = SectionType::Atmo;
    int phase = 0;            ///< the sequence phase it belongs to (the Atmo to the first, a Bridge to the next)
    int index = 0;            ///< the n-th of its type in the phase (Build 0, 1, 2)
    double beat = 0.0;        ///< start
    double length = 16.0;     ///< beats
    float e0 = 0.0f, e1 = 0.0f;   ///< energy at start and end
};

/** @brief The form of one piece. */
struct PieceForm {
    std::vector<Section> sections;
    std::vector<double> phaseBpm;   ///< tempo of each phase
    std::vector<int> phaseKey;      ///< semitones of each phase's key from the piece's
    double lengthBeats = 0.0;
    /** @brief The energy at @p beat, interpolated inside its section. */
    float energy(double beat) const;
    /** @brief The first section of @p type in @p phase, or null. */
    const Section* find(SectionType type, int phase) const;
};

/**
 * @brief Draws a form.
 * @param profile the style profile
 * @param minutes length of the piece
 * @param bpm     tempo of the first phase
 * @param rng     the form's stream
 */
PieceForm drawForm(const StyleProfile& profile, double minutes, double bpm, Rng& rng);

} // namespace eph
