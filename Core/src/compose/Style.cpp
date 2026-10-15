/**
 * @file Style.cpp
 * @brief The five profiles.
 */
#include "eph/compose/Style.h"
#include "eph/Params.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {

/** @brief A player's hands: a gesture's median length and the rest between, seconds. */
HandStyle hands(double medianSeconds, double restSeconds)
{
    HandStyle h;
    h.m_medianSeconds = medianSeconds;
    h.m_restSeconds = restSeconds;
    return h;
}

/// Designated initializers (C++20): every value stands by its name, in the order of Style.h.
const StyleProfile Profiles[] = {
    // Cosmic: the seventies' sound, no drums, choir tapes, long intros, slow builds.
    {
        .m_name = "Cosmic",
        .m_bpmLow = 112.0f,
        .m_bpmHigh = 126.0f,
        .m_minutesLow = 12.0f,
        .m_minutesHigh = 22.0f,
        .m_phasesLow = 1,
        .m_phasesHigh = 2,
        .m_introShare = 0.08f,
        .m_codaShare = 0.10f,
        .m_newTempoChance = 0.3f,
        .m_newKeyChance = 0.3f,
        .m_peakRows = 3,
        .m_counterLengths = { 13, 12, 7, 9 },
        .m_mutation = 0.2f,
        .m_transposerDivision = RowDivisionE::BARS4,
        .m_transposerLength = 8,
        .m_tapeChance = 0.9f,
        .m_stringsChance = 0.5f,
        .m_leadChance = 0.6f,
        .m_bleepChance = 0.4f,
        .m_drumsChance = 0.0f,
        .m_tape = TapeSetE::CHOIR,
        .m_leadIntensity = 0.5f,
        .m_hands = hands(8.0, 14.0),
        .m_darkness = -0.05f,
        .m_hallSeconds = 6.0f,
        .m_levelDb = 4.8f,
        .m_grainChance = 0.6f,
        .m_peakLufs = -14.5f,   // the loudest part's target (Leveler.h: the mean of four pieces, 26.09.2026)
    },
    // Doom: slow, low, dark, dissonant roots; strings tapes; few layers; much dynamics.
    {
        .m_name = "Doom",
        .m_bpmLow = 92.0f,
        .m_bpmHigh = 110.0f,
        .m_minutesLow = 10.0f,
        .m_minutesHigh = 18.0f,
        .m_phasesLow = 1,
        .m_phasesHigh = 2,
        .m_introShare = 0.09f,
        .m_codaShare = 0.10f,
        .m_newTempoChance = 0.2f,
        .m_newKeyChance = 0.4f,
        .m_peakRows = 3,
        .m_counterLengths = { 11, 7, 13 },
        .m_mutation = 0.15f,
        .m_transposerDivision = RowDivisionE::BARS4,
        .m_transposerLength = 6,
        .m_tapeChance = 0.7f,
        .m_stringsChance = 0.3f,
        .m_leadChance = 0.3f,
        .m_bleepChance = 0.2f,
        .m_drumsChance = 0.05f,
        .m_tape = TapeSetE::STRINGS,
        .m_leadIntensity = 0.35f,
        .m_hands = hands(10.0, 16.0),
        .m_darkness = -0.15f,
        .m_hallSeconds = 7.5f,
        .m_levelDb = 2.8f,
        .m_grainChance = 0.3f,
        .m_peakLufs = -15.7f,   // the loudest part's target (Leveler.h: the mean of four pieces, 26.09.2026)
    },
    // Melodic: brighter, more rows and chord changes, leads, drums later in the piece, shorter pieces.
    {
        .m_name = "Melodic",
        .m_bpmLow = 118.0f,
        .m_bpmHigh = 132.0f,
        .m_minutesLow = 7.0f,
        .m_minutesHigh = 12.0f,
        .m_phasesLow = 1,
        .m_phasesHigh = 2,
        .m_introShare = 0.07f,
        .m_codaShare = 0.08f,
        .m_newTempoChance = 0.4f,
        .m_newKeyChance = 0.4f,
        .m_peakRows = 4,
        .m_counterLengths = { 12, 8, 6, 16 },
        .m_mutation = 0.1f,
        .m_transposerDivision = RowDivisionE::BARS2,
        .m_transposerLength = 8,
        .m_tapeChance = 0.6f,
        .m_stringsChance = 0.6f,
        .m_leadChance = 0.9f,
        .m_bleepChance = 0.2f,
        .m_drumsChance = 0.8f,
        .m_tape = TapeSetE::STRINGS,
        .m_leadIntensity = 0.75f,
        .m_hands = hands(7.0, 12.0),
        .m_darkness = 0.05f,
        .m_hallSeconds = 4.5f,
        .m_levelDb = 3.3f,
        .m_grainChance = 0.2f,
        .m_peakLufs = -13.4f,   // the loudest part's target (Leveler.h: the mean of four pieces, 26.09.2026)
    },
    // Modern: hybrid and polished, cinematic pads, wider moves, sparse drums.
    {
        .m_name = "Modern",
        .m_bpmLow = 118.0f,
        .m_bpmHigh = 130.0f,
        .m_minutesLow = 6.0f,
        .m_minutesHigh = 11.0f,
        .m_phasesLow = 1,
        .m_phasesHigh = 2,
        .m_introShare = 0.06f,
        .m_codaShare = 0.10f,
        .m_newTempoChance = 0.5f,
        .m_newKeyChance = 0.5f,
        .m_peakRows = 4,
        .m_counterLengths = { 7, 5, 12, 13 },
        .m_mutation = 0.3f,
        .m_transposerDivision = RowDivisionE::BARS2,
        .m_transposerLength = 8,
        .m_tapeChance = 0.5f,
        .m_stringsChance = 0.7f,
        .m_leadChance = 0.6f,
        .m_bleepChance = 0.6f,
        .m_drumsChance = 0.5f,
        .m_tape = TapeSetE::FLUTE,
        .m_leadIntensity = 0.6f,
        .m_hands = hands(6.0, 11.0),
        .m_darkness = 0.0f,
        .m_hallSeconds = 5.0f,
        .m_levelDb = 4.5f,
        .m_grainChance = 0.4f,
        .m_peakLufs = -12.8f,   // the loudest part's target (Leveler.h: the mean of four pieces, 26.09.2026)
    },
    // Drift: long, improvised, ambient phases between sequence episodes, tempo and key drift.
    {
        .m_name = "Drift",
        .m_bpmLow = 100.0f,
        .m_bpmHigh = 118.0f,
        .m_minutesLow = 12.0f,
        .m_minutesHigh = 28.0f,
        .m_phasesLow = 2,
        .m_phasesHigh = 3,
        .m_introShare = 0.09f,
        .m_codaShare = 0.12f,
        .m_newTempoChance = 0.7f,
        .m_newKeyChance = 0.6f,
        .m_peakRows = 2,
        .m_counterLengths = { 13, 9 },
        .m_mutation = 0.3f,
        .m_transposerDivision = RowDivisionE::BARS4,
        .m_transposerLength = 6,
        .m_tapeChance = 0.8f,
        .m_stringsChance = 0.4f,
        .m_leadChance = 0.5f,
        .m_bleepChance = 0.5f,
        .m_drumsChance = 0.0f,
        .m_tape = TapeSetE::CHOIR,
        .m_leadIntensity = 0.4f,
        .m_hands = hands(12.0, 18.0),
        .m_darkness = -0.08f,
        .m_hallSeconds = 8.0f,
        .m_levelDb = 4.0f,
        .m_grainChance = 0.7f,
        .m_peakLufs = -15.7f,   // the loudest part's target (Leveler.h: the mean of four pieces, 26.09.2026)
    },
};

} // namespace

