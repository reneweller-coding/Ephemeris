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

StyleProfile morphProfile(const StyleProfile& a, const StyleProfile& b, float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    if (t <= 0.0f) return a;
    if (t >= 1.0f) return b;
    auto mix = [t](float x, float y) { return x + t * (y - x); };
    auto mixi = [t](int x, int y) { return static_cast<int>(std::lround(static_cast<float>(x) + t * static_cast<float>(y - x))); };
    auto mixd = [t](double x, double y) { return x + static_cast<double>(t) * (y - x); };
    StyleProfile m = t < 0.5f ? a : b;   // the nearer one for what cannot be halfway
    m.bpmLow = mix(a.bpmLow, b.bpmLow);
    m.bpmHigh = mix(a.bpmHigh, b.bpmHigh);
    m.minutesLow = mix(a.minutesLow, b.minutesLow);
    m.minutesHigh = mix(a.minutesHigh, b.minutesHigh);
    m.phasesLow = mixi(a.phasesLow, b.phasesLow);
    m.phasesHigh = std::max(m.phasesLow, mixi(a.phasesHigh, b.phasesHigh));
    m.introShare = mix(a.introShare, b.introShare);
    m.codaShare = mix(a.codaShare, b.codaShare);
    m.newTempoChance = mix(a.newTempoChance, b.newTempoChance);
    m.newKeyChance = mix(a.newKeyChance, b.newKeyChance);
    m.peakRows = mixi(a.peakRows, b.peakRows);
    m.mutation = mix(a.mutation, b.mutation);
    m.tapeChance = mix(a.tapeChance, b.tapeChance);
    m.stringsChance = mix(a.stringsChance, b.stringsChance);
    m.leadChance = mix(a.leadChance, b.leadChance);
    m.bleepChance = mix(a.bleepChance, b.bleepChance);
    m.drumsChance = mix(a.drumsChance, b.drumsChance);
    m.leadIntensity = mix(a.leadIntensity, b.leadIntensity);
    m.hands.medianSeconds = mixd(a.hands.medianSeconds, b.hands.medianSeconds);
    m.hands.spread = mixd(a.hands.spread, b.hands.spread);
    m.hands.longChance = mixd(a.hands.longChance, b.hands.longChance);
    m.hands.quickChance = mixd(a.hands.quickChance, b.hands.quickChance);
    m.hands.restSeconds = mixd(a.hands.restSeconds, b.hands.restSeconds);
    m.darkness = mix(a.darkness, b.darkness);
    m.hallSeconds = mix(a.hallSeconds, b.hallSeconds);
    m.levelDb = mix(a.levelDb, b.levelDb);
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
    m.peakRows = std::clamp(p.peakRows + static_cast<int>(std::lround(2.0f * x)), 1, kRows - 1);
    m.bpmLow = p.bpmLow * (1.0f + 0.06f * x);
    m.bpmHigh = p.bpmHigh * (1.0f + 0.06f * x);
    m.tapeChance = chance(p.tapeChance, 0.3f);
    m.stringsChance = chance(p.stringsChance, 0.3f);
    m.leadChance = chance(p.leadChance, 0.4f);
    m.bleepChance = chance(p.bleepChance, 0.3f);
    m.drumsChance = p.drumsChance > 0.0f ? chance(p.drumsChance, 0.4f) : 0.0f;   // a style without drums stays without
    m.leadIntensity = std::clamp(p.leadIntensity + 0.3f * x, 0.1f, 1.0f);
    m.mutation = std::clamp(p.mutation + 0.1f * x, 0.0f, 1.0f);
    m.darkness = p.darkness + 0.15f * x;
    m.hands.medianSeconds = p.hands.medianSeconds * (1.0 - 0.3 * static_cast<double>(x));
    m.hands.restSeconds = p.hands.restSeconds * (1.0 - 0.3 * static_cast<double>(x));
    return m;
}

bool customStyleOn(const ParamStore& p)
{
    return p.getBool(p.id(Module::Custom, 0, custom::Use));
}

StyleProfile customProfile(const ParamStore& p, const StyleProfile& base)
{
    auto v = [&](int index) { return p.get(p.id(Module::Custom, 0, index)); };
    StyleProfile m = base;
    m.bpmLow = std::min(v(custom::BpmLow), v(custom::BpmHigh));
    m.bpmHigh = std::max(v(custom::BpmLow), v(custom::BpmHigh));
    m.minutesLow = std::min(v(custom::MinutesLow), v(custom::MinutesHigh));
    m.minutesHigh = std::max(v(custom::MinutesLow), v(custom::MinutesHigh));
    m.phasesLow = static_cast<int>(std::lround(std::min(v(custom::PhasesLow), v(custom::PhasesHigh))));
    m.phasesHigh = static_cast<int>(std::lround(std::max(v(custom::PhasesLow), v(custom::PhasesHigh))));
    m.introShare = v(custom::Intro);
    m.codaShare = v(custom::Coda);
    m.newTempoChance = v(custom::NewTempo);
    m.newKeyChance = v(custom::NewKey);
    m.peakRows = static_cast<int>(std::lround(v(custom::PeakRows)));
    m.mutation = v(custom::Mutation);
    m.tapeChance = v(custom::Tape);
    m.stringsChance = v(custom::Strings);
    m.leadChance = v(custom::Lead);
    m.bleepChance = v(custom::Bleeps);
    m.drumsChance = v(custom::Drums);
    m.leadIntensity = v(custom::LeadDensity);
    m.hands.medianSeconds = v(custom::HandMove);
    m.hands.restSeconds = v(custom::HandRest);
    m.darkness = v(custom::Darkness);
    m.hallSeconds = v(custom::Hall);
    m.levelDb = v(custom::Level);
    return m;
}

void copyToCustom(const StyleProfile& s, ParamStore& p)
{
    auto set = [&](int index, float value) { p.set(p.id(Module::Custom, 0, index), value); };
    set(custom::BpmLow, s.bpmLow); set(custom::BpmHigh, s.bpmHigh);
    set(custom::MinutesLow, s.minutesLow); set(custom::MinutesHigh, s.minutesHigh);
    set(custom::PhasesLow, static_cast<float>(s.phasesLow)); set(custom::PhasesHigh, static_cast<float>(s.phasesHigh));
    set(custom::Intro, s.introShare); set(custom::Coda, s.codaShare);
    set(custom::NewTempo, s.newTempoChance); set(custom::NewKey, s.newKeyChance);
    set(custom::PeakRows, static_cast<float>(s.peakRows)); set(custom::Mutation, s.mutation);
    set(custom::Tape, s.tapeChance); set(custom::Strings, s.stringsChance); set(custom::Lead, s.leadChance);
    set(custom::Bleeps, s.bleepChance); set(custom::Drums, s.drumsChance); set(custom::LeadDensity, s.leadIntensity);
    set(custom::HandMove, static_cast<float>(s.hands.medianSeconds)); set(custom::HandRest, static_cast<float>(s.hands.restSeconds));
    set(custom::Darkness, s.darkness); set(custom::Hall, s.hallSeconds); set(custom::Level, s.levelDb);
}

} // namespace eph
