/**
 * @file Style.cpp
 * @brief The five profiles.
 */
#include "eph/compose/Style.h"
#include <algorithm>

namespace eph {

namespace {

HandStyle hands(double medianSeconds, double restSeconds)
{
    HandStyle h;
    h.medianSeconds = medianSeconds;
    h.restSeconds = restSeconds;
    return h;
}

// Designated initializers (C++20): every value stands by its name, in the order of Style.h.
const StyleProfile kProfiles[] = {
    // Cosmic: the seventies' sound, no drums, choir tapes, long intros, slow builds.
    {
        .name = "Cosmic",
        .bpmLow = 96.0f,
        .bpmHigh = 118.0f,
        .minutesLow = 12.0f,
        .minutesHigh = 22.0f,
        .phasesLow = 1,
        .phasesHigh = 2,
        .introShare = 0.10f,
        .codaShare = 0.08f,
        .newTempoChance = 0.3f,
        .newKeyChance = 0.3f,
        .peakRows = 3,
        .counterLengths = { 13, 12, 7, 9 },
        .mutation = 0.2f,
        .transposerDivision = RowDivision::Bars4,
        .transposerLength = 8,
        .tapeChance = 0.9f,
        .stringsChance = 0.5f,
        .leadChance = 0.6f,
        .bleepChance = 0.4f,
        .drumsChance = 0.0f,
        .tape = TapeSet::Choir,
        .leadIntensity = 0.5f,
        .hands = hands(8.0, 14.0),
        .darkness = -0.05f,
        .hallSeconds = 6.0f,
        .levelDb = 4.0f,
    },
    // Doom: slow, low, dark, dissonant roots; strings tapes; few layers; much dynamics.
    {
        .name = "Doom",
        .bpmLow = 84.0f,
        .bpmHigh = 108.0f,
        .minutesLow = 10.0f,
        .minutesHigh = 18.0f,
        .phasesLow = 1,
        .phasesHigh = 2,
        .introShare = 0.12f,
        .codaShare = 0.10f,
        .newTempoChance = 0.2f,
        .newKeyChance = 0.4f,
        .peakRows = 3,
        .counterLengths = { 11, 7, 13 },
        .mutation = 0.15f,
        .transposerDivision = RowDivision::Bars4,
        .transposerLength = 6,
        .tapeChance = 0.7f,
        .stringsChance = 0.3f,
        .leadChance = 0.3f,
        .bleepChance = 0.2f,
        .drumsChance = 0.05f,
        .tape = TapeSet::Strings,
        .leadIntensity = 0.35f,
        .hands = hands(10.0, 16.0),
        .darkness = -0.15f,
        .hallSeconds = 7.5f,
        .levelDb = 0.0f,
    },
    // Melodic: brighter, more rows and chord changes, leads, drums later in the piece, shorter pieces.
    {
        .name = "Melodic",
        .bpmLow = 104.0f,
        .bpmHigh = 126.0f,
        .minutesLow = 7.0f,
        .minutesHigh = 12.0f,
        .phasesLow = 1,
        .phasesHigh = 2,
        .introShare = 0.05f,
        .codaShare = 0.05f,
        .newTempoChance = 0.4f,
        .newKeyChance = 0.4f,
        .peakRows = 4,
        .counterLengths = { 12, 8, 6, 16 },
        .mutation = 0.1f,
        .transposerDivision = RowDivision::Bars2,
        .transposerLength = 8,
        .tapeChance = 0.6f,
        .stringsChance = 0.6f,
        .leadChance = 0.9f,
        .bleepChance = 0.2f,
        .drumsChance = 0.8f,
        .tape = TapeSet::Strings,
        .leadIntensity = 0.75f,
        .hands = hands(7.0, 12.0),
        .darkness = 0.05f,
        .hallSeconds = 4.5f,
        .levelDb = 3.0f,
    },
    // Modern: hybrid and polished, cinematic pads, wider moves, sparse drums.
    {
        .name = "Modern",
        .bpmLow = 90.0f,
        .bpmHigh = 120.0f,
        .minutesLow = 6.0f,
        .minutesHigh = 11.0f,
        .phasesLow = 1,
        .phasesHigh = 2,
        .introShare = 0.08f,
        .codaShare = 0.08f,
        .newTempoChance = 0.5f,
        .newKeyChance = 0.5f,
        .peakRows = 4,
        .counterLengths = { 7, 5, 12, 13 },
        .mutation = 0.3f,
        .transposerDivision = RowDivision::Bars2,
        .transposerLength = 8,
        .tapeChance = 0.5f,
        .stringsChance = 0.7f,
        .leadChance = 0.6f,
        .bleepChance = 0.6f,
        .drumsChance = 0.5f,
        .tape = TapeSet::Flute,
        .leadIntensity = 0.6f,
        .hands = hands(6.0, 11.0),
        .darkness = 0.0f,
        .hallSeconds = 5.0f,
        .levelDb = 4.0f,
    },
    // Drift: long, improvised, ambient phases between sequence episodes, tempo and key drift.
    {
        .name = "Drift",
        .bpmLow = 80.0f,
        .bpmHigh = 110.0f,
        .minutesLow = 12.0f,
        .minutesHigh = 28.0f,
        .phasesLow = 2,
        .phasesHigh = 3,
        .introShare = 0.15f,
        .codaShare = 0.12f,
        .newTempoChance = 0.7f,
        .newKeyChance = 0.6f,
        .peakRows = 2,
        .counterLengths = { 13, 9 },
        .mutation = 0.3f,
        .transposerDivision = RowDivision::Bars4,
        .transposerLength = 6,
        .tapeChance = 0.8f,
        .stringsChance = 0.4f,
        .leadChance = 0.5f,
        .bleepChance = 0.5f,
        .drumsChance = 0.0f,
        .tape = TapeSet::Choir,
        .leadIntensity = 0.4f,
        .hands = hands(12.0, 18.0),
        .darkness = -0.08f,
        .hallSeconds = 8.0f,
        .levelDb = 0.0f,
    },
};

} // namespace

const StyleProfile& styleProfile(Style style)
{
    const int i = std::clamp(static_cast<int>(style), 0, static_cast<int>(Style::Count) - 1);
    return kProfiles[i];
}

} // namespace eph
