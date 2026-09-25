/**
 * @file Composer.cpp
 * @brief A piece and a concert.
 */
#include "eph/compose/Composer.h"
#include "eph/compose/Form.h"
#include "eph/compose/GestureEngine.h"
#include "eph/compose/Harmony.h"
#include "eph/compose/Lead.h"
#include "eph/compose/Pads.h"
#include "eph/Rack.h"
#include "eph/compose/Style.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

namespace eph {

namespace {

/** @brief The streams of a piece: one per step of PLAN 6, so changing one leaves the others. */
enum Stream : uint64_t { sForm = 1, sTempo, sRows, sRack, sLayers, sLead, sPads, sHands };

/** @brief The offset in a knob's normalised range that takes it from its default to @p value. */
float offsetTo(const ParamStore& p, int id, float value)
{
    return p.toNormalised(id, value) - p.toNormalised(id, p.get(id));
}

/**
 * @brief Drums for a span, by style (PLAN 5.7): an eighties kit in "Melodic", a sparse one in "Modern",
 *        a lone tom now and then in "Doom". General MIDI numbers; toms are tuned by the engine.
 */
void writeDrums(Score& s, Style style, double from, double to, Rng& rng)
{
    auto hit = [&](double beat, int note, float vel) {
        if (beat >= from && beat < to) s.notes.push_back({ beat, 0.1, Part::Drums, note, vel, false, false });
    };
    const bool sixteenths = rng.uniform() < 0.5f, shaker = rng.uniform() < 0.4f;
    int bar = 0;
    for (double b = std::ceil(from / kBeatsPerBar) * kBeatsPerBar; b < to; b += kBeatsPerBar, ++bar) {
        const bool fill = (bar % 8) == 7;
        switch (style) {
        case Style::Melodic:
            hit(b, 36, 0.95f);
            hit(b + 2.0, 36, 0.9f);
            if (rng.uniform() < 0.3f) hit(b + 1.75, 36, 0.6f);
            hit(b + 1.0, 38, 0.85f);
            if (!fill) hit(b + 3.0, 38, 0.85f);
            for (double h = 0.0; h < 4.0; h += sixteenths ? 0.25 : 0.5)
                if (!(fill && h >= 3.0)) hit(b + h, 42, std::fmod(h, 1.0) == 0.5 ? 0.75f : 0.5f);
            if (rng.uniform() < 0.5f && !fill) hit(b + 3.5, 46, 0.6f);
            if (fill) { hit(b + 3.0, 48, 0.8f); hit(b + 3.25, 48, 0.7f); hit(b + 3.5, 45, 0.8f); hit(b + 3.75, 45, 0.75f); }
            if (shaker) for (double h = 0.0; h < 4.0; h += 0.25) hit(b + h, 70, 0.35f);
            break;
        case Style::Modern:
            hit(b, 36, 0.9f);
            if (rng.uniform() < 0.4f) hit(b + 2.5, 36, 0.7f);
            hit(b + 2.0, 38, 0.8f);   // half time
            for (double h = 0.5; h < 4.0; h += 1.0) if (rng.uniform() < 0.5f) hit(b + h, 37, 0.6f);
            for (double h = 0.0; h < 4.0; h += 0.25) if (rng.uniform() < 0.6f) hit(b + h, 42, 0.3f + 0.3f * rng.uniform());
            if (shaker) for (double h = 0.0; h < 4.0; h += 0.5) hit(b + h, 70, 0.3f);
            if ((bar % 16) == 15) { hit(b + 3.0, 48, 0.7f); hit(b + 3.5, 45, 0.7f); }
            break;
        case Style::Doom:
            if (bar % 2 == 0) hit(b, 45, 0.8f);
            break;
        default: break;
        }
    }
}

/**
 * @brief The pulse (25.09.2026, hypnosis): a soft kick under the sequence where the kit does not play -- four to the
 *        floor in Melodic and Modern, a heartbeat in Cosmic, a slow beat in Drift, a low tom every bar in Doom.
 */
void writePulse(Score& s, Style style, double from, double to, Rng& rng)
{
    auto hit = [&](double beat, int note, float vel) {
        if (beat >= from && beat < to) s.notes.push_back({ beat, 0.1, Part::Drums, note, vel + 0.05f * rng.uniform(), false, false });
    };
    for (double b = std::ceil(from / kBeatsPerBar) * kBeatsPerBar; b < to; b += kBeatsPerBar) {
        switch (style) {
        case Style::Melodic: case Style::Modern:
            for (int q = 0; q < 4; ++q) hit(b + q, 36, q == 0 ? 0.68f : 0.58f);
            break;
        case Style::Cosmic:
            hit(b, 36, 0.58f); hit(b + 0.5, 36, 0.38f); hit(b + 2.0, 36, 0.52f); hit(b + 2.5, 36, 0.35f);
            break;
        case Style::Drift:
            hit(b, 36, 0.45f); hit(b + 2.0, 36, 0.4f);
            break;
        case Style::Doom:
            hit(b, 45, 0.6f);
            break;
        default: break;
        }
    }
}

} // namespace

const char* const kUnitNames[8] = { "form", "tempo", "rows", "rack", "layers", "lead", "pads", "hands" };

namespace {

/** @brief What the steps of a piece share: the settings, the profile, the form, the score being written. */
struct Piece {
    /** @brief The piece's settings, profile, score and form; the rest is filled in by the steps. */
    Piece(ParamStore& params, const StyleProfile& profile, Style st, Score& score, const PieceForm& f)
        : p(params), prof(profile), style(st), s(score), form(f) {}
    ParamStore& p;                        ///< the piece's own copy of the parameters
    const StyleProfile& prof;             ///< the style profile
    Style style;                          ///< its style
    Score& s;                             ///< the score being written
    const PieceForm& form;                ///< the form (step 1)
    int key = 0;                          ///< pitch class of the key
    int phases = 0;                       ///< sequence phases
    int scale = 0;                        ///< compose.scale order
    int counters = 0;                     ///< counter rows (rows 2 ..)
    std::vector<double> rowFrom;          ///< first beat each row plays, -1 if never (for the hands)
    double firstLead = -1.0;              ///< first beat of a lead section, -1 if none
    bool bleepsOn = false;                ///< the layers' draw for the atmosphere's bleeps
    bool grainsOn = false;                ///< the layers' last draw: the granular cloud
    double stringsFrom = -1.0;            ///< first beat of the string machine, -1 if the piece has none
    std::vector<std::pair<double, int>> chords;   ///< the chord track: degrees of the mode over time (Harmony.h)
    std::vector<std::pair<double, int>> keys = { { 0.0, 0 } };   ///< the phases' keys over time, for the drone
    std::vector<std::pair<double, double>> doubled;   ///< spans where row 2 plays sixteenths (the pulse doubled)
    std::function<uint64_t(Stream)> seedOf;   ///< the seed of a stream, rerolls counted
    /** @brief A fresh generator on stream @p k. */
    Rng stream(Stream k) const { Rng r; r.seed(seedOf(k)); return r; }
};

/** @brief The tempo map (a new tempo starts with the bridge before its phase) and the markers. */
void writeMarkers(Piece& c)
{
    for (const Section& sec : c.form.sections) {
        if (sec.type == SectionType::Bridge) c.s.tempo.add(sec.beat, c.form.phaseBpm[static_cast<size_t>(sec.phase)], false);
        std::string name = sectionName(sec.type);
        if (c.phases > 1 && sec.type != SectionType::Atmo && sec.type != SectionType::Coda) name += " " + std::to_string(sec.phase + 1);
        c.s.markers.push_back({ sec.beat, name });
    }
}

/** @brief Step 2a, the rows: a bass row, the profile's counter rows, a transposer on row 8. */
void setUpRows(Piece& c)
{
    ParamStore& p = c.p;
    Rng cfg = c.stream(sRows);
    const int counters = c.counters = std::clamp(c.prof.peakRows - 1, 0, kRows - 2);
    std::vector<int> lengths = c.prof.counterLengths;
    for (size_t i = lengths.size(); i > 1; --i) std::swap(lengths[i - 1], lengths[static_cast<size_t>(cfg.below(static_cast<int>(i)))]);
    auto rowSet = [&](int r, int index, float v) { p.set(p.id(Module::Row, r, index), v); };
    for (int r = 0; r < kRows; ++r) rowSet(r, row::Active, 0.0f);
    // The bass ostinato: eight or sixteen steps (the style guide's 4.2), its gate long (4.4).
    rowSet(0, row::Length, cfg.uniform() < 0.5f ? 8.0f : 16.0f); rowSet(0, row::Division, static_cast<float>(RowDivision::Sixteenth));
    rowSet(0, row::Gate, 60.0f + 15.0f * cfg.uniform());
    // The bass and the main sequence change at half the style's rate: the ground holds (hypnosis, 25.09.2026).
    rowSet(0, row::Octave, -1); rowSet(0, row::Mutation, 0.5f * c.prof.mutation); rowSet(0, row::Mode, 0);
    for (int k = 0; k < counters; ++k) {
        const int r = k + 1;
        rowSet(r, row::Length, static_cast<float>(lengths[static_cast<size_t>(k) % lengths.size()]));
        rowSet(r, row::Division, static_cast<float>(k % 2 == 0 ? RowDivision::Sixteenth : RowDivision::Eighth));
        rowSet(r, row::Octave, k % 2 == 0 ? 0.0f : 1.0f);
        rowSet(r, row::Mutation, k == 0 ? 0.5f * c.prof.mutation : c.prof.mutation);
        rowSet(r, row::Gate, 40.0f + 15.0f * cfg.uniform());
        rowSet(r, row::Mode, 0);
    }
    const int tr = kRows - 1;
    rowSet(tr, row::Mode, static_cast<float>(RowMode::Transposer));
    rowSet(tr, row::Division, static_cast<float>(c.prof.transposerDivision));
    rowSet(tr, row::Length, static_cast<float>(c.prof.transposerLength));
    rowSet(tr, row::Mutation, 0.0f);   // the chain of transpositions stays as drawn (Harmony.h)
}

/** @brief The cycle of row @p r in beats, as set up. */
double cycleBeats(const ParamStore& p, int r)
{
    return std::clamp(static_cast<int>(p.get(p.id(Module::Row, r, row::Length))), 1, kMaxSteps)
         * rowDivisionBeats(static_cast<RowDivision>(static_cast<int>(p.get(p.id(Module::Row, r, row::Division)))));
}

/**
 * @brief Step 2b, the rack: new patterns in every phase; rows in through the builds, all at the peak, the
 *        counters out in the breakdown, everything out at the bridge or the coda (the bass a third into the coda).
 *        The harmony (Harmony.h, the style guide's 3.x): the chord track on the bass row from the first build to
 *        the breakdown, drawn anew at the peak in the peak's mode; the transposer only on the plateau (the lead's
 *        section) and at the peak; a parallel change of mode at the peak (Aeolian to Dorian) or in the
 *        breakdown (to Phrygian).
 */
void writeRack(Piece& c)
{
    ParamStore& p = c.p;
    Score& s = c.s;
    const PieceForm& form = c.form;
    const int counters = c.counters;
    const int tr = kRows - 1;
    Rack rack;
    rack.setup(p, c.seedOf(sRack));
    std::vector<double>& rowFrom = c.rowFrom;
    rowFrom.assign(kRows, -1.0);
    c.scale = p.getInt(p.id(Module::Compose, 0, compose::Scale));
    // The harmony's own stream, on the rows' unit: a reroll of the rows draws a new harmony as well.
    Rng harm;
    harm.seed(mixSeed(c.seedOf(sRows), 0x68u));
    // Chances of the parallel changes of mode (3.2): brighter at the peak, darker in the breakdown.
    static const float kBrighten[] = { 0.35f, 0.0f, 0.5f, 0.3f, 0.2f }, kDarken[] = { 0.1f, 0.4f, 0.0f, 0.1f, 0.0f };
    const int si = std::clamp(static_cast<int>(c.style), 0, 4);
    // A mode of its own for a later phase, as the parts of a suite have (the style guide's 6.5): another of the
    // minor modes, by the guide's weights (Aeolian 40, Dorian 25, Phrygian 15). It starts at the bridge before
    // the phase, so the phase's rows are drawn in it.
    static const float kPhaseMode[] = { 0.4f, 0.5f, 0.35f, 0.3f, 0.3f };
    std::vector<int> phaseScale(static_cast<size_t>(std::max(1, c.phases)), c.scale);
    for (int ph = 1; ph < c.phases; ++ph) {
        if (c.scale > 2 || harm.uniform() >= kPhaseMode[si]) continue;
        const float w[3] = { c.scale == 0 ? 0.0f : 0.40f, c.scale == 1 ? 0.0f : 0.25f, c.scale == 2 ? 0.0f : 0.15f };
        float u = harm.uniform() * (w[0] + w[1] + w[2]);
        int m = 0;
        while (m < 2 && (u -= w[m]) > 0.0f) ++m;
        phaseScale[static_cast<size_t>(ph)] = m;
    }
    for (int ph = 0; ph < c.phases; ++ph) {
        const Section* entry = form.find(SectionType::Entry, ph);
        if (entry == nullptr) continue;
        // Everything before this phase is played with the old patterns.
        s.sort();
        rack.run(s, entry->beat);
        rack.generate(0, RowRole::Bass);
        for (int k = 0; k < counters; ++k) rack.generate(k + 1, k % 2 == 0 ? RowRole::Counter : RowRole::Walk);
        rack.generate(tr, RowRole::Transposer);
        std::vector<RackEvent> ev;
        if (form.phaseKey[static_cast<size_t>(ph)] != 0 || ph > 0) {
            ev.push_back({ entry->beat, -1, RackOp::Key, form.phaseKey[static_cast<size_t>(ph)] });
            c.keys.push_back({ entry->beat, form.phaseKey[static_cast<size_t>(ph)] });
        }
        ev.push_back({ entry->beat, 0, RackOp::Start, 0 });
        if (rowFrom[0] < 0.0) rowFrom[0] = entry->beat;
        int started = 0;
        for (const Section& sec : form.sections) {
            if (sec.phase != ph) continue;
            if (sec.type == SectionType::Lead) {
                ev.push_back({ sec.beat, tr, RackOp::Start, 0 });
            } else if (sec.type == SectionType::Build) {
                if (started < counters) {
                    ev.push_back({ sec.beat, started + 1, RackOp::Start, 0 });
                    // It comes in with rests and fills up, a step every two bars (the style guide's 6.4).
                    ev.push_back({ sec.beat, started + 1, RackOp::Thin, 6 });
                    for (int f = 1; f <= 6; ++f) ev.push_back({ sec.beat + 8.0 * f, started + 1, RackOp::Fill, 1 });
                    if (rowFrom[static_cast<size_t>(started + 1)] < 0.0) rowFrom[static_cast<size_t>(started + 1)] = sec.beat;
                    ++started;
                }
            } else if (sec.type == SectionType::Peak) {
                if (form.find(SectionType::Lead, ph) == nullptr) ev.push_back({ sec.beat, tr, RackOp::Start, 0 });
                for (; started < counters; ++started) {
                    ev.push_back({ sec.beat, started + 1, RackOp::Start, 0 });
                    if (rowFrom[static_cast<size_t>(started + 1)] < 0.0) rowFrom[static_cast<size_t>(started + 1)] = sec.beat;
                }
            } else if (sec.type == SectionType::Breakdown) {
                for (int k = 0; k < counters; ++k) ev.push_back({ sec.beat, k + 1, RackOp::Stop, 0 });
                ev.push_back({ sec.beat, tr, RackOp::Stop, 0 });
            }
        }
        // The end of the phase: the next bridge, or the coda.
        const Section* bridge = form.find(SectionType::Bridge, ph + 1);
        const Section* coda = form.find(SectionType::Coda, ph);
        const double end = bridge != nullptr ? bridge->beat : (coda != nullptr ? coda->beat : form.lengthBeats);
        // The harmony of the phase.
        const Section* build0 = form.find(SectionType::Build, ph);
        const Section* peak = form.find(SectionType::Peak, ph);
        const Section* breakdown = form.find(SectionType::Breakdown, ph);
        const double chordsEnd = breakdown != nullptr ? breakdown->beat : end;
        const int base = phaseScale[static_cast<size_t>(ph)];
        const int next = ph + 1 < c.phases ? phaseScale[static_cast<size_t>(ph + 1)] : base;
        int peakScale = base;
        if (peak != nullptr && base == 0 && harm.uniform() < kBrighten[si]) {
            peakScale = 1;
            ev.push_back({ peak->beat, -1, RackOp::Scale, 1 });
            ev.push_back({ chordsEnd, -1, RackOp::Scale, base });
        }
        bool darkened = false;
        if (breakdown != nullptr && (base == 0 || base == 1) && harm.uniform() < kDarken[si]) {
            ev.push_back({ breakdown->beat, -1, RackOp::Scale, 2 });
            darkened = true;
        }
        // At the phase's end: back from the breakdown's darkening, or on into the next phase's mode.
        if (darkened || next != base) ev.push_back({ end, -1, RackOp::Scale, next });
        // The sequencing of the phase (the style guide's 4.3): ratchets on the plateau and at the peak, the pulse of
        // an eighths row doubled at the peak, the bass losing steps in the breakdown. Their own draws, after the harmony's.
        static const float kRatchets[] = { 0.6f, 0.3f, 0.8f, 0.7f, 0.2f }, kDouble[] = { 0.5f, 0.3f, 0.6f, 0.5f, 0.0f };
        const Section* plateau = form.find(SectionType::Lead, ph);
        const int lead = counters > 0 ? 1 : 0;   // the main sequence: the first counter row, else the bass
        if (harm.uniform() < kRatchets[si]) {
            if (plateau != nullptr) ev.push_back({ plateau->beat, lead, RackOp::Ratchet, 1 });
            if (peak != nullptr) {
                ev.push_back({ peak->beat, lead, RackOp::Ratchet, 2 });
                ev.push_back({ peak->beat, 0, RackOp::Ratchet, 1 });
            }
            ev.push_back({ chordsEnd, lead, RackOp::Ratchet, 0 });
            ev.push_back({ chordsEnd, 0, RackOp::Ratchet, 0 });
        }
        if (peak != nullptr && counters >= 2 && harm.uniform() < kDouble[si]) {
            // Row 2 runs in eighths (setUpRows): sixteenths from the peak to the breakdown.
            ev.push_back({ peak->beat, 2, RackOp::Division, static_cast<int>(RowDivision::Sixteenth) });
            ev.push_back({ chordsEnd, 2, RackOp::Division, static_cast<int>(RowDivision::Eighth) });
            c.doubled.push_back({ peak->beat, chordsEnd });
        }
        if (breakdown != nullptr)
            for (double b = breakdown->beat + 2 * kBeatsPerBar; b < breakdown->beat + breakdown->length; b += 4 * kBeatsPerBar)
                ev.push_back({ b, 0, RackOp::Thin, 1 });
        auto chordSpan = [&](double b0, double b1, int scale) {
            if (b1 <= b0) return;
            for (const auto& ch : drawChordTrack(c.style, scale, b0, b1, harm)) {
                ev.push_back({ ch.first, 0, RackOp::Chord, bassDegree(scale, ch.second) });
                c.chords.push_back(ch);
            }
        };
        if (build0 != nullptr) {
            chordSpan(build0->beat, peak != nullptr ? std::min(peak->beat, chordsEnd) : chordsEnd, base);
            if (peak != nullptr) chordSpan(peak->beat, chordsEnd, peakScale);
            ev.push_back({ chordsEnd, 0, RackOp::Chord, 0 });
            c.chords.push_back({ chordsEnd, 0 });
        }
        for (int k = 0; k < counters; ++k) ev.push_back({ end, k + 1, RackOp::Stop, 0 });
        ev.push_back({ end, tr, RackOp::Stop, 0 });
        // In the coda the bass row plays on for a third of it, then leaves the atmosphere alone.
        const double bassEnd = bridge != nullptr ? end : end + std::floor((coda != nullptr ? coda->length : 0.0) / 3.0 / kBeatsPerBar) * kBeatsPerBar;
        ev.push_back({ bassEnd, 0, RackOp::Stop, 0 });
        // In the coda the bass loses a step every two bars before it stops (the style guide's 4.7).
        if (bridge == nullptr)
            for (double b = end + 2 * kBeatsPerBar; b < bassEnd; b += 2 * kBeatsPerBar) ev.push_back({ b, 0, RackOp::Thin, 1 });
        s.rack.insert(s.rack.end(), ev.begin(), ev.end());
    }
    s.sort();
    rack.run(s, form.lengthBeats);
    s.rootShifts = rack.shifts();
    s.scaleShifts = rack.scales();
    // The rows' shapes for a display, as the rack read them (Rack::setup).
    for (int r = 0; r < kRows; ++r) {
        const int length = std::clamp(static_cast<int>(p.get(p.id(Module::Row, r, row::Length))), 1, kMaxSteps);
        const double div = rowDivisionBeats(static_cast<RowDivision>(static_cast<int>(p.get(p.id(Module::Row, r, row::Division)))));
        const bool transposer = static_cast<int>(p.get(p.id(Module::Row, r, row::Mode))) == static_cast<int>(RowMode::Transposer);
        s.rowShapes.push_back({ 0.0, r, length, div, transposer });
    }
    // Row 2's doubled pulse, for the displays.
    const int length2 = std::clamp(static_cast<int>(p.get(p.id(Module::Row, 2, row::Length))), 1, kMaxSteps);
    for (const auto& d : c.doubled) {
        s.rowShapes.push_back({ d.first, 2, length2, rowDivisionBeats(RowDivision::Sixteenth), false });
        s.rowShapes.push_back({ d.second, 2, length2, rowDivisionBeats(RowDivision::Eighth), false });
    }
}

/**
 * @brief Step 3, what follows the rack's roots: the drone throughout, the tape keys from the second build
 *        to the end of the peak and in the bridges, the string machine at the peak and in the breakdown,
 *        the lead in its section and at the peak, the drums from the second build -- each with the
 *        profile's chance.
 */
void writeLayers(Piece& c)
{
    Score& s = c.s;
    const PieceForm& form = c.form;
    const StyleProfile& prof = c.prof;
    Rng layers = c.stream(sLayers);
    const bool tapeOn = layers.uniform() < prof.tapeChance;
    const bool stringsOn = layers.uniform() < prof.stringsChance;
    c.bleepsOn = layers.uniform() < prof.bleepChance;
    const bool drumsOn = layers.uniform() < prof.drumsChance;   // drawn last, so the draws above stay as they were
    const Section* coda = form.find(SectionType::Coda, c.phases - 1);
    writeDrone(s, c.key, 0.0, coda != nullptr ? coda->beat + coda->length * 0.8 : form.lengthBeats, &c.keys);

    Rng pads = c.stream(sPads);
    PadPlan pp;
    pp.keyRoot = c.key;
    pp.scale = c.scale;
    pp.shifts = s.rootShifts;
    pp.chords = c.chords;
    pp.scales = s.scaleShifts;
    PadPlan sp = pp;
    sp.part = Part::Strings;
    sp.low = 62; sp.high = 81;
    sp.restrikeSeconds = 1e6;
    sp.choir = false;
    Rng leadRng = c.stream(sLead);
    LeadPlan lp;
    lp.keyRoot = c.key;
    lp.scale = c.scale;
    lp.shifts = s.rootShifts;
    lp.scales = s.scaleShifts;
    for (int ph = 0; ph < c.phases; ++ph) {
        const Section* peak = form.find(SectionType::Peak, ph);
        const Section* breakdown = form.find(SectionType::Breakdown, ph);
        const Section* lead = form.find(SectionType::Lead, ph);
        // The tape keys from the second build (or the only one) to the end of the peak.
        const Section* b1 = nullptr;
        for (const Section& sec : form.sections)
            if (sec.phase == ph && sec.type == SectionType::Build && (b1 == nullptr || sec.index == 1)) b1 = &sec;
        if (tapeOn && b1 != nullptr && peak != nullptr) writeChords(s, pp, b1->beat, peak->beat + peak->length, pads);
        if (stringsOn && peak != nullptr) {
            writeChords(s, sp, peak->beat, breakdown != nullptr ? breakdown->beat + breakdown->length : peak->beat + peak->length, pads);
            if (c.stringsFrom < 0.0) c.stringsFrom = peak->beat;
        }
        if (lead != nullptr) {
            lp.intensity = prof.leadIntensity;
            writeLead(s, lp, lead->beat, lead->beat + lead->length, leadRng);
            if (c.firstLead < 0.0) c.firstLead = lead->beat;
        }
        if (lead != nullptr && peak != nullptr) {
            lp.intensity = std::min(1.0f, prof.leadIntensity + 0.25f);
            writeLead(s, lp, peak->beat, peak->beat + peak->length, leadRng);
        }
        const Section* bridge = form.find(SectionType::Bridge, ph + 1);
        if (tapeOn && bridge != nullptr) writeChords(s, pp, bridge->beat, bridge->beat + bridge->length, pads);
        // The drums come late: from the second build (or the peak) to the end of the peak, and never before 45 % of
        // the piece or after 90 % (the style guide's 5.3: the kick not before the sequence, gone before the end).
        double kitFrom = -1.0;
        if (drumsOn && peak != nullptr) {
            const Section* second = nullptr;
            for (const Section& sec : form.sections) if (sec.phase == ph && sec.type == SectionType::Build && sec.index == 1) second = &sec;
            const double d0 = std::max(second != nullptr ? second->beat : peak->beat,
                                       std::ceil(0.3 * form.lengthBeats / kBeatsPerBar) * kBeatsPerBar);
            const double d1 = std::min(peak->beat + peak->length, std::floor(0.9 * form.lengthBeats / kBeatsPerBar) * kBeatsPerBar);
            if (d1 - d0 >= 8 * kBeatsPerBar) { writeDrums(s, c.style, d0, d1, layers); kitFrom = d0; }
        }
        // The pulse under the sequence from the second build (or the first), not before a fifth of the piece, to the
        // kit or the breakdown; its own stream, so the layers' draws stay as they were.
        {
            static const float kPulse[] = { 0.5f, 0.35f, 0.8f, 0.7f, 0.3f };   // Cosmic, Doom, Melodic, Modern, Drift
            Rng pr;
            pr.seed(mixSeed(c.seedOf(sLayers), 0x7075u + static_cast<uint64_t>(ph)));
            const Section* from = nullptr;
            for (const Section& sec : form.sections)
                if (sec.phase == ph && sec.type == SectionType::Build && (from == nullptr || sec.index == 1)) from = &sec;
            if (from != nullptr && peak != nullptr && pr.uniform() < kPulse[std::clamp(static_cast<int>(c.style), 0, 4)]) {
                const double p0 = std::max(from->beat, std::ceil(0.2 * form.lengthBeats / kBeatsPerBar) * kBeatsPerBar);
                const double p1 = kitFrom >= 0.0 ? kitFrom : (breakdown != nullptr ? breakdown->beat : peak->beat + peak->length);
                if (p1 - p0 >= 8 * kBeatsPerBar) writePulse(s, c.style, p0, std::min(p1, std::floor(0.9 * form.lengthBeats / kBeatsPerBar) * kBeatsPerBar), pr);
            }
        }
    }
    // The end on the open fifth (the style guide's 3.3, Phaedra's close): the strings, or else the tape keys,
    // hold the centre and its fifth over the fading drone of the coda.
    if (coda != nullptr && (stringsOn || tapeOn)) {
        PadPlan fifth = stringsOn ? sp : pp;
        fifth.openFifth = true;
        writeChords(s, fifth, coda->beat + coda->length * 0.15, coda->beat + coda->length * 0.85, pads);
    }
    // The granular cloud: the layers' last draw, so every draw above stays as it was.
    c.grainsOn = layers.uniform() < prof.grainChance;
}

/** @brief Step 4, the atmosphere: wind and sweeps where no rows play, bleeps in the builds. */
void writeAtmosphere(Piece& c)
{
    Score& s = c.s;
    const int wind = c.p.id(Module::Atmos, 0, atmos::Wind), sweeps = c.p.id(Module::Atmos, 0, atmos::Sweeps);
    const int bleeps = c.p.id(Module::Atmos, 0, atmos::Bleeps);
    const int grains = c.p.id(Module::Atmos, 0, atmos::Grains);
    using G = GestureShape;
    float windAt = 0.0f;
    auto windTo = [&](double b0, double b1, float v) {
        if (b1 <= b0) return;
        s.gestures.push_back({ wind, b0, b1 - b0, windAt, v, G::MinimumJerk, 1 });
        windAt = v;
    };
    // The granular cloud where the piece has it: up in the spaces without rows, away when they come in.
    float grainsAt = 0.0f;
    auto grainsTo = [&](double b0, double b1, float v) {
        if (!c.grainsOn || b1 <= b0 || v == grainsAt) return;
        s.gestures.push_back({ grains, b0, b1 - b0, grainsAt, v, G::MinimumJerk, 3 });   // the composer's own hand
        grainsAt = v;
    };
    for (const Section& sec : c.form.sections) {
        switch (sec.type) {
        case SectionType::Atmo:
            grainsTo(sec.beat + sec.length * 0.2, sec.beat + sec.length * 0.8, 0.7f);
            windTo(sec.beat, sec.beat + sec.length * 0.7, 0.6f);
            s.gestures.push_back({ sweeps, sec.beat, 0.0, 0.0f, 0.3f, G::Step, 1 });
            break;
        case SectionType::Entry:
            grainsTo(sec.beat, sec.beat + sec.length * 0.5, 0.0f);
            windTo(sec.beat, sec.beat + sec.length, 0.3f);
            s.gestures.push_back({ sweeps, sec.beat, 0.0, 0.3f, 0.0f, G::Step, 1 });
            break;
        case SectionType::Build:
            if (sec.index == 0) windTo(sec.beat, sec.beat + sec.length, 0.12f);
            if (sec.index == 0 && c.bleepsOn) s.gestures.push_back({ bleeps, sec.beat, 0.0, 0.0f, 0.2f, G::Step, 1 });
            break;
        case SectionType::Peak:
            if (c.bleepsOn) s.gestures.push_back({ bleeps, sec.beat, 0.0, 0.2f, 0.0f, G::Step, 1 });
            break;
        case SectionType::Bridge:
            grainsTo(sec.beat, sec.beat + sec.length * 0.5, 0.6f);
            windTo(sec.beat, sec.beat + sec.length * 0.6, 0.5f);
            s.gestures.push_back({ sweeps, sec.beat, 0.0, 0.0f, 0.3f, G::Step, 1 });
            break;
        case SectionType::Coda:
            grainsTo(sec.beat + sec.length * 0.1, sec.beat + sec.length * 0.6, 0.7f);
            windTo(sec.beat, sec.beat + sec.length * 0.6, 0.6f);
            s.gestures.push_back({ sweeps, sec.beat, 0.0, 0.0f, 0.3f, G::Step, 1 });
            break;
        default: break;
        }
    }
}

/** @brief Step 5, the settings of the piece as steps at its start: tape set, hall, loudness. */
void writeSettings(Piece& c)
{
    ParamStore& p = c.p;
    using G = GestureShape;
    const int tapeSet = p.id(Module::Tape, 0, tape::Set);
    c.s.gestures.push_back({ tapeSet, 0.0, 0.0, 0.0f, offsetTo(p, tapeSet, static_cast<float>(c.prof.tape)), G::Step, 1 });
    const int hall = p.id(Module::Reverb, 0, reverb::Decay);
    c.s.gestures.push_back({ hall, 0.0, 0.0, 0.0f, offsetTo(p, hall, c.prof.hallSeconds), G::Step, 1 });
    const int level = p.id(Module::Master, 0, master::Level);
    c.s.gestures.push_back({ level, 0.0, 0.0, 0.0f, offsetTo(p, level, p.get(level) + c.prof.levelDb), G::Step, 1 });
    // The rooms of the style guide's 7.3: the bass dry, the pads and strings in the hall without echo.
    for (int id : { p.id(Module::Row, 0, row::EchoSend), p.id(Module::Strings, 0, strings::EchoSend), p.id(Module::Tape, 0, tape::EchoSend) })
        c.s.gestures.push_back({ id, 0.0, 0.0, 0.0f, offsetTo(p, id, 0.0f), G::Step, 1 });
    const int bassHall = p.id(Module::Row, 0, row::ReverbSend);
    c.s.gestures.push_back({ bassHall, 0.0, 0.0, 0.0f, offsetTo(p, bassHall, std::min(p.get(bassHall), 0.1f)), G::Step, 1 });
    // The stereo field (7.3): the bass in the middle, the main sequence near it, the other counter rows 30 to 50 % out
    // to alternating sides, the lead a little off the middle, the drone wandering slowly across. Drawn on the rows' unit.
    Rng pans;
    pans.seed(mixSeed(c.seedOf(sRows), 0x70u));
    auto setTo = [&](int id, float v) { c.s.gestures.push_back({ id, 0.0, 0.0, 0.0f, offsetTo(p, id, v), G::Step, 1 }); };
    setTo(p.id(Module::Row, 0, row::Pan), 0.0f);
    const float side = pans.uniform() < 0.5f ? -1.0f : 1.0f;
    for (int k = 0; k < c.counters; ++k) {
        const float amount = k == 0 ? 0.1f * pans.uniform() : 0.3f + 0.2f * pans.uniform();
        setTo(p.id(Module::Row, k + 1, row::Pan), (k % 2 == 0 ? side : -side) * amount);
    }
    setTo(p.id(Module::Lead, 0, lead::Pan), -side * (0.1f + 0.1f * pans.uniform()));
    setTo(p.id(Module::Drone, 0, lead::Pan), 0.0f);   // (its auto pan, drone.auto_pan, lets it wander)
    // Each layer its own delay (4.4, 4.5): the main sequence on the tape echo's dotted eighths, the other counter
    // rows on the second echo in eighths or quarter triplets, with less of the first.
    const int e2time = p.id(Module::Echo2, 0, echo2::Time);
    const float e2 = static_cast<float>(pans.uniform() < 0.6f ? EchoTime::Eighth : EchoTime::QuarterT);
    c.s.gestures.push_back({ e2time, 0.0, 0.0, 0.0f, offsetTo(p, e2time, e2), G::Step, 1 });
    for (int k = 1; k < c.counters; ++k) {
        setTo(p.id(Module::Row, k + 1, row::Echo2Send), 0.35f);
        setTo(p.id(Module::Row, k + 1, row::EchoSend), 0.1f);
    }
    // The frequency architecture of the production guide (4.1, 4.2): the bass row alone down to 30 Hz, the main
    // sequence from 90 Hz, the other counter rows from 200 Hz (the drone, the lead, the pads have their own defaults).
    setTo(p.id(Module::Row, 0, row::LowCut), 30.0f);
    for (int k = 0; k < c.counters; ++k) setTo(p.id(Module::Row, k + 1, row::LowCut), k == 0 ? 90.0f : 200.0f);
    // The hall's pre-delay a 64th at the piece's tempo (5.3: 32 ms at 118 BPM), which keeps the dry sounds in front.
    const int preDelay = p.id(Module::Reverb, 0, reverb::PreDelay);
    setTo(preDelay, static_cast<float>(60000.0 / (c.form.phaseBpm.front() * 16.0)));

    // Along the form, by the composer's own hand (3), apart from the player's two: a slow glide at the start of each
    // section towards the section's value (at most eight bars).
    auto alongForm = [&](int id, float start, const std::function<float(SectionType)>& value) {
        float at = offsetTo(p, id, start);
        for (const Section& sec : c.form.sections) {
            const float to = offsetTo(p, id, value(sec.type));
            if (to == at) continue;
            c.s.gestures.push_back({ id, sec.beat, std::min(sec.length, 8.0 * kBeatsPerBar), at, to, G::MinimumJerk, 3 });
            at = to;
        }
    };
    // The hall grows in the spaces and shrinks at the peak by 40 % (the style guide's 7.3, the production guide's 5.6).
    alongForm(hall, c.prof.hallSeconds, [&](SectionType t) {
        switch (t) {
        case SectionType::Atmo: case SectionType::Bridge: return c.prof.hallSeconds * 1.3f;
        case SectionType::Coda: return c.prof.hallSeconds * 1.4f;
        case SectionType::Peak: return c.prof.hallSeconds * 0.6f;
        case SectionType::Breakdown: return c.prof.hallSeconds * 1.1f;
        default: return c.prof.hallSeconds;
        }
    });
    // Automation instead of compression (7.2): the level follows the form, quieter spaces, the peak the loudest.
    const float base = p.get(level) + c.prof.levelDb;
    alongForm(level, base, [&](SectionType t) {
        switch (t) {
        case SectionType::Atmo: case SectionType::Bridge: case SectionType::Coda: return base - 3.0f;
        case SectionType::Entry: case SectionType::Breakdown: return base - 1.5f;
        case SectionType::Build: return base - 1.0f;
        case SectionType::Lead: return base - 0.5f;
        default: return base;
        }
    });
    // The width over the form (6.6): the widest in the spaces, narrow where the dry bass comes in, growing again.
    const int width = p.id(Module::Master, 0, master::Width);
    const float w0 = p.get(width);
    alongForm(width, w0, [&](SectionType t) {
        switch (t) {
        case SectionType::Atmo: case SectionType::Bridge: case SectionType::Coda: return w0 + 2.0f;
        case SectionType::Entry: return w0 - 1.5f;
        case SectionType::Build: return w0 - 0.5f;
        case SectionType::Peak: return w0 + 0.5f;
        case SectionType::Breakdown: return w0 + 1.0f;
        default: return w0;
        }
    });
    // The approach (the addon's 2): the bass comes in from afar over 45 s at each entry and recedes in the breakdown
    // over up to two minutes, and in the coda before it stops -- level, highs and hall together (row.distance).
    const int dist = p.id(Module::Row, 0, row::Distance);
    const float far = offsetTo(p, dist, 0.7f), near = offsetTo(p, dist, 0.0f);
    for (int ph = 0; ph < c.phases; ++ph) {
        const double bpm = c.form.phaseBpm[static_cast<size_t>(ph)];
        if (const Section* entry = c.form.find(SectionType::Entry, ph))
            c.s.gestures.push_back({ dist, entry->beat, std::min(entry->length, 45.0 * bpm / 60.0), far, near, G::MinimumJerk, 3 });
        if (const Section* bd = c.form.find(SectionType::Breakdown, ph))
            c.s.gestures.push_back({ dist, bd->beat, std::min(bd->length, 120.0 * bpm / 60.0), near, far, G::MinimumJerk, 3 });
    }
    if (const Section* coda = c.form.find(SectionType::Coda, c.phases - 1)) {
        const double third = std::floor(coda->length / 3.0 / kBeatsPerBar) * kBeatsPerBar;
        if (third > 0.0) c.s.gestures.push_back({ dist, coda->beat, third, near, far, G::MinimumJerk, 3 });
    }
    // The rooms per layer (the addon's 4, the production guide's 5.2): the second layer -- the counter rows, the lead,
    // the drums -- in the blend room with a little hall, the main sequence near it with less, the bass dry; the pads,
    // the drone and the atmosphere stay in the hall. A direct sound gets one room fully, never both.
    for (int k = 0; k < c.counters; ++k) {
        setTo(p.id(Module::Row, k + 1, row::BlendSend), k == 0 ? 0.15f : 0.3f);
        setTo(p.id(Module::Row, k + 1, row::ReverbSend), 0.1f);
    }
    setTo(p.id(Module::Lead, 0, lead::BlendSend), 0.35f);
    setTo(p.id(Module::Lead, 0, lead::ReverbSend), 0.25f);
    setTo(p.id(Module::Drums, 0, drums::BlendSend), 0.25f);
    setTo(p.id(Module::Drums, 0, drums::ReverbSend), 0.05f);
    // Send A, the early reflections: distance and glue without a tail -- the second layer, a little of the main sequence.
    for (int k = 0; k < c.counters; ++k) setTo(p.id(Module::Row, k + 1, row::EarlySend), k == 0 ? 0.1f : 0.25f);
    setTo(p.id(Module::Lead, 0, lead::EarlySend), 0.2f);
    setTo(p.id(Module::Drums, 0, drums::EarlySend), 0.3f);
    // Send D, the effect hall: the drone, the atmosphere and the tape keys in the spaces and the transitions (the
    // atmosphere, the bridges, the coda), nothing while the machine runs.
    auto spaces = [](SectionType t) { return t == SectionType::Atmo || t == SectionType::Bridge || t == SectionType::Coda; };
    const std::pair<int, float> shimmerSends[3] = { { p.id(Module::Drone, 0, lead::ShimmerSend), 0.2f },
                                                     { p.id(Module::Atmos, 0, atmos::ShimmerSend), 0.35f },
                                                     { p.id(Module::Tape, 0, tape::ShimmerSend), 0.15f } };
    for (const auto& [id, amount] : shimmerSends) {
        const float at = p.get(id), wet = amount;
        alongForm(id, at, [&, at, wet](SectionType t) { return spaces(t) ? std::min(1.0f, at + wet) : at; });
    }
    // The sound of the sequences (25.09.2026, hypnosis): the main sequence squelchy -- more resonance, a deep and short
    // filter envelope, strong accents -- the bass round, the counter rows between; and their filter sweeps, slow sines
    // of their own period (the main sequence the widest), the Berlin School's long openings and closings.
    auto voiceTo = [&](int r, int index, float v) { setTo(p.id(Module::Voice, r, index), v); };
    voiceTo(0, voice::Resonance, 0.3f); voiceTo(0, voice::EnvAmount, 2.5f); voiceTo(0, voice::Decay, 260.0f); voiceTo(0, voice::Cutoff, 320.0f);
    voiceTo(0, voice::Accent, 0.6f);
    setTo(p.id(Module::Row, 0, row::Sweep), 0.3f);
    for (int k = 0; k < c.counters; ++k) {
        const int r = k + 1;
        voiceTo(r, voice::Resonance, k == 0 ? 0.52f : 0.42f);
        voiceTo(r, voice::EnvAmount, k == 0 ? 3.6f : 3.0f);
        voiceTo(r, voice::Decay, k == 0 ? 190.0f : 150.0f);
        voiceTo(r, voice::Cutoff, k == 0 ? 450.0f : 700.0f);
        voiceTo(r, voice::Accent, k == 0 ? 0.75f : 0.55f);
        setTo(p.id(Module::Row, r, row::Sweep), k == 0 ? 0.9f : 0.6f);
    }
    // The main sequence's attacks lifted, so it stays in front (the production guide's 7.5).
    if (c.counters > 0) setTo(p.id(Module::Row, 1, row::Punch), 0.5f);
    // The serial feed into the hall down to 5 % at the peak, where energy would pile up in the far room (the addon's 4).
    const int into = p.id(Module::Blend, 0, blend::IntoHall);
    const float into0 = p.get(into);
    alongForm(into, into0, [&](SectionType t) { return t == SectionType::Peak ? 0.05f : into0; });
    // The foundation in pure intervals (the addon's 3): the bass row's two oscillators without detune.
    setTo(p.id(Module::Voice, 0, voice::Detune), 0.0f);
    // The pads step back 3 dB while the lead plays (7.2): its section and the peak after it.
    for (int ph = 0; ph < c.phases; ++ph) {
        const Section* lead = c.form.find(SectionType::Lead, ph);
        if (lead == nullptr) continue;
        const Section* peak = c.form.find(SectionType::Peak, ph);
        const double from = lead->beat, to = peak != nullptr ? peak->beat + peak->length : lead->beat + lead->length;
        for (int id : { p.id(Module::Tape, 0, tape::Level), p.id(Module::Strings, 0, strings::Level) }) {
            const float at = offsetTo(p, id, p.get(id)), down = offsetTo(p, id, p.get(id) - 3.0f);
            c.s.gestures.push_back({ id, from, 2.0 * kBeatsPerBar, at, down, G::MinimumJerk, 3 });
            c.s.gestures.push_back({ id, std::max(from + 2.0 * kBeatsPerBar, to - 2.0 * kBeatsPerBar), 2.0 * kBeatsPerBar, down, at, G::MinimumJerk, 3 });
        }
    }
}

/**
 * @brief The events of the addon's 7: in the stages without a sequence -- the atmosphere, the bridges, the coda --
 *        now and then a near event, a lead fragment of one to three notes (root, fifth, third, octave of the scale
 *        at its beat), 20 to 90 s apart. It sets the ear's sense of depth anew; once the sequence runs, it is the
 *        reference. Their own stream, on the lead's unit.
 */
void writeEvents(Piece& c)
{
    Rng ev;
    ev.seed(mixSeed(c.seedOf(sLead), 0x65u));
    for (const Section& sec : c.form.sections) {
        if (sec.type != SectionType::Atmo && sec.type != SectionType::Bridge && sec.type != SectionType::Coda) continue;
        const double bpm = c.s.tempo.bpmAt(sec.beat), secs = bpm / 60.0;
        const double to = sec.beat + (sec.type == SectionType::Coda ? 0.7 : 0.9) * sec.length;
        for (double t = sec.beat + 0.1 * sec.length + (20.0 + 30.0 * ev.uniform()) * secs; t < to - 4.0; t += (20.0 + 70.0 * ev.uniform()) * secs) {
            const int root = pitchClass(c.key + c.s.rootAt(t));
            const int scale = c.s.scaleAt(t, c.scale);
            const int notes = 1 + ev.below(3);
            double b = std::floor(t);
            for (int k = 0; k < notes && b < to; ++k) {
                static const int kDegrees[4] = { 0, 4, 2, 7 };
                int pitch = 60 + root + scaleSemitones(scale, kDegrees[ev.below(4)]);
                while (pitch < 64) pitch += 12;
                while (pitch > 84) pitch -= 12;
                const double len = 1.5 + 1.5 * ev.uniform();
                c.s.notes.push_back({ b, len, Part::Lead, pitch, 0.6f + 0.12f * ev.uniform(), false, false });
                b += len + 0.5;
            }
        }
    }
}

/** @brief Step 6, the hands on the knobs of what plays, following the form's energy. */
void writeHands(Piece& c)
{
    ParamStore& p = c.p;
    const StyleProfile& prof = c.prof;
    const std::vector<double>& rowFrom = c.rowFrom;
    auto knob = [&](Module m, int inst, int index, float low, float high, float rest, float peak, float weight, double from) {
        HandKnob k;
        k.param = p.id(m, inst, index);
        k.low = low; k.high = high;
        k.atRest = rest + prof.darkness; k.atPeak = peak + prof.darkness;
        k.weight = weight; k.from = std::max(0.0, from);
        return k;
    };
    std::vector<HandKnob> knobs;
    knobs.push_back(knob(Module::Voice, 0, voice::Cutoff, -0.35f, 0.35f, -0.22f, 0.22f, 3.0f, rowFrom[0]));
    knobs.push_back(knob(Module::Voice, 0, voice::Decay, -0.10f, 0.30f, 0.0f, 0.18f, 1.0f, rowFrom[0]));
    knobs.push_back(knob(Module::Voice, 0, voice::Resonance, -0.10f, 0.30f, 0.0f, 0.2f, 1.0f, rowFrom[0]));
    for (int k = 0; k < c.counters; ++k)
        if (rowFrom[static_cast<size_t>(k + 1)] >= 0.0)
            knobs.push_back(knob(Module::Voice, k + 1, voice::Cutoff, -0.40f, 0.30f, -0.28f, 0.15f, 1.5f, rowFrom[static_cast<size_t>(k + 1)]));
    if (c.firstLead >= 0.0) knobs.push_back(knob(Module::Lead, 0, lead::Cutoff, -0.30f, 0.30f, -0.10f, 0.20f, 1.0f, c.firstLead));
    knobs.push_back(knob(Module::Drone, 0, drone::Cutoff, -0.20f, 0.30f, -0.10f, 0.15f, 0.8f, 0.0));
    knobs.push_back(knob(Module::Atmos, 0, atmos::WindTone, -0.30f, 0.30f, 0.0f, 0.10f, 0.5f, 0.0));
    // The string machine's registration, a slow hand through its mixes (StringMachine.h), where it plays.
    if (c.stringsFrom >= 0.0) knobs.push_back(knob(Module::Strings, 0, strings::Registration, 0.0f, 0.45f, 0.05f, 0.3f, 0.6f, c.stringsFrom));
    // The echo's feedback, thrown and caught: its home high where the energy is low and low at the peak (the style
    // guide's 6.3, U-shaped over the piece).
    HandKnob throwKnob = knob(Module::Echo, 0, echo::Feedback, -0.15f, 0.4f, 0.0f, 0.05f, 0.6f, rowFrom[0]);
    throwKnob.atRest = 0.12f; throwKnob.atPeak = -0.08f;
    throwKnob.throws = true;
    throwKnob.scatter = 0.15f;
    knobs.push_back(throwKnob);
    Rng hands = c.stream(sHands);
    const PieceForm& form = c.form;
    playHands(c.s, knobs, prof.hands, [&](double b) { return form.energy(b); }, 0.0, form.lengthBeats, hands);
}

} // namespace

Score composePiece(const ParamStore& params, uint64_t seed, double minutes, int keyShift,
                   const Curation* curation, const std::string& unit, const StyleProfile* profile)
{
    ParamStore p;
    p.copyValuesFrom(params);
    const Style style = static_cast<Style>(p.getInt(p.id(Module::Compose, 0, compose::Style)));
    // The profile: the one handed in (a concert's), else the user's own style where it is on, else compose.style's.
    StyleProfile own;
    if (profile == nullptr && customStyleOn(p)) { own = customProfile(p, styleProfile(style)); profile = &own; }
    const StyleProfile& prof = profile != nullptr ? *profile : styleProfile(style);
    // A unit's stream moves by its reroll counter; every other stream stays where it was.
    auto seedOf = [seed, curation, unit](Stream k) {
        const int n = curation != nullptr ? curation->count(unit + kUnitNames[k - 1]) : 0;
        return mixSeed(seed, static_cast<uint64_t>(k) + 131u * static_cast<uint64_t>(n));
    };
    auto stream = [&](Stream k) { Rng r; r.seed(seedOf(k)); return r; };

    // Step 1: key, tempo, form.
    const int keyId = p.id(Module::Compose, 0, compose::Key);
    const int key = pitchClass(p.getInt(keyId) + keyShift);
    p.set(keyId, static_cast<float>(key));
    double bpm = p.get(p.id(Module::Compose, 0, compose::Bpm));
    if (p.getBool(p.id(Module::Compose, 0, compose::StyleTempo))) {
        Rng t = stream(sTempo);
        bpm = std::round(prof.bpmLow + (prof.bpmHigh - prof.bpmLow) * t.uniform());
    }
    Rng formRng = stream(sForm);
    PieceForm form = drawForm(prof, minutes, bpm, formRng);

    Score s;
    s.clear(form.phaseBpm[0]);
    s.seed = seed;
    s.keyRoot = key;
    s.lengthBeats = form.lengthBeats;
    Piece c{ p, prof, style, s, form };
    c.key = key;
    c.phases = static_cast<int>(form.phaseBpm.size());
    c.seedOf = seedOf;
    setUpRows(c);         // step 2a
    // The builds end where the row they bring in meets the bass row again (Form.h).
    std::vector<double> cycles;
    for (int k = 0; k < c.counters; ++k) cycles.push_back(cycleBeats(p, k + 1));
    snapToConjunctions(form, cycleBeats(p, 0), cycles);
    writeMarkers(c);
    writeRack(c);         // step 2b
    writeLayers(c);       // step 3
    writeAtmosphere(c);   // step 4
    writeEvents(c);       // (the addon's events in the stages without a sequence)
    writeSettings(c);     // step 5
    writeHands(c);        // step 6
    s.sort();
    return s;
}

Score composeInterlude(const ParamStore& params, uint64_t seed, double minutes, int keyShift, const StyleProfile& profile)
{
    ParamStore p;
    p.copyValuesFrom(params);
    const Style style = static_cast<Style>(p.getInt(p.id(Module::Compose, 0, compose::Style)));
    const int keyId = p.id(Module::Compose, 0, compose::Key);
    const int key = pitchClass(p.getInt(keyId) + keyShift);
    p.set(keyId, static_cast<float>(key));
    const int scale = p.getInt(p.id(Module::Compose, 0, compose::Scale));
    StyleProfile prof = profile;
    prof.hallSeconds *= 1.4f;
    const double bpm = prof.bpmLow;
    // The form: the atmosphere, then a coda of a fifth of it; whole bars.
    const double bars = std::max(8.0, std::round(minutes * bpm / kBeatsPerBar));
    const double codaBars = std::max(4.0, std::round(bars * 0.2));
    PieceForm form;
    form.sections.push_back({ SectionType::Atmo, 0, 0, 0.0, (bars - codaBars) * kBeatsPerBar, 0.1f, 0.25f });
    form.sections.push_back({ SectionType::Coda, 0, 0, (bars - codaBars) * kBeatsPerBar, codaBars * kBeatsPerBar, 0.2f, 0.0f });
    form.phaseBpm = { bpm };
    form.phaseKey = { 0 };
    form.lengthBeats = bars * kBeatsPerBar;

    Score s;
    s.clear(bpm);
    s.seed = seed;
    s.keyRoot = key;
    s.lengthBeats = form.lengthBeats;
    Piece c{ p, prof, style, s, form };
    c.key = key;
    c.phases = 1;
    c.scale = scale;
    c.rowFrom.assign(kRows, -1.0);
    c.seedOf = [seed](Stream k) { return mixSeed(seed, static_cast<uint64_t>(k)); };
    writeMarkers(c);
    s.markers.front().text = "Zwischenspiel";
    s.rootShifts = { { 0.0, 0 } };
    s.scaleShifts = { { 0.0, scale } };
    const Section& atmo = form.sections[0];
    const Section& coda = form.sections[1];
    writeDrone(s, key, 0.0, coda.beat + coda.length * 0.9, &c.keys);
    // Slow chords, a chord every 16 to 32 bars (Drift's rhythm), on the string machine or the tape keys' choir.
    Rng pads = c.stream(sPads);
    PadPlan pp;
    pp.keyRoot = key;
    pp.scale = scale;
    pp.shifts = s.rootShifts;
    pp.scales = s.scaleShifts;
    if (pads.uniform() < 0.5f) {
        pp.part = Part::Strings;
        pp.low = 62; pp.high = 81;
        pp.restrikeSeconds = 1e6;
        pp.choir = false;
    }
    const double from = 4.0 * kBeatsPerBar;
    pp.chords = drawChordTrack(Style::Drift, scale, from, atmo.beat + atmo.length, pads);
    pp.chords.push_back({ atmo.beat + atmo.length, 0 });
    writeChords(s, pp, from, atmo.beat + atmo.length, pads);
    if (pp.part == Part::Strings) c.stringsFrom = from;
    PadPlan fifth = pp;
    fifth.openFifth = true;
    writeChords(s, fifth, coda.beat, coda.beat + coda.length * 0.85, pads);
    c.grainsOn = true;
    writeAtmosphere(c);
    writeEvents(c);
    writeSettings(c);
    writeHands(c);
    s.sort();
    return s;
}

void appendScore(Score& dst, const Score& src, int rootOffset)
{
    const double off = dst.lengthBeats;
    for (const TempoPoint& tp : src.tempo.points()) dst.tempo.add(off + tp.beat, tp.bpm, tp.rampToNext);
    for (NoteEvent n : src.notes) { n.beat += off; dst.notes.push_back(n); }
    for (Gesture g : src.gestures) { g.beat += off; dst.gestures.push_back(g); }
    for (RackEvent e : src.rack) { e.beat += off; dst.rack.push_back(e); }
    for (Marker m : src.markers) { m.beat += off; dst.markers.push_back(m); }
    for (const auto& r : src.rootShifts) dst.rootShifts.push_back({ off + r.first, r.second + rootOffset });
    for (const auto& r : src.scaleShifts) dst.scaleShifts.push_back({ off + r.first, r.second });
    for (RowShape r : src.rowShapes) { r.from += off; dst.rowShapes.push_back(r); }
    dst.lengthBeats = off + src.lengthBeats;
    dst.sort();
}

Score composeConcert(const ParamStore& p, uint64_t seed, double minutes, const Curation* curation)
{
    const int styleId = p.id(Module::Compose, 0, compose::Style);
    const Style from = static_cast<Style>(p.getInt(styleId));
    const int morph = p.getInt(p.id(Module::Compose, 0, compose::MorphTo)) - 1;
    const bool morphing = morph >= 0 && morph < static_cast<int>(Style::Count) && morph != static_cast<int>(from);
    const float arc = p.get(p.id(Module::Compose, 0, compose::ConcertArc));
    const bool shaped = morphing || arc > 0.0f;
    ParamStore q;   // the concert's own copy: a morphing concert changes compose.style from piece to piece
    q.copyValuesFrom(p);
    // The user's own style, where it is on, is where the concert starts (and what it morphs from).
    const StyleProfile start = customStyleOn(p) ? customProfile(p, styleProfile(from)) : styleProfile(from);
    StyleProfile prof = start;
    Rng r;
    r.seed(mixSeed(seed, 99u + 131u * static_cast<uint64_t>(curation != nullptr ? curation->count("concert") : 0)));
    Score out;
    double elapsed = 0.0;
    int shift = 0;
    // The album (the style guide's 6.5): its draws come only where it is on, so a concert without it stays as it was.
    const bool album = p.getBool(p.id(Module::Compose, 0, compose::Album));
    const int scaleId = p.id(Module::Compose, 0, compose::Scale);
    const int baseScale = p.getInt(scaleId);
    bool middleDone = false;
    int interludes = 0;
    for (int i = 0; elapsed < minutes * 60.0 - 90.0 && i < 64; ++i) {
        if (shaped) {
            // The profile at the share of the concert already played: between the two styles (the nearer one's
            // drums), then along the arc of tension.
            const float t = static_cast<float>(elapsed / (minutes * 60.0));
            prof = morphing ? morphProfile(start, styleProfile(static_cast<Style>(morph)), t) : start;
            if (morphing) q.set(styleId, static_cast<float>(t < 0.5f ? static_cast<int>(from) : morph));
            prof = arcProfile(prof, concertArc(t) - 0.5f, arc);
        }
        const double left = minutes - elapsed / 60.0;
        double m = prof.minutesLow + (prof.minutesHigh - prof.minutesLow) * r.uniform();
        if (left - m < prof.minutesLow * 0.6) m = left;   // no short piece at the end: the last takes the rest
        m = std::max(4.0, m);
        const bool last = m >= left - 1e-9;
        StyleProfile own = prof;
        if (album) {
            // The darkest piece where the concert's middle falls, an ethereal one at its end.
            q.set(scaleId, static_cast<float>(baseScale));
            const double total = minutes * 60.0;
            if (!middleDone && !last && elapsed + m * 60.0 >= 0.5 * total) {
                middleDone = true;
                if (baseScale <= 1) q.set(scaleId, 2.0f);
                own.darkness -= 0.12f;
                own.peakRows = std::min(kRows - 1, own.peakRows + 1);
            } else if (last && i > 0) {
                if (baseScale == 0 || baseScale == 2) q.set(scaleId, 1.0f);
                own.hallSeconds *= 1.3f;
                own.peakRows = std::max(1, own.peakRows - 1);
                own.drumsChance = 0.0f;
                own.leadIntensity *= 0.7f;
                own.darkness += 0.05f;
            }
        }
        Score piece = composePiece(q, mixSeed(seed, 1000 + static_cast<uint64_t>(i)), m, shift, curation,
                                   "piece" + std::to_string(i + 1) + ".", album ? &own : (shaped ? &prof : nullptr));
        for (Marker& mk : piece.markers) mk.text = "Stueck " + std::to_string(i + 1) + ": " + mk.text;
        if (i == 0) { out = piece; out.rootShifts.clear(); out.scaleShifts.clear(); out.lengthBeats = 0.0; out.notes.clear(); out.gestures.clear();
                      out.rack.clear(); out.markers.clear(); out.rowShapes.clear(); }
        const double before = out.tempo.secondsAt(out.lengthBeats);
        appendScore(out, piece, shift);
        elapsed += out.tempo.secondsAt(out.lengthBeats) - before;
        // An interlude of three to five minutes between two long pieces, in the key of the one before.
        if (album && !last) {
            const double im = 3.0 + 2.0 * r.uniform();
            if (minutes * 60.0 - elapsed - im * 60.0 > prof.minutesLow * 60.0 * 0.8) {
                q.set(scaleId, static_cast<float>(baseScale));
                Score inter = composeInterlude(q, mixSeed(seed, 5000 + static_cast<uint64_t>(i)), im, shift, prof);
                ++interludes;
                for (Marker& mk : inter.markers)
                    mk.text = "Zwischenspiel " + std::to_string(interludes) + (mk.text == "Zwischenspiel" ? std::string() : ": " + mk.text);
                const double b0 = out.tempo.secondsAt(out.lengthBeats);
                appendScore(out, inter, shift);
                elapsed += out.tempo.secondsAt(out.lengthBeats) - b0;
            }
        }
        // The next key: a fourth, a fifth, the relative, a tone.
        const int moves[5] = { 5, -5, 3, -2, 2 };
        shift += moves[r.below(5)];
        shift = pitchClass(shift + 6) - 6;
    }
    return out;
}

} // namespace eph