const StyleProfile& styleProfile(StyleE style)
{
    const int i = std::clamp(static_cast<int>(style), 0, static_cast<int>(StyleE::COUNT) - 1);
    return Profiles[i];
}

StyleProfile morphProfile(const StyleProfile& a, const StyleProfile& b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    auto mix = [t](float x, float y) { return x + t * (y - x); };
    auto mixi = [t](int x, int y) { return static_cast<int>(std::lround(static_cast<float>(x) + t * static_cast<float>(y - x))); };
    auto mixd = [t](double x, double y) { return x + static_cast<double>(t) * (y - x); };
    StyleProfile m = t < 0.5f ? a : b;   // the nearer one for what cannot be halfway
    m.m_bpmLow = mix(a.m_bpmLow, b.m_bpmLow);
    m.m_bpmHigh = mix(a.m_bpmHigh, b.m_bpmHigh);
    m.m_minutesLow = mix(a.m_minutesLow, b.m_minutesLow);
    m.m_minutesHigh = mix(a.m_minutesHigh, b.m_minutesHigh);
    m.m_phasesLow = mixi(a.m_phasesLow, b.m_phasesLow);
    m.m_phasesHigh = std::max(m.m_phasesLow, mixi(a.m_phasesHigh, b.m_phasesHigh));
    m.m_introShare = mix(a.m_introShare, b.m_introShare);
    m.m_codaShare = mix(a.m_codaShare, b.m_codaShare);
    m.m_newTempoChance = mix(a.m_newTempoChance, b.m_newTempoChance);
    m.m_newKeyChance = mix(a.m_newKeyChance, b.m_newKeyChance);
    m.m_peakRows = mixi(a.m_peakRows, b.m_peakRows);
    m.m_mutation = mix(a.m_mutation, b.m_mutation);
    m.m_tapeChance = mix(a.m_tapeChance, b.m_tapeChance);
    m.m_stringsChance = mix(a.m_stringsChance, b.m_stringsChance);
    m.m_leadChance = mix(a.m_leadChance, b.m_leadChance);
    m.m_bleepChance = mix(a.m_bleepChance, b.m_bleepChance);
    m.m_drumsChance = mix(a.m_drumsChance, b.m_drumsChance);
    m.m_leadIntensity = mix(a.m_leadIntensity, b.m_leadIntensity);
    m.m_hands.m_medianSeconds = mixd(a.m_hands.m_medianSeconds, b.m_hands.m_medianSeconds);
    m.m_hands.m_spread = mixd(a.m_hands.m_spread, b.m_hands.m_spread);
    m.m_hands.m_longChance = mixd(a.m_hands.m_longChance, b.m_hands.m_longChance);
    m.m_hands.m_quickChance = mixd(a.m_hands.m_quickChance, b.m_hands.m_quickChance);
    m.m_hands.m_restSeconds = mixd(a.m_hands.m_restSeconds, b.m_hands.m_restSeconds);
    m.m_darkness = mix(a.m_darkness, b.m_darkness);
    m.m_hallSeconds = mix(a.m_hallSeconds, b.m_hallSeconds);
    m.m_levelDb = mix(a.m_levelDb, b.m_levelDb);
    m.m_grainChance = mix(a.m_grainChance, b.m_grainChance);
    m.m_peakLufs = mix(a.m_peakLufs, b.m_peakLufs);
    return m;
}

