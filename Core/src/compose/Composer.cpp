/**
 * @file Composer.cpp
 * @brief A piece and a concert.
 */
#include "eph/compose/Composer.h"
#include "eph/compose/Form.h"
#include "eph/compose/GestureEngine.h"
#include "eph/compose/Lead.h"
#include "eph/compose/Pads.h"
#include "eph/Rack.h"
#include "eph/compose/Style.h"
#include <algorithm>
#include <cmath>
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

} // namespace

const char* const kUnitNames[8] = { "form", "tempo", "rows", "rack", "layers", "lead", "pads", "hands" };

Score composePiece(const ParamStore& params, uint64_t seed, double minutes, int keyShift,
                   const Curation* curation, const std::string& unit)
{
    ParamStore p;
    p.copyValuesFrom(params);
    const Style style = static_cast<Style>(p.getInt(p.id(Module::Compose, 0, compose::Style)));
    const StyleProfile& prof = styleProfile(style);
    // A unit's stream moves by its reroll counter; every other stream stays where it was.
    auto streamSeed = [&](Stream k) {
        const int n = curation != nullptr ? curation->count(unit + kUnitNames[k - 1]) : 0;
        return mixSeed(seed, static_cast<uint64_t>(k) + 131u * static_cast<uint64_t>(n));
    };
    auto stream = [&](Stream k) { Rng r; r.seed(streamSeed(k)); return r; };

    const int keyId = p.id(Module::Compose, 0, compose::Key);
    const int key = pitchClass(p.getInt(keyId) + keyShift);
    p.set(keyId, static_cast<float>(key));
    double bpm = p.get(p.id(Module::Compose, 0, compose::Bpm));
    if (p.getBool(p.id(Module::Compose, 0, compose::StyleTempo))) {
        Rng t = stream(sTempo);
        bpm = std::round(prof.bpmLow + (prof.bpmHigh - prof.bpmLow) * t.uniform());
    }
    Rng formRng = stream(sForm);
    const PieceForm form = drawForm(prof, minutes, bpm, formRng);
    const int phases = static_cast<int>(form.phaseBpm.size());

    Score s;
    s.clear(form.phaseBpm[0]);
    s.seed = seed;
    s.keyRoot = key;
    s.lengthBeats = form.lengthBeats;
    for (const Section& sec : form.sections) {
        if (sec.type == SectionType::Bridge) s.tempo.add(sec.beat, form.phaseBpm[static_cast<size_t>(sec.phase)], false);
        std::string name = sectionName(sec.type);
        if (phases > 1 && sec.type != SectionType::Atmo && sec.type != SectionType::Coda) name += " " + std::to_string(sec.phase + 1);
        s.markers.push_back({ sec.beat, name });
    }

    // The rows of the piece: a bass row, the profile's counters, a transposer on row 8.
    Rng cfg = stream(sRows);
    const int counters = std::clamp(prof.peakRows - 1, 0, kRows - 2);
    std::vector<int> lengths = prof.counterLengths;
    for (size_t i = lengths.size(); i > 1; --i) std::swap(lengths[i - 1], lengths[static_cast<size_t>(cfg.below(static_cast<int>(i)))]);
    auto rowSet = [&](int r, int index, float v) { p.set(p.id(Module::Row, r, index), v); };
    for (int r = 0; r < kRows; ++r) rowSet(r, row::Active, 0.0f);
    rowSet(0, row::Length, 16); rowSet(0, row::Division, static_cast<float>(RowDivision::Sixteenth));
    rowSet(0, row::Octave, -1); rowSet(0, row::Mutation, prof.mutation); rowSet(0, row::Mode, 0);
    for (int c = 0; c < counters; ++c) {
        const int r = c + 1;
        rowSet(r, row::Length, static_cast<float>(lengths[static_cast<size_t>(c) % lengths.size()]));
        rowSet(r, row::Division, static_cast<float>(c % 2 == 0 ? RowDivision::Sixteenth : RowDivision::Eighth));
        rowSet(r, row::Octave, c % 2 == 0 ? 0.0f : 1.0f);
        rowSet(r, row::Mutation, prof.mutation);
        rowSet(r, row::Gate, 40.0f + 15.0f * cfg.uniform());
        rowSet(r, row::Mode, 0);
    }
    const int tr = kRows - 1;
    rowSet(tr, row::Mode, static_cast<float>(RowMode::Transposer));
    rowSet(tr, row::Division, static_cast<float>(prof.transposerDivision));
    rowSet(tr, row::Length, static_cast<float>(prof.transposerLength));
    rowSet(tr, row::Mutation, 0.3f);

    // The rack, phase by phase: new patterns for every phase, rows in through the builds.
    Rack rack;
    rack.setup(p, streamSeed(sRack));
    std::vector<double> rowFrom(kRows, -1.0);   // first beat each row plays, for the hands
    for (int ph = 0; ph < phases; ++ph) {
        const Section* entry = form.find(SectionType::Entry, ph);
        if (entry == nullptr) continue;
        // Everything before this phase is played with the old patterns.
        s.sort();
        rack.run(s, entry->beat);
        rack.generate(0, RowRole::Bass);
        for (int c = 0; c < counters; ++c) rack.generate(c + 1, c % 2 == 0 ? RowRole::Counter : RowRole::Walk);
        rack.generate(tr, RowRole::Transposer);
        std::vector<RackEvent> ev;
        if (form.phaseKey[static_cast<size_t>(ph)] != 0 || ph > 0) ev.push_back({ entry->beat, -1, RackOp::Key, form.phaseKey[static_cast<size_t>(ph)] });
        ev.push_back({ entry->beat, 0, RackOp::Start, 0 });
        if (rowFrom[0] < 0.0) rowFrom[0] = entry->beat;
        int started = 0;
        for (const Section& sec : form.sections) {
            if (sec.phase != ph) continue;
            if (sec.type == SectionType::Build) {
                if (sec.index == 0) ev.push_back({ sec.beat, tr, RackOp::Start, 0 });
                if (started < counters) {
                    ev.push_back({ sec.beat, started + 1, RackOp::Start, 0 });
                    if (rowFrom[static_cast<size_t>(started + 1)] < 0.0) rowFrom[static_cast<size_t>(started + 1)] = sec.beat;
                    ++started;
                }
            } else if (sec.type == SectionType::Peak) {
                for (; started < counters; ++started) {
                    ev.push_back({ sec.beat, started + 1, RackOp::Start, 0 });
                    if (rowFrom[static_cast<size_t>(started + 1)] < 0.0) rowFrom[static_cast<size_t>(started + 1)] = sec.beat;
                }
            } else if (sec.type == SectionType::Breakdown) {
                for (int c = 0; c < counters; ++c) ev.push_back({ sec.beat, c + 1, RackOp::Stop, 0 });
                ev.push_back({ sec.beat, tr, RackOp::Stop, 0 });
            }
        }
        // The end of the phase: the next bridge, or the coda.
        const Section* bridge = form.find(SectionType::Bridge, ph + 1);
        const Section* coda = form.find(SectionType::Coda, ph);
        const double end = bridge != nullptr ? bridge->beat : (coda != nullptr ? coda->beat : form.lengthBeats);
        for (int c = 0; c < counters; ++c) ev.push_back({ end, c + 1, RackOp::Stop, 0 });
        ev.push_back({ end, tr, RackOp::Stop, 0 });
        // In the coda the bass row plays on for a third of it, then leaves the atmosphere alone.
        const double bassEnd = bridge != nullptr ? end : end + std::floor((coda != nullptr ? coda->length : 0.0) / 3.0 / kBeatsPerBar) * kBeatsPerBar;
        ev.push_back({ bassEnd, 0, RackOp::Stop, 0 });
        s.rack.insert(s.rack.end(), ev.begin(), ev.end());
    }
    s.sort();
    rack.run(s, form.lengthBeats);
    s.rootShifts = rack.shifts();

    // What follows the roots.
    Rng layers = stream(sLayers);
    const bool tapeOn = layers.uniform() < prof.tapeChance;
    const bool stringsOn = layers.uniform() < prof.stringsChance;
    const bool bleepsOn = layers.uniform() < prof.bleepChance;
    const bool drumsOn = layers.uniform() < prof.drumsChance;   // drawn last, so the draws above stay as they were
    const Section* coda = form.find(SectionType::Coda, phases - 1);
    writeDrone(s, key, 0.0, coda != nullptr ? coda->beat + coda->length * 0.8 : form.lengthBeats);

    Rng pads = stream(sPads);
    PadPlan pp;
    pp.keyRoot = key;
    pp.scale = rack.scale();
    pp.shifts = s.rootShifts;
    PadPlan sp = pp;
    sp.part = Part::Strings;
    sp.low = 62; sp.high = 81;
    sp.restrikeSeconds = 1e6;
    sp.colour = 0.5f;
    Rng leadRng = stream(sLead);
    LeadPlan lp;
    lp.keyRoot = key;
    lp.scale = rack.scale();
    lp.shifts = s.rootShifts;
    double firstLead = -1.0;
    for (int ph = 0; ph < phases; ++ph) {
        const Section* peak = form.find(SectionType::Peak, ph);
        const Section* breakdown = form.find(SectionType::Breakdown, ph);
        const Section* lead = form.find(SectionType::Lead, ph);
        // The tape keys from the second build (or the only one) to the end of the peak.
        const Section* b1 = nullptr;
        for (const Section& sec : form.sections)
            if (sec.phase == ph && sec.type == SectionType::Build && (b1 == nullptr || sec.index == 1)) b1 = &sec;
        if (tapeOn && b1 != nullptr && peak != nullptr) writeChords(s, pp, b1->beat, peak->beat + peak->length, pads);
        if (stringsOn && peak != nullptr)
            writeChords(s, sp, peak->beat, breakdown != nullptr ? breakdown->beat + breakdown->length : peak->beat + peak->length, pads);
        if (lead != nullptr) {
            lp.intensity = prof.leadIntensity;
            writeLead(s, lp, lead->beat, lead->beat + lead->length, leadRng);
            if (firstLead < 0.0) firstLead = lead->beat;
        }
        if (lead != nullptr && peak != nullptr) {
            lp.intensity = std::min(1.0f, prof.leadIntensity + 0.25f);
            writeLead(s, lp, peak->beat, peak->beat + peak->length, leadRng);
        }
        const Section* bridge = form.find(SectionType::Bridge, ph + 1);
        if (tapeOn && bridge != nullptr) writeChords(s, pp, bridge->beat, bridge->beat + bridge->length, pads);
        // The drums come late: from the second build (or the peak) to the end of the peak.
        if (drumsOn && peak != nullptr) {
            const Section* second = nullptr;
            for (const Section& sec : form.sections) if (sec.phase == ph && sec.type == SectionType::Build && sec.index == 1) second = &sec;
            writeDrums(s, style, second != nullptr ? second->beat : peak->beat, peak->beat + peak->length, layers);
        }
    }

    // The atmosphere: wind and sweeps where no rows play, bleeps in the builds.
    const int wind = p.id(Module::Atmos, 0, atmos::Wind), sweeps = p.id(Module::Atmos, 0, atmos::Sweeps);
    const int bleeps = p.id(Module::Atmos, 0, atmos::Bleeps);
    using G = GestureShape;
    float windAt = 0.0f;
    auto windTo = [&](double b0, double b1, float v) {
        if (b1 <= b0) return;
        s.gestures.push_back({ wind, b0, b1 - b0, windAt, v, G::MinimumJerk, 1 });
        windAt = v;
    };
    for (const Section& sec : form.sections) {
        switch (sec.type) {
        case SectionType::Atmo:
            windTo(sec.beat, sec.beat + sec.length * 0.7, 0.6f);
            s.gestures.push_back({ sweeps, sec.beat, 0.0, 0.0f, 0.3f, G::Step, 1 });
            break;
        case SectionType::Entry:
            windTo(sec.beat, sec.beat + sec.length, 0.3f);
            s.gestures.push_back({ sweeps, sec.beat, 0.0, 0.3f, 0.0f, G::Step, 1 });
            break;
        case SectionType::Build:
            if (sec.index == 0) windTo(sec.beat, sec.beat + sec.length, 0.12f);
            if (sec.index == 0 && bleepsOn) s.gestures.push_back({ bleeps, sec.beat, 0.0, 0.0f, 0.2f, G::Step, 1 });
            break;
        case SectionType::Peak:
            if (bleepsOn) s.gestures.push_back({ bleeps, sec.beat, 0.0, 0.2f, 0.0f, G::Step, 1 });
            break;
        case SectionType::Bridge:
            windTo(sec.beat, sec.beat + sec.length * 0.6, 0.5f);
            s.gestures.push_back({ sweeps, sec.beat, 0.0, 0.0f, 0.3f, G::Step, 1 });
            break;
        case SectionType::Coda:
            windTo(sec.beat, sec.beat + sec.length * 0.6, 0.6f);
            s.gestures.push_back({ sweeps, sec.beat, 0.0, 0.0f, 0.3f, G::Step, 1 });
            break;
        default: break;
        }
    }

    // The settings of the piece, as steps at its start.
    const int tapeSet = p.id(Module::Tape, 0, tape::Set);
    s.gestures.push_back({ tapeSet, 0.0, 0.0, 0.0f, offsetTo(p, tapeSet, static_cast<float>(prof.tape)), G::Step, 1 });
    const int hall = p.id(Module::Reverb, 0, reverb::Decay);
    s.gestures.push_back({ hall, 0.0, 0.0, 0.0f, offsetTo(p, hall, prof.hallSeconds), G::Step, 1 });
    const int level = p.id(Module::Master, 0, master::Level);
    s.gestures.push_back({ level, 0.0, 0.0, 0.0f, offsetTo(p, level, p.get(level) + prof.levelDb), G::Step, 1 });

    // The hands, on the knobs of what plays.
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
    for (int c = 0; c < counters; ++c)
        if (rowFrom[static_cast<size_t>(c + 1)] >= 0.0)
            knobs.push_back(knob(Module::Voice, c + 1, voice::Cutoff, -0.40f, 0.30f, -0.28f, 0.15f, 1.5f, rowFrom[static_cast<size_t>(c + 1)]));
    if (firstLead >= 0.0) knobs.push_back(knob(Module::Lead, 0, lead::Cutoff, -0.30f, 0.30f, -0.10f, 0.20f, 1.0f, firstLead));
    knobs.push_back(knob(Module::Drone, 0, drone::Cutoff, -0.20f, 0.30f, -0.10f, 0.15f, 0.8f, 0.0));
    knobs.push_back(knob(Module::Atmos, 0, atmos::WindTone, -0.30f, 0.30f, 0.0f, 0.10f, 0.5f, 0.0));
    HandKnob throwKnob = knob(Module::Echo, 0, echo::Feedback, 0.0f, 0.4f, 0.0f, 0.05f, 0.6f, rowFrom[0]);
    throwKnob.atRest = 0.0f; throwKnob.atPeak = 0.05f;
    throwKnob.throws = true;
    throwKnob.scatter = 0.15f;
    knobs.push_back(throwKnob);
    Rng hands = stream(sHands);
    playHands(s, knobs, prof.hands, [&](double b) { return form.energy(b); }, 0.0, form.lengthBeats, hands);
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
    dst.lengthBeats = off + src.lengthBeats;
    dst.sort();
}

