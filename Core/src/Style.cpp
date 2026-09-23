/**
 * @file Style.cpp
 * @brief The five profiles.
 */
#include "eph/Style.h"
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

const StyleProfile kProfiles[] = {
    // Cosmic: the seventies' sound, no drums, choir tapes, long intros, slow builds.
    { "Cosmic",  96.0f, 118.0f, 12.0f, 22.0f, 1, 2, 0.10f, 0.08f, 0.3f, 0.3f,
      3, { 13, 12, 7, 9 }, 0.2f, RowDivision::Bars4, 8,
      0.9f, 0.5f, 0.6f, 0.4f, 0.0f, TapeSet::Choir, 0.5f, hands(8.0, 14.0), -0.05f, 6.0f, 4.0f },
    // Doom: slow, low, dark, dissonant roots; strings tapes; few layers; much dynamics.
    { "Doom",    84.0f, 108.0f, 10.0f, 18.0f, 1, 2, 0.12f, 0.10f, 0.2f, 0.4f,
      3, { 11, 7, 13 }, 0.15f, RowDivision::Bars4, 6,
      0.7f, 0.3f, 0.3f, 0.2f, 0.05f, TapeSet::Strings, 0.35f, hands(10.0, 16.0), -0.15f, 7.5f, 0.0f },
    // Melodic: brighter, more rows and chord changes, leads, drums later in the piece, shorter pieces.
    { "Melodic", 104.0f, 126.0f, 7.0f, 12.0f, 1, 2, 0.05f, 0.05f, 0.4f, 0.4f,
      4, { 12, 8, 6, 16 }, 0.1f, RowDivision::Bars2, 8,
      0.6f, 0.6f, 0.9f, 0.2f, 0.8f, TapeSet::Strings, 0.75f, hands(7.0, 12.0), 0.05f, 4.5f, 3.0f },
    // Modern: hybrid and polished, cinematic pads, wider moves, sparse drums.
    { "Modern",  90.0f, 120.0f, 6.0f, 11.0f, 1, 2, 0.08f, 0.08f, 0.5f, 0.5f,
      4, { 7, 5, 12, 13 }, 0.3f, RowDivision::Bars2, 8,
      0.5f, 0.7f, 0.6f, 0.6f, 0.5f, TapeSet::Flute, 0.6f, hands(6.0, 11.0), 0.0f, 5.0f, 4.0f },
    // Drift: long, improvised, ambient phases between sequence episodes, tempo and key drift.
    { "Drift",   80.0f, 110.0f, 12.0f, 28.0f, 2, 3, 0.15f, 0.12f, 0.7f, 0.6f,
      2, { 13, 9 }, 0.3f, RowDivision::Bars4, 6,
      0.8f, 0.4f, 0.5f, 0.5f, 0.0f, TapeSet::Choir, 0.4f, hands(12.0, 18.0), -0.08f, 8.0f, 0.0f },
};

} // namespace

const StyleProfile& styleProfile(Style style)
{
    const int i = std::clamp(static_cast<int>(style), 0, static_cast<int>(Style::Count) - 1);
    return kProfiles[i];
}

} // namespace eph
