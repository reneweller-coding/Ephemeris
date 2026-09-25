/**
 * @file Form.cpp
 * @brief The grammar of a piece.
 */
#include "eph/compose/Form.h"
#include <algorithm>
#include <cmath>

namespace eph {

const char* sectionName(SectionType t)
{
    switch (t) {
    case SectionType::Atmo:      return "Atmo";
    case SectionType::Entry:     return "Einsatz";
    case SectionType::Build:     return "Aufbau";
    case SectionType::Lead:      return "Lead";
    case SectionType::Peak:      return "Hoehepunkt";
    case SectionType::Breakdown: return "Abbau";
    case SectionType::Bridge:    return "Bruecke";
    case SectionType::Coda:      return "Ausklang";
    }
    return "?";
}

float PieceForm::energy(double beat) const
{
    for (const Section& s : sections) {
        if (beat < s.beat + s.length || &s == &sections.back()) {
            const double t = std::clamp((beat - s.beat) / std::max(1.0, s.length), 0.0, 1.0);
            return static_cast<float>(s.e0 + t * (s.e1 - s.e0));
        }
    }
    return 0.0f;
}

const Section* PieceForm::find(SectionType type, int phase) const
{
    for (const Section& s : sections) if (s.type == type && s.phase == phase) return &s;
    return nullptr;
}

namespace {

int minBars(SectionType t)
{
    switch (t) {
    case SectionType::Lead: case SectionType::Peak: return 8;
    default: return 4;
    }
}

} // namespace

PieceForm drawForm(const StyleProfile& prof, double minutes, double bpm, Rng& rng)
{
    PieceForm f;
    const int phases = prof.phasesLow + rng.below(prof.phasesHigh - prof.phasesLow + 1);
    // Tempo and key of every phase.
    for (int p = 0; p < phases; ++p) {
        double b = bpm;
        int key = 0;
        if (p > 0) {
            if (rng.uniform() < prof.newTempoChance)
                b = std::clamp(bpm * (0.88 + 0.24 * static_cast<double>(rng.uniform())), static_cast<double>(prof.bpmLow), static_cast<double>(prof.bpmHigh));
            if (rng.uniform() < prof.newKeyChance) {
                const int moves[4] = { 5, -5, 3, -2 };   // up a fourth, down a fourth, the relative, a tone down
                key = moves[rng.below(4)];
            }
        }
        f.phaseBpm.push_back(std::round(b * 10.0) / 10.0);
        f.phaseKey.push_back(key);
    }

    // Seconds: intro, coda, bridges, the rest to the phases.
    const double total = std::max(4.0, minutes) * 60.0;
    const double intro = prof.introShare * total, coda = prof.codaShare * total;
    const double bridge = 0.06 * total;
    const double phaseTotal = std::max(60.0, total - intro - coda - bridge * (phases - 1));
    std::vector<double> phaseSec;
    double sum = 0.0;
    for (int p = 0; p < phases; ++p) { phaseSec.push_back(0.8 + 0.4 * static_cast<double>(rng.uniform())); sum += phaseSec.back(); }
    for (double& x : phaseSec) x *= phaseTotal / sum;

    auto add = [&](SectionType t, int phase, int index, double seconds, double tempo, float e0, float e1) {
        Section s;
        s.type = t; s.phase = phase; s.index = index;
        s.beat = f.lengthBeats;
        const int bars = std::max(minBars(t), static_cast<int>(std::lround(seconds * tempo / 60.0 / kBeatsPerBar)));
        s.length = bars * kBeatsPerBar;
        s.e0 = e0; s.e1 = e1;
        f.sections.push_back(s);
        f.lengthBeats += s.length;
    };

    add(SectionType::Atmo, 0, 0, intro, f.phaseBpm[0], 0.05f, 0.15f);
    for (int p = 0; p < phases; ++p) {
        const double tempo = f.phaseBpm[static_cast<size_t>(p)];
        const bool last = p == phases - 1;
        const int builds = 1 + rng.below(3);
        const bool lead = rng.uniform() < prof.leadChance;
        // Weights of the sections inside the phase.
        std::vector<std::pair<SectionType, double>> parts = { { SectionType::Entry, 0.10 } };
        for (int b = 0; b < builds; ++b) parts.push_back({ SectionType::Build, 0.18 });
        if (lead) parts.push_back({ SectionType::Lead, 0.16 });
        parts.push_back({ SectionType::Peak, 0.14 });
        if (!last) parts.push_back({ SectionType::Breakdown, 0.08 });
        double w = 0.0;
        for (const auto& x : parts) w += x.second;
        int b = 0;
        for (const auto& x : parts) {
            const double sec = phaseSec[static_cast<size_t>(p)] * x.second / w;
            switch (x.first) {
            case SectionType::Entry: add(x.first, p, 0, sec, tempo, 0.2f, 0.3f); break;
            case SectionType::Build: {
                const float a = 0.3f + 0.45f * static_cast<float>(b) / builds, c = 0.3f + 0.45f * static_cast<float>(b + 1) / builds;
                add(x.first, p, b, sec, tempo, a, c);
                ++b;
                break;
            }
            case SectionType::Lead: add(x.first, p, 0, sec, tempo, 0.75f, 0.85f); break;
            case SectionType::Peak: add(x.first, p, 0, sec, tempo, 0.95f, 1.0f); break;
            case SectionType::Breakdown: add(x.first, p, 0, sec, tempo, 0.5f, 0.35f); break;
            default: break;
            }
        }
        if (!last) add(SectionType::Bridge, p + 1, 0, bridge, f.phaseBpm[static_cast<size_t>(p + 1)], 0.25f, 0.2f);
    }
    add(SectionType::Coda, phases - 1, 0, coda, f.phaseBpm.back(), 0.3f, 0.0f);
    return f;
}

} // namespace eph