float concertArc(float t)
{
    // A raised sine whose peak is moved from the middle to 60 %: sin(pi t^e) with 0.6^e = 0.5.
    t = std::clamp(t, 0.0f, 1.0f);
    const float e = std::log(0.5f) / std::log(0.6f);
    return std::sin(3.14159265f * std::pow(t, e));
}

StyleProfile arcProfile(const StyleProfile& p, float tension, float strength)
{
    const float x = std::clamp(tension, -0.5f, 0.5f) * std::clamp(strength, 0.0f, 1.0f);
    if (x == 0.0f) return p;
    StyleProfile m = p;
    auto chance = [x](float c, float amount) { return std::clamp(c + amount * x, 0.0f, 1.0f); };
    m.m_peakRows = std::clamp(p.m_peakRows + static_cast<int>(std::lround(2.0f * x)), 1, Rows - 1);
    m.m_bpmLow = p.m_bpmLow * (1.0f + 0.06f * x);
    m.m_bpmHigh = p.m_bpmHigh * (1.0f + 0.06f * x);
    m.m_tapeChance = chance(p.m_tapeChance, 0.3f);
    m.m_stringsChance = chance(p.m_stringsChance, 0.3f);
    m.m_leadChance = chance(p.m_leadChance, 0.4f);
    m.m_bleepChance = chance(p.m_bleepChance, 0.3f);
    m.m_drumsChance = p.m_drumsChance > 0.0f ? chance(p.m_drumsChance, 0.4f) : 0.0f;   // a style without drums stays without
    m.m_leadIntensity = std::clamp(p.m_leadIntensity + 0.3f * x, 0.1f, 1.0f);
    m.m_mutation = std::clamp(p.m_mutation + 0.1f * x, 0.0f, 1.0f);
    m.m_darkness = p.m_darkness + 0.15f * x;
    m.m_hands.m_medianSeconds = p.m_hands.m_medianSeconds * (1.0 - 0.3 * static_cast<double>(x));
    m.m_hands.m_restSeconds = p.m_hands.m_restSeconds * (1.0 - 0.3 * static_cast<double>(x));
    return m;
}