Score composeConcert(const ParamStore& p, uint64_t seed, double minutes, const Curation* curation)
{
    const StyleProfile& prof = styleProfile(static_cast<Style>(p.getInt(p.id(Module::Compose, 0, compose::Style))));
    Rng r;
    r.seed(mixSeed(seed, 99u + 131u * static_cast<uint64_t>(curation != nullptr ? curation->count("concert") : 0)));
    Score out;
    double elapsed = 0.0;
    int shift = 0;
    for (int i = 0; elapsed < minutes * 60.0 - 90.0 && i < 64; ++i) {
        const double left = minutes - elapsed / 60.0;
        double m = prof.minutesLow + (prof.minutesHigh - prof.minutesLow) * r.uniform();
        if (left - m < prof.minutesLow * 0.6) m = left;   // no short piece at the end: the last takes the rest
        m = std::max(4.0, m);
        Score piece = composePiece(p, mixSeed(seed, 1000 + static_cast<uint64_t>(i)), m, shift, curation,
                                   "piece" + std::to_string(i + 1) + ".");
        for (Marker& mk : piece.markers) mk.text = "Stueck " + std::to_string(i + 1) + ": " + mk.text;
        if (i == 0) { out = piece; out.rootShifts.clear(); out.lengthBeats = 0.0; out.notes.clear(); out.gestures.clear();
                      out.rack.clear(); out.markers.clear(); }
        const double before = out.tempo.secondsAt(out.lengthBeats);
        appendScore(out, piece, shift);
        elapsed += out.tempo.secondsAt(out.lengthBeats) - before;
        // The next key: a fourth, a fifth, the relative, a tone.
        const int moves[5] = { 5, -5, 3, -2, 2 };
        shift += moves[r.below(5)];
        shift = pitchClass(shift + 6) - 6;
    }
    return out;
}

} // namespace eph