bool customStyleOn(const ParamStore& p)
{
    return p.getBool(p.id(ModuleE::CUSTOM, 0, custom::USE));
}

StyleProfile customProfile(const ParamStore& p, const StyleProfile& base)
{
    auto v = [&](int index) { return p.get(p.id(ModuleE::CUSTOM, 0, index)); };
    StyleProfile m = base;
    m.m_bpmLow = std::min(v(custom::BPM_LOW), v(custom::BPM_HIGH));
    m.m_bpmHigh = std::max(v(custom::BPM_LOW), v(custom::BPM_HIGH));
    m.m_minutesLow = std::min(v(custom::MINUTES_LOW), v(custom::MINUTES_HIGH));
    m.m_minutesHigh = std::max(v(custom::MINUTES_LOW), v(custom::MINUTES_HIGH));
    m.m_phasesLow = static_cast<int>(std::lround(std::min(v(custom::PHASES_LOW), v(custom::PHASES_HIGH))));
    m.m_phasesHigh = static_cast<int>(std::lround(std::max(v(custom::PHASES_LOW), v(custom::PHASES_HIGH))));
    m.m_introShare = v(custom::INTRO);
    m.m_codaShare = v(custom::CODA);
    m.m_newTempoChance = v(custom::NEW_TEMPO);
    m.m_newKeyChance = v(custom::NEW_KEY);
    m.m_peakRows = static_cast<int>(std::lround(v(custom::PEAK_ROWS)));
    m.m_mutation = v(custom::MUTATION);
    m.m_tapeChance = v(custom::TAPE);
    m.m_stringsChance = v(custom::STRINGS);
    m.m_leadChance = v(custom::LEAD);
    m.m_bleepChance = v(custom::BLEEPS);
    m.m_drumsChance = v(custom::DRUMS);
    m.m_leadIntensity = v(custom::LEAD_DENSITY);
    m.m_hands.m_medianSeconds = v(custom::HAND_MOVE);
    m.m_hands.m_restSeconds = v(custom::HAND_REST);
    m.m_darkness = v(custom::DARKNESS);
    m.m_hallSeconds = v(custom::HALL);
    m.m_levelDb = v(custom::LEVEL);
    m.m_peakLufs = base.m_peakLufs + (m.m_levelDb - base.m_levelDb);   // the user's level moves the target with it
    return m;
}

void copyToCustom(const StyleProfile& s, ParamStore& p)
{
    auto set = [&](int index, float value) { p.set(p.id(ModuleE::CUSTOM, 0, index), value); };
    set(custom::BPM_LOW, s.m_bpmLow); set(custom::BPM_HIGH, s.m_bpmHigh);
    set(custom::MINUTES_LOW, s.m_minutesLow); set(custom::MINUTES_HIGH, s.m_minutesHigh);
    set(custom::PHASES_LOW, static_cast<float>(s.m_phasesLow)); set(custom::PHASES_HIGH, static_cast<float>(s.m_phasesHigh));
    set(custom::INTRO, s.m_introShare); set(custom::CODA, s.m_codaShare);
    set(custom::NEW_TEMPO, s.m_newTempoChance); set(custom::NEW_KEY, s.m_newKeyChance);
    set(custom::PEAK_ROWS, static_cast<float>(s.m_peakRows)); set(custom::MUTATION, s.m_mutation);
    set(custom::TAPE, s.m_tapeChance); set(custom::STRINGS, s.m_stringsChance); set(custom::LEAD, s.m_leadChance);
    set(custom::BLEEPS, s.m_bleepChance); set(custom::DRUMS, s.m_drumsChance); set(custom::LEAD_DENSITY, s.m_leadIntensity);
    set(custom::HAND_MOVE, static_cast<float>(s.m_hands.m_medianSeconds)); set(custom::HAND_REST, static_cast<float>(s.m_hands.m_restSeconds));
    set(custom::DARKNESS, s.m_darkness); set(custom::HALL, s.m_hallSeconds); set(custom::LEVEL, s.m_levelDb);
}

} // namespace eph
