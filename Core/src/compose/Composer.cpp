/**
 * @file Composer.cpp
 * @brief A piece and a concert.
 * @author Rene Weller
 */
#include "eph/compose/Composer.h"
#include "eph/compose/Form.h"
#include "eph/compose/GestureEngine.h"
#include "eph/compose/Harmony.h"
#include "eph/compose/Lead.h"
#include "eph/compose/Pads.h"
#include "eph/Rack.h"
#include "eph/compose/Style.h"
#include "eph/Presets.h"
#include "eph/synth/Wavetable.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

namespace eph {

namespace {

/** @brief The streams of a piece: one per step of PLAN 6, so changing one leaves the others. */
enum StreamE : uint64_t { S_FORM = 1, S_TEMPO, S_ROWS, S_RACK, S_LAYERS, S_LEAD, S_PADS, S_HANDS, S_SOUNDS };

/**
 * @brief The offset in a knob's normalised range that takes it from its default to @p value.
 * @param[in] p      the parameters
 * @param[in] id     the knob
 * @param[in] value  the value wanted
 * @return the offset in the normalised range
 */
float offsetTo(const ParamStore& p, int id, float value)
{
    return p.toNormalised(id, value) - p.toNormalised(id, p.get(id));
}

/**
 * @brief Drums for a span, by style (PLAN 5.7): an eighties kit in "Melodic", a sparse one in "Modern",
 *        a lone tom now and then in "Doom". General MIDI numbers; toms are tuned by the engine.
 * @param[in,out] s      the score written to
 * @param[in]     style  the style
 * @param[in]     from   the span's first beat
 * @param[in]     to     the span's end in beats
 * @param[in,out] rng    the random stream
 */
void writeDrums(Score& s, StyleE style, double from, double to, Rng& rng)
{
    auto hit = [&](double beat, int note, float vel) {
        if (beat >= from && beat < to) s.m_notes.push_back({ beat, 0.1, PartE::DRUMS, note, vel, false, false });
    };
    const bool sixteenths = rng.uniform() < 0.5f, shaker = rng.uniform() < 0.4f;
    int bar = 0;
    for (double b = std::ceil(from / BeatsPerBar) * BeatsPerBar; b < to; b += BeatsPerBar, ++bar) {
        const bool fill = (bar % 8) == 7;
        switch (style) {
        case StyleE::MELODIC:
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
        case StyleE::MODERN:
            hit(b, 36, 0.9f);
            if (rng.uniform() < 0.4f) hit(b + 2.5, 36, 0.7f);
            hit(b + 2.0, 38, 0.8f);   // half time
            for (double h = 0.5; h < 4.0; h += 1.0) if (rng.uniform() < 0.5f) hit(b + h, 37, 0.6f);
            for (double h = 0.0; h < 4.0; h += 0.25) if (rng.uniform() < 0.6f) hit(b + h, 42, 0.3f + 0.3f * rng.uniform());
            if (shaker) for (double h = 0.0; h < 4.0; h += 0.5) hit(b + h, 70, 0.3f);
            if ((bar % 16) == 15) { hit(b + 3.0, 48, 0.7f); hit(b + 3.5, 45, 0.7f); }
            break;
        case StyleE::DOOM:
            if (bar % 2 == 0) hit(b, 45, 0.8f);
            break;
        default: break;
        }
    }
}

/**
 * @brief The pulse (25.09.2026, hypnosis): a soft kick under the sequence where the kit does not play -- four to the
 *        floor in Melodic and Modern, a heartbeat in Cosmic, a slow beat in Drift, a low tom every bar in Doom.
 * @param[in,out] s      the score written to
 * @param[in]     style  the style
 * @param[in]     from   the span's first beat
 * @param[in]     to     the span's end in beats
 * @param[in,out] rng    the random stream
 */
void writePulse(Score& s, StyleE style, double from, double to, Rng& rng)
{
    auto hit = [&](double beat, int note, float vel) {
        if (beat >= from && beat < to) s.m_notes.push_back({ beat, 0.1, PartE::DRUMS, note, vel + 0.05f * rng.uniform(), false, false });
    };
    for (double b = std::ceil(from / BeatsPerBar) * BeatsPerBar; b < to; b += BeatsPerBar) {
        switch (style) {
        case StyleE::MELODIC: case StyleE::MODERN:
            for (int q = 0; q < 4; ++q) hit(b + q, 36, q == 0 ? 0.68f : 0.58f);
            break;
        case StyleE::COSMIC:
            hit(b, 36, 0.58f); hit(b + 0.5, 36, 0.38f); hit(b + 2.0, 36, 0.52f); hit(b + 2.5, 36, 0.35f);
            break;
        case StyleE::DRIFT:
            hit(b, 36, 0.45f); hit(b + 2.0, 36, 0.4f);
            break;
        case StyleE::DOOM:
            hit(b, 45, 0.6f);
            break;
        default: break;
        }
    }
}

} // namespace

const char* const UnitNames[9] = { "form", "tempo", "rows", "rack", "layers", "lead", "pads", "hands", "sounds" };

namespace {

/** @brief What the steps of a piece share: the settings, the profile, the form, the score being written. */
struct Piece {
    /**
     * @brief The piece's settings, profile, score and form; the rest is filled in by the steps.
     * @param[in,out] params   the piece's copy of the parameters
     * @param[in]     profile  the style profile
     * @param[in]     st       the style
     * @param[in,out] score    the score written to
     * @param[in]     f        the form
     */
    Piece(ParamStore& params, const StyleProfile& profile, StyleE st, Score& score, const PieceForm& f)
        : m_p(params), m_prof(profile), m_style(st), m_s(score), m_form(f) {}
    ParamStore& m_p;                        ///< the piece's own copy of the parameters
    const StyleProfile& m_prof;             ///< the style profile
    StyleE m_style;                          ///< its style
    Score& m_s;                             ///< the score being written
    const PieceForm& m_form;                ///< the form (step 1)
    int m_key = 0;                          ///< pitch class of the key
    int m_phases = 0;                       ///< sequence phases
    int m_scale = 0;                        ///< compose.scale order
    int m_counters = 0;                     ///< counter rows (rows 2 ..)
    std::vector<double> m_rowFrom;          ///< first beat each row plays, -1 if never (for the hands)
    double m_firstLead = -1.0;              ///< first beat of a lead section, -1 if none
    bool m_bleepsOn = false;                ///< the layers' draw for the atmosphere's bleeps
    bool m_grainsOn = false;                ///< the layers' last draw: the granular cloud
    double m_stringsFrom = -1.0;            ///< first beat of the string machine, -1 if the piece has none
    double m_polyFrom = -1.0;               ///< first beat of the pad synth, -1 if the piece has none
    std::vector<std::pair<double, int>> m_chords;   ///< the chord track: degrees of the mode over time (Harmony.h)
    std::vector<std::pair<double, int>> m_keys = { { 0.0, 0 } };   ///< the phases' keys over time, for the drone
    std::vector<std::pair<double, double>> m_doubled;   ///< spans where row 2 plays sixteenths (the pulse doubled)
    std::vector<std::pair<double, double>> m_breaks;    ///< spans where the rows stop before a peak (the style guide's 6.4)
    std::function<uint64_t(StreamE)> m_seedOf;   ///< the seed of a stream, rerolls counted
    /**
     * @brief A fresh generator on stream @p k.
     * @param[in] k  the stream
     * @return a generator seeded for it
     */
    Rng stream(StreamE k) const { Rng r; r.seed(m_seedOf(k)); return r; }
};

/**
 * @brief The tempo map (a new tempo starts with the bridge before its phase) and the markers.
 * @param[in,out] c  the piece
 */
void writeMarkers(Piece& c)
{
    for (const Section& sec : c.m_form.m_sections) {
        if (sec.m_type == SectionTypeE::BRIDGE) c.m_s.m_tempo.add(sec.m_beat, c.m_form.m_phaseBpm[static_cast<size_t>(sec.m_phase)], false);
        std::string name = sectionName(sec.m_type);
        if (c.m_phases > 1 && sec.m_type != SectionTypeE::ATMO && sec.m_type != SectionTypeE::CODA) name += " " + std::to_string(sec.m_phase + 1);
        c.m_s.m_markers.push_back({ sec.m_beat, name });
    }
}

/**
 * @brief Step 2a, the rows: a bass row, the profile's counter rows, a transposer on row 8.
 * @param[in,out] c  the piece
 */
void setUpRows(Piece& c)
{
    ParamStore& p = c.m_p;
    Rng cfg = c.stream(S_ROWS);
    const int counters = c.m_counters = std::clamp(c.m_prof.m_peakRows - 1, 0, Rows - 2);
    std::vector<int> lengths = c.m_prof.m_counterLengths;
    for (size_t i = lengths.size(); i > 1; --i) std::swap(lengths[i - 1], lengths[static_cast<size_t>(cfg.below(static_cast<int>(i)))]);
    auto rowSet = [&](int r, int index, float v) { p.set(p.id(ModuleE::ROW, r, index), v); };
    for (int r = 0; r < Rows; ++r) rowSet(r, row::ACTIVE, 0.0f);
    // The bass ostinato: eight or sixteen steps (the style guide's 4.2), its gate long (4.4).
    rowSet(0, row::LENGTH, cfg.uniform() < 0.5f ? 8.0f : 16.0f); rowSet(0, row::DIVISION, static_cast<float>(RowDivisionE::SIXTEENTH));
    rowSet(0, row::GATE, 60.0f + 15.0f * cfg.uniform());
    // The bass and the main sequence change at half the style's rate: the ground holds (hypnosis, 25.09.2026).
    rowSet(0, row::OCTAVE, -1); rowSet(0, row::MUTATION, 0.5f * c.m_prof.m_mutation); rowSet(0, row::MODE, 0);
    for (int k = 0; k < counters; ++k) {
        const int r = k + 1;
        rowSet(r, row::LENGTH, static_cast<float>(lengths[static_cast<size_t>(k) % lengths.size()]));
        rowSet(r, row::DIVISION, static_cast<float>(k % 2 == 0 ? RowDivisionE::SIXTEENTH : RowDivisionE::EIGHTH));
        rowSet(r, row::OCTAVE, k % 2 == 0 ? 0.0f : 1.0f);
        rowSet(r, row::MUTATION, k == 0 ? 0.5f * c.m_prof.m_mutation : c.m_prof.m_mutation);
        rowSet(r, row::GATE, 40.0f + 15.0f * cfg.uniform());
        rowSet(r, row::MODE, 0);
    }
    const int tr = Rows - 1;
    rowSet(tr, row::MODE, static_cast<float>(RowModeE::TRANSPOSER));
    rowSet(tr, row::DIVISION, static_cast<float>(c.m_prof.m_transposerDivision));
    rowSet(tr, row::LENGTH, static_cast<float>(c.m_prof.m_transposerLength));
    rowSet(tr, row::MUTATION, 0.0f);   // the chain of transpositions stays as drawn (Harmony.h)
}

/**
 * @brief The cycle of row @p r in beats, as set up.
 * @param[in] p  the parameters
 * @param[in] r  the row
 * @return its cycle in beats
 */
double cycleBeats(const ParamStore& p, int r)
{
    return std::clamp(static_cast<int>(p.get(p.id(ModuleE::ROW, r, row::LENGTH))), 1, MaxSteps)
         * rowDivisionBeats(static_cast<RowDivisionE>(static_cast<int>(p.get(p.id(ModuleE::ROW, r, row::DIVISION)))));
}

/**
 * @brief Step 2b, the rack: new patterns in every phase; rows in through the builds, all at the peak, the
 *        counters out in the breakdown, everything out at the bridge or the coda (the bass a third into the coda).
 *        The harmony (Harmony.h, the style guide's 3.x): the chord track on the bass row from the first build to
 *        the breakdown, drawn anew at the peak in the peak's mode; the transposer only on the plateau (the lead's
 *        section) and at the peak; a parallel change of mode at the peak (Aeolian to Dorian) or in the
 *        breakdown (to Phrygian).
 * @param[in,out] c  the piece
 */
void writeRack(Piece& c)
{
    ParamStore& p = c.m_p;
    Score& s = c.m_s;
    const PieceForm& form = c.m_form;
    const int counters = c.m_counters;
    const int tr = Rows - 1;
    Rack rack;
    rack.setup(p, c.m_seedOf(S_RACK));
    std::vector<double>& rowFrom = c.m_rowFrom;
    rowFrom.assign(Rows, -1.0);
    c.m_scale = p.getInt(p.id(ModuleE::COMPOSE, 0, compose::SCALE));
    // The harmony's own stream, on the rows' unit: a reroll of the rows draws a new harmony as well.
    Rng harm;
    harm.seed(mixSeed(c.m_seedOf(S_ROWS), 0x68u));
    // Chances of the parallel changes of mode (3.2): brighter at the peak, darker in the breakdown.
    static const float Brighten[] = { 0.35f, 0.0f, 0.5f, 0.3f, 0.2f }, Darken[] = { 0.1f, 0.4f, 0.0f, 0.1f, 0.0f };
    const int si = std::clamp(static_cast<int>(c.m_style), 0, 4);
    // A mode of its own for a later phase, as the parts of a suite have (the style guide's 6.5): another of the
    // minor modes, by the guide's weights (Aeolian 40, Dorian 25, Phrygian 15). It starts at the bridge before
    // the phase, so the phase's rows are drawn in it.
    static const float PhaseMode[] = { 0.4f, 0.5f, 0.35f, 0.3f, 0.3f };
    std::vector<int> phaseScale(static_cast<size_t>(std::max(1, c.m_phases)), c.m_scale);
    for (int ph = 1; ph < c.m_phases; ++ph) {
        if (c.m_scale > 2 || harm.uniform() >= PhaseMode[si]) continue;
        const float w[3] = { c.m_scale == 0 ? 0.0f : 0.40f, c.m_scale == 1 ? 0.0f : 0.25f, c.m_scale == 2 ? 0.0f : 0.15f };
        float u = harm.uniform() * (w[0] + w[1] + w[2]);
        int m = 0;
        while (m < 2 && (u -= w[m]) > 0.0f) ++m;
        phaseScale[static_cast<size_t>(ph)] = m;
    }
    for (int ph = 0; ph < c.m_phases; ++ph) {
        const Section* entry = form.find(SectionTypeE::ENTRY, ph);
        if (entry == nullptr) continue;
        // Everything before this phase is played with the old patterns.
        s.sort();
        rack.run(s, entry->m_beat);
        rack.generate(0, RowRoleE::BASS);
        for (int k = 0; k < counters; ++k) rack.generate(k + 1, k % 2 == 0 ? RowRoleE::COUNTER : RowRoleE::WALK);
        rack.generate(tr, RowRoleE::TRANSPOSER);
        std::vector<RackEvent> ev;
        if (form.m_phaseKey[static_cast<size_t>(ph)] != 0 || ph > 0) {
            ev.push_back({ entry->m_beat, -1, RackOpE::KEY, form.m_phaseKey[static_cast<size_t>(ph)] });
            c.m_keys.push_back({ entry->m_beat, form.m_phaseKey[static_cast<size_t>(ph)] });
        }
        ev.push_back({ entry->m_beat, 0, RackOpE::START, 0 });
        if (rowFrom[0] < 0.0) rowFrom[0] = entry->m_beat;
        int started = 0;
        for (const Section& sec : form.m_sections) {
            if (sec.m_phase != ph) continue;
            if (sec.m_type == SectionTypeE::LEAD) {
                ev.push_back({ sec.m_beat, tr, RackOpE::START, 0 });
            } else if (sec.m_type == SectionTypeE::BUILD) {
                if (started < counters) {
                    ev.push_back({ sec.m_beat, started + 1, RackOpE::START, 0 });
                    // It comes in with rests and fills up, a step every two bars (the style guide's 6.4).
                    ev.push_back({ sec.m_beat, started + 1, RackOpE::THIN, 6 });
                    for (int f = 1; f <= 6; ++f) ev.push_back({ sec.m_beat + 8.0 * f, started + 1, RackOpE::FILL, 1 });
                    if (rowFrom[static_cast<size_t>(started + 1)] < 0.0) rowFrom[static_cast<size_t>(started + 1)] = sec.m_beat;
                    ++started;
                }
            } else if (sec.m_type == SectionTypeE::PEAK) {
                if (form.find(SectionTypeE::LEAD, ph) == nullptr) ev.push_back({ sec.m_beat, tr, RackOpE::START, 0 });
                for (; started < counters; ++started) {
                    ev.push_back({ sec.m_beat, started + 1, RackOpE::START, 0 });
                    if (rowFrom[static_cast<size_t>(started + 1)] < 0.0) rowFrom[static_cast<size_t>(started + 1)] = sec.m_beat;
                }
            } else if (sec.m_type == SectionTypeE::BREAKDOWN) {
                for (int k = 0; k < counters; ++k) ev.push_back({ sec.m_beat, k + 1, RackOpE::STOP, 0 });
                ev.push_back({ sec.m_beat, tr, RackOpE::STOP, 0 });
            }
        }
        // The end of the phase: the next bridge, or the coda.
        const Section* bridge = form.find(SectionTypeE::BRIDGE, ph + 1);
        const Section* coda = form.find(SectionTypeE::CODA, ph);
        const double end = bridge != nullptr ? bridge->m_beat : (coda != nullptr ? coda->m_beat : form.m_lengthBeats);
        // The harmony of the phase.
        const Section* build0 = form.find(SectionTypeE::BUILD, ph);
        const Section* peak = form.find(SectionTypeE::PEAK, ph);
        const Section* breakdown = form.find(SectionTypeE::BREAKDOWN, ph);
        const double chordsEnd = breakdown != nullptr ? breakdown->m_beat : end;
        const int base = phaseScale[static_cast<size_t>(ph)];
        const int next = ph + 1 < c.m_phases ? phaseScale[static_cast<size_t>(ph + 1)] : base;
        int peakScale = base;
        if (peak != nullptr && base == 0 && harm.uniform() < Brighten[si]) {
            peakScale = 1;
            ev.push_back({ peak->m_beat, -1, RackOpE::SCALE, 1 });
            ev.push_back({ chordsEnd, -1, RackOpE::SCALE, base });
        }
        bool darkened = false;
        if (breakdown != nullptr && (base == 0 || base == 1) && harm.uniform() < Darken[si]) {
            ev.push_back({ breakdown->m_beat, -1, RackOpE::SCALE, 2 });
            darkened = true;
        }
        // At the phase's end: back from the breakdown's darkening, or on into the next phase's mode.
        if (darkened || next != base) ev.push_back({ end, -1, RackOpE::SCALE, next });
        // The sequencing of the phase (the style guide's 4.3): ratchets on the plateau and at the peak, the pulse of
        // an eighths row doubled at the peak, the bass losing steps in the breakdown. Their own draws, after the harmony's.
        static const float Ratchets[] = { 0.6f, 0.3f, 0.8f, 0.7f, 0.2f }, Double[] = { 0.5f, 0.3f, 0.6f, 0.5f, 0.0f };
        const Section* plateau = form.find(SectionTypeE::LEAD, ph);
        const int lead = counters > 0 ? 1 : 0;   // the main sequence: the first counter row, else the bass
        if (harm.uniform() < Ratchets[si]) {
            if (plateau != nullptr) ev.push_back({ plateau->m_beat, lead, RackOpE::RATCHET, 1 });
            if (peak != nullptr) {
                ev.push_back({ peak->m_beat, lead, RackOpE::RATCHET, 2 });
                ev.push_back({ peak->m_beat, 0, RackOpE::RATCHET, 1 });
            }
            ev.push_back({ chordsEnd, lead, RackOpE::RATCHET, 0 });
            ev.push_back({ chordsEnd, 0, RackOpE::RATCHET, 0 });
        }
        if (peak != nullptr && counters >= 2 && harm.uniform() < Double[si]) {
            // Row 2 runs in eighths (setUpRows): sixteenths from the peak to the breakdown.
            ev.push_back({ peak->m_beat, 2, RackOpE::DIVISION, static_cast<int>(RowDivisionE::SIXTEENTH) });
            ev.push_back({ chordsEnd, 2, RackOpE::DIVISION, static_cast<int>(RowDivisionE::EIGHTH) });
            c.m_doubled.push_back({ peak->m_beat, chordsEnd });
        }
        if (breakdown != nullptr)
            for (double b = breakdown->m_beat + 2 * BeatsPerBar; b < breakdown->m_beat + breakdown->m_length; b += 4 * BeatsPerBar)
                ev.push_back({ b, 0, RackOpE::THIN, 1 });
        // Whether row r plays at beat b in this phase, by the events written so far.
        auto running = [&](int r, double b) {
            double on = -1.0;
            for (const RackEvent& e : ev) if (e.m_row == r && e.m_op == RackOpE::START && e.m_beat <= b) on = std::max(on, e.m_beat);
            if (on < 0.0) return false;
            for (const RackEvent& e : ev) if (e.m_row == r && e.m_op == RackOpE::STOP && e.m_beat > on && e.m_beat <= b) return false;
            return true;
        };
        // The variations (the style guide's 4.3: no sequence runs more than sixteen bars unchanged): every 8 to 16
        // bars one planned event -- a step gated, a step an octave off, the row a step or two shorter for four or
        // eight bars (skip), its direction turned for as long, or the row back to its theme -- on the main sequence
        // mostly, now and then on another counter row or the bass. Their own stream on the rack's unit.
        {
            Rng var;
            var.seed(mixSeed(c.m_seedOf(S_RACK), 0x766172u + static_cast<uint64_t>(ph)));
            int changes[Rows] = {};
            const double from = (build0 != nullptr ? build0->m_beat : entry->m_beat) + 4.0 * BeatsPerBar;
            for (double t = from; t < chordsEnd - 4.0 * BeatsPerBar;) {
                const float u = var.uniform();
                int target = u < 0.6f ? lead : (u < 0.9f && counters > 1 ? 2 + var.below(counters - 1) : 0);
                if (!running(target, t)) target = running(lead, t) ? lead : 0;
                if (running(target, t)) {
                    const int len = std::clamp(static_cast<int>(p.get(p.id(ModuleE::ROW, target, row::LENGTH))), 1, MaxSteps);
                    const double hold = (var.uniform() < 0.5f ? 4.0 : 8.0) * BeatsPerBar;
                    const bool bass = target == 0, room = t + hold < chordsEnd - 1e-9;
                    const float w[5] = { 3.0f, 2.0f, bass || !room || len < 4 ? 0.0f : 1.5f, bass || !room ? 0.0f : 1.0f,
                                         changes[target] >= 2 ? 1.5f : 0.0f };
                    float x = var.uniform() * (w[0] + w[1] + w[2] + w[3] + w[4]);
                    int k = 0;
                    while (k < 4 && (x -= w[k]) > 0.0f) ++k;
                    if (w[k] <= 0.0f) k = 0;
                    switch (k) {
                    case 0: ev.push_back({ t, target, RackOpE::GATE, 1 }); ++changes[target]; break;
                    case 1: ev.push_back({ t, target, RackOpE::OCTAVE_STEP, 1 }); ++changes[target]; break;
                    case 2:
                        ev.push_back({ t, target, RackOpE::SET_LENGTH, len - 1 - (len >= 8 ? var.below(2) : 0) });
                        ev.push_back({ t + hold, target, RackOpE::SET_LENGTH, len });
                        break;
                    case 3:
                        ev.push_back({ t, target, RackOpE::DIRECTION, static_cast<int>(var.uniform() < 0.5f ? RowDirectionE::BACKWARD : RowDirectionE::PENDULUM) });
                        ev.push_back({ t + hold, target, RackOpE::DIRECTION, static_cast<int>(RowDirectionE::FORWARD) });
                        break;
                    default: ev.push_back({ t, target, RackOpE::THEME, 0 }); changes[target] = 0; break;
                    }
                }
                static const double Gap[4] = { 8.0, 8.0, 12.0, 16.0 };
                t += Gap[var.below(4)] * BeatsPerBar;
            }
        }
        // A break before the peak (the style guide's 6.4): the rows stop for two or four bars -- the echoes' tails,
        // the pads and the lead go on, the drums stop too -- and all come back together on the peak's downbeat.
        if (peak != nullptr) {
            static const float Break[] = { 0.4f, 0.3f, 0.6f, 0.6f, 0.2f };   // Cosmic, Doom, Melodic, Modern, Drift
            Rng br;
            br.seed(mixSeed(c.m_seedOf(S_RACK), 0x62726bu + static_cast<uint64_t>(ph)));
            if (br.uniform() < Break[si]) {
                const double b0 = peak->m_beat - (br.uniform() < 0.6f ? 2.0 : 4.0) * BeatsPerBar;
                if (b0 >= (build0 != nullptr ? build0->m_beat : entry->m_beat) + 8.0 * BeatsPerBar) {
                    bool any = false;
                    for (int r = 0; r <= counters; ++r) {
                        if (!running(r, b0 - 1e-6)) continue;
                        ev.push_back({ b0, r, RackOpE::STOP, 0 });
                        ev.push_back({ peak->m_beat, r, RackOpE::START, 0 });
                        any = true;
                    }
                    if (any) c.m_breaks.push_back({ b0, peak->m_beat });
                }
            }
        }
        auto chordSpan = [&](double b0, double b1, int scale) {
            if (b1 <= b0) return;
            for (const auto& ch : drawChordTrack(c.m_style, scale, b0, b1, harm)) {
                ev.push_back({ ch.first, 0, RackOpE::CHORD, bassDegree(scale, ch.second) });
                c.m_chords.push_back(ch);
            }
        };
        if (build0 != nullptr) {
            chordSpan(build0->m_beat, peak != nullptr ? std::min(peak->m_beat, chordsEnd) : chordsEnd, base);
            if (peak != nullptr) chordSpan(peak->m_beat, chordsEnd, peakScale);
            ev.push_back({ chordsEnd, 0, RackOpE::CHORD, 0 });
            c.m_chords.push_back({ chordsEnd, 0 });
        }
        // A DJ outro (a night set's piece): the rows play on, whole, for the next piece to come in over them -- the
        // counter rows until its bass comes in, the main sequence and the bass to the end.
        const bool outro = bridge == nullptr && c.m_prof.m_djOutro;
        const double codaBars = coda != nullptr ? std::floor(coda->m_length / BeatsPerBar) : 0.0;
        const double handover = end + (c.m_prof.m_bassHandover > 0 ? std::min<double>(c.m_prof.m_bassHandover, codaBars) : std::floor(codaBars / 2.0)) * BeatsPerBar;
        for (int k = 0; k < counters; ++k)
            ev.push_back({ outro ? (k == 0 ? form.m_lengthBeats : handover) : end, k + 1, RackOpE::STOP, 0 });
        ev.push_back({ end, tr, RackOpE::STOP, 0 });
        // In the coda the bass row plays on for a third of it, then leaves the atmosphere alone.
        const double bassEnd = outro ? form.m_lengthBeats
                             : bridge != nullptr ? end : end + std::floor((coda != nullptr ? coda->m_length : 0.0) / 3.0 / BeatsPerBar) * BeatsPerBar;
        ev.push_back({ bassEnd, 0, RackOpE::STOP, 0 });
        // In the coda the bass loses a step every two bars before it stops (the style guide's 4.7).
        if (bridge == nullptr && !outro)
            for (double b = end + 2 * BeatsPerBar; b < bassEnd; b += 2 * BeatsPerBar) ev.push_back({ b, 0, RackOpE::THIN, 1 });
        s.m_rack.insert(s.m_rack.end(), ev.begin(), ev.end());
    }
    s.sort();
    rack.run(s, form.m_lengthBeats);
    s.m_rootShifts = rack.shifts();
    s.m_scaleShifts = rack.scales();
    // The rows' shapes for a display, as the rack read them (Rack::setup).
    for (int r = 0; r < Rows; ++r) {
        const int length = std::clamp(static_cast<int>(p.get(p.id(ModuleE::ROW, r, row::LENGTH))), 1, MaxSteps);
        const double div = rowDivisionBeats(static_cast<RowDivisionE>(static_cast<int>(p.get(p.id(ModuleE::ROW, r, row::DIVISION)))));
        const bool transposer = static_cast<int>(p.get(p.id(ModuleE::ROW, r, row::MODE))) == static_cast<int>(RowModeE::TRANSPOSER);
        s.m_rowShapes.push_back({ 0.0, r, length, div, transposer });
    }
    // Row 2's doubled pulse, for the displays.
    const int length2 = std::clamp(static_cast<int>(p.get(p.id(ModuleE::ROW, 2, row::LENGTH))), 1, MaxSteps);
    for (const auto& d : c.m_doubled) {
        s.m_rowShapes.push_back({ d.first, 2, length2, rowDivisionBeats(RowDivisionE::SIXTEENTH), false });
        s.m_rowShapes.push_back({ d.second, 2, length2, rowDivisionBeats(RowDivisionE::EIGHTH), false });
    }
}

/**
 * @brief Step 3, what follows the rack's roots: the drone throughout, the tape keys from the second build
 *        to the end of the peak and in the bridges, the string machine at the peak and in the breakdown,
 *        the lead in its section and at the peak, the drums from the second build -- each with the
 *        profile's chance.
 * @param[in,out] c  the piece
 */
void writeLayers(Piece& c)
{
    Score& s = c.m_s;
    const PieceForm& form = c.m_form;
    const StyleProfile& prof = c.m_prof;
    Rng layers = c.stream(S_LAYERS);
    const bool tapeOn = layers.uniform() < prof.m_tapeChance;
    const bool stringsOn = layers.uniform() < prof.m_stringsChance;
    c.m_bleepsOn = layers.uniform() < prof.m_bleepChance;
    const bool drumsOn = layers.uniform() < prof.m_drumsChance;   // drawn last, so the draws above stay as they were
    const Section* coda = form.find(SectionTypeE::CODA, c.m_phases - 1);
    writeDrone(s, c.m_key, 0.0, coda != nullptr ? coda->m_beat + coda->m_length * 0.8 : form.m_lengthBeats, &c.m_keys);

    Rng pads = c.stream(S_PADS);
    PadPlan pp;
    pp.m_keyRoot = c.m_key;
    pp.m_scale = c.m_scale;
    pp.m_shifts = s.m_rootShifts;
    pp.m_chords = c.m_chords;
    pp.m_scales = s.m_scaleShifts;
    PadPlan sp = pp;
    sp.m_part = PartE::STRINGS;
    sp.m_low = 62; sp.m_high = 81;
    sp.m_restrikeSeconds = 1e6;
    sp.m_choir = false;
    Rng leadRng = c.stream(S_LEAD);
    LeadPlan lp;
    lp.m_keyRoot = c.m_key;
    lp.m_scale = c.m_scale;
    lp.m_shifts = s.m_rootShifts;
    lp.m_scales = s.m_scaleShifts;
    for (int ph = 0; ph < c.m_phases; ++ph) {
        const Section* peak = form.find(SectionTypeE::PEAK, ph);
        const Section* breakdown = form.find(SectionTypeE::BREAKDOWN, ph);
        const Section* lead = form.find(SectionTypeE::LEAD, ph);
        // The tape keys from the second build (or the only one) to the end of the peak.
        const Section* b1 = nullptr;
        for (const Section& sec : form.m_sections)
            if (sec.m_phase == ph && sec.m_type == SectionTypeE::BUILD && (b1 == nullptr || sec.m_index == 1)) b1 = &sec;
        if (tapeOn && b1 != nullptr && peak != nullptr) writeChords(s, pp, b1->m_beat, peak->m_beat + peak->m_length, pads);
        if (stringsOn && peak != nullptr) {
            writeChords(s, sp, peak->m_beat, breakdown != nullptr ? breakdown->m_beat + breakdown->m_length : peak->m_beat + peak->m_length, pads);
            if (c.m_stringsFrom < 0.0) c.m_stringsFrom = peak->m_beat;
        }
        if (lead != nullptr) {
            lp.m_intensity = prof.m_leadIntensity;
            writeLead(s, lp, lead->m_beat, lead->m_beat + lead->m_length, leadRng);
            if (c.m_firstLead < 0.0) c.m_firstLead = lead->m_beat;
        }
        if (lead != nullptr && peak != nullptr) {
            lp.m_intensity = std::min(1.0f, prof.m_leadIntensity + 0.25f);
            writeLead(s, lp, peak->m_beat, peak->m_beat + peak->m_length, leadRng);
        }
        const Section* bridge = form.find(SectionTypeE::BRIDGE, ph + 1);
        if (tapeOn && bridge != nullptr) writeChords(s, pp, bridge->m_beat, bridge->m_beat + bridge->m_length, pads);
        // The drums come late: from the second build (or the peak) to the end of the peak, and never before 45 % of
        // the piece or after 90 % (the style guide's 5.3: the kick not before the sequence, gone before the end).
        double kitFrom = -1.0;
        if (drumsOn && peak != nullptr) {
            const Section* second = nullptr;
            for (const Section& sec : form.m_sections) if (sec.m_phase == ph && sec.m_type == SectionTypeE::BUILD && sec.m_index == 1) second = &sec;
            const double d0 = std::max(second != nullptr ? second->m_beat : peak->m_beat,
                                       std::ceil(0.3 * form.m_lengthBeats / BeatsPerBar) * BeatsPerBar);
            const double d1 = std::min(peak->m_beat + peak->m_length, std::floor(0.9 * form.m_lengthBeats / BeatsPerBar) * BeatsPerBar);
            if (d1 - d0 >= 8 * BeatsPerBar) { writeDrums(s, c.m_style, d0, d1, layers); kitFrom = d0; }
        }
        // The pulse under the sequence from the second build (or the first), not before a fifth of the piece, to the
        // kit or the breakdown; its own stream, so the layers' draws stay as they were.
        {
            static const float Pulse[] = { 0.5f, 0.35f, 0.8f, 0.7f, 0.3f };   // Cosmic, Doom, Melodic, Modern, Drift
            Rng pr;
            pr.seed(mixSeed(c.m_seedOf(S_LAYERS), 0x7075u + static_cast<uint64_t>(ph)));
            const Section* from = nullptr;
            for (const Section& sec : form.m_sections)
                if (sec.m_phase == ph && sec.m_type == SectionTypeE::BUILD && (from == nullptr || sec.m_index == 1)) from = &sec;
            if (from != nullptr && peak != nullptr && pr.uniform() < Pulse[std::clamp(static_cast<int>(c.m_style), 0, 4)]) {
                const double p0 = std::max(from->m_beat, std::ceil(0.2 * form.m_lengthBeats / BeatsPerBar) * BeatsPerBar);
                const double p1 = kitFrom >= 0.0 ? kitFrom : (breakdown != nullptr ? breakdown->m_beat : peak->m_beat + peak->m_length);
                if (p1 - p0 >= 8 * BeatsPerBar) writePulse(s, c.m_style, p0, std::min(p1, std::floor(0.9 * form.m_lengthBeats / BeatsPerBar) * BeatsPerBar), pr);
            }
        }
    }
    // The end on the open fifth (the style guide's 3.3, Phaedra's close): the strings, or else the tape keys,
    // hold the centre and its fifth over the fading drone of the coda.
    if (coda != nullptr && (stringsOn || tapeOn)) {
        PadPlan fifth = stringsOn ? sp : pp;
        fifth.m_openFifth = true;
        writeChords(s, fifth, coda->m_beat + coda->m_length * 0.15, coda->m_beat + coda->m_length * 0.85, pads);
    }
    // The granular cloud: the layers' last draw, so every draw above stays as it was.
    c.m_grainsOn = layers.uniform() < prof.m_grainChance;
    // The pad synth (25.09.2026, the style guide's 7.1: the polysynth plane of Stürtzer's, Quaeschning's and Redshift's
    // pads): in the atmosphere under the drone, from the first build to where the tape keys come in (or the peak), in
    // the breakdowns and the bridges -- wherever fewer than two other planes play (the production guide: never more
    // than two pads at once), an octave under the strings where they play. Its chance by style; its own streams, so
    // every draw above stays as it was.
    {
        static const float Poly[] = { 0.6f, 0.45f, 0.55f, 0.7f, 0.8f };   // Cosmic, Doom, Melodic, Modern, Drift
        const int si = std::clamp(static_cast<int>(c.m_style), 0, 4);
        Rng on;
        on.seed(mixSeed(c.m_seedOf(S_LAYERS), 0x706f6c79u));
        if (on.uniform() < Poly[si]) {
            Rng voicing;
            voicing.seed(mixSeed(c.m_seedOf(S_PADS), 0x706f6c79u));
            PadPlan pl = pp;
            pl.m_part = PartE::PAD;
            pl.m_restrikeSeconds = 1e6;
            pl.m_choir = false;
            auto span = [&](double b0, double b1) {
                if (b1 - b0 < 8.0 * BeatsPerBar) return;
                bool tape = false, strings = false;
                for (const NoteEvent& n : s.m_notes) {
                    if (n.m_beat >= b1 || n.m_beat + n.m_length <= b0) continue;
                    tape = tape || n.m_part == PartE::TAPE_KEYS;
                    strings = strings || n.m_part == PartE::STRINGS;
                }
                if (tape && strings) return;
                pl.m_low = strings ? 41 : 48;
                pl.m_high = strings ? 60 : 70;
                writeChords(s, pl, b0, b1, voicing);
                if (c.m_polyFrom < 0.0) c.m_polyFrom = b0;
            };
            const Section& atmo = form.m_sections.front();
            if (atmo.m_type == SectionTypeE::ATMO) span(atmo.m_beat + std::floor(0.25 * atmo.m_length / BeatsPerBar) * BeatsPerBar, atmo.m_beat + atmo.m_length);
            for (int ph = 0; ph < c.m_phases; ++ph) {
                const Section* build0 = form.find(SectionTypeE::BUILD, ph);
                const Section* peak = form.find(SectionTypeE::PEAK, ph);
                const Section* breakdown = form.find(SectionTypeE::BREAKDOWN, ph);
                const Section* bridge = form.find(SectionTypeE::BRIDGE, ph + 1);
                if (build0 != nullptr) {
                    const Section* b1 = nullptr;
                    for (const Section& sec : form.m_sections)
                        if (sec.m_phase == ph && sec.m_type == SectionTypeE::BUILD && sec.m_index == 1) b1 = &sec;
                    const double end = tapeOn && b1 != nullptr ? b1->m_beat : (peak != nullptr ? peak->m_beat : build0->m_beat + build0->m_length);
                    span(build0->m_beat, end);
                }
                if (breakdown != nullptr) span(breakdown->m_beat, breakdown->m_beat + breakdown->m_length);
                if (bridge != nullptr) span(bridge->m_beat, bridge->m_beat + bridge->m_length);
            }
        }
    }
    // The breaks are silent for the drums as well.
    for (const auto& b : c.m_breaks)
        s.m_notes.erase(std::remove_if(s.m_notes.begin(), s.m_notes.end(), [&](const NoteEvent& n) {
            return n.m_part == PartE::DRUMS && n.m_beat >= b.first - 1e-9 && n.m_beat < b.second - 1e-9;
        }), s.m_notes.end());
}

/**
 * @brief Step 4, the atmosphere: wind and sweeps where no rows play, bleeps in the builds.
 * @param[in,out] c  the piece
 */
void writeAtmosphere(Piece& c)
{
    Score& s = c.m_s;
    const int wind = c.m_p.id(ModuleE::ATMOS, 0, atmos::WIND), sweeps = c.m_p.id(ModuleE::ATMOS, 0, atmos::SWEEPS);
    const int bleeps = c.m_p.id(ModuleE::ATMOS, 0, atmos::BLEEPS);
    const int grains = c.m_p.id(ModuleE::ATMOS, 0, atmos::GRAINS);
    using GT = GestureShapeE;
    float windAt = 0.0f;
    auto windTo = [&](double b0, double b1, float v) {
        if (b1 <= b0) return;
        s.m_gestures.push_back({ wind, b0, b1 - b0, windAt, v, GT::MINIMUM_JERK, 1 });
        windAt = v;
    };
    // The granular cloud where the piece has it: up in the spaces without rows, away when they come in.
    float grainsAt = 0.0f;
    auto grainsTo = [&](double b0, double b1, float v) {
        if (!c.m_grainsOn || b1 <= b0 || v == grainsAt) return;
        s.m_gestures.push_back({ grains, b0, b1 - b0, grainsAt, v, GT::MINIMUM_JERK, 3 });   // the composer's own hand
        grainsAt = v;
    };
    for (const Section& sec : c.m_form.m_sections) {
        switch (sec.m_type) {
        case SectionTypeE::ATMO:
            grainsTo(sec.m_beat + sec.m_length * 0.2, sec.m_beat + sec.m_length * 0.8, 0.7f);
            windTo(sec.m_beat, sec.m_beat + sec.m_length * 0.7, 0.6f);
            s.m_gestures.push_back({ sweeps, sec.m_beat, 0.0, 0.0f, 0.3f, GT::STEP, 1 });
            break;
        case SectionTypeE::ENTRY:
            grainsTo(sec.m_beat, sec.m_beat + sec.m_length * 0.5, 0.0f);
            windTo(sec.m_beat, sec.m_beat + sec.m_length, 0.3f);
            s.m_gestures.push_back({ sweeps, sec.m_beat, 0.0, 0.3f, 0.0f, GT::STEP, 1 });
            break;
        case SectionTypeE::BUILD:
            if (sec.m_index == 0) windTo(sec.m_beat, sec.m_beat + sec.m_length, 0.12f);
            if (sec.m_index == 0 && c.m_bleepsOn) s.m_gestures.push_back({ bleeps, sec.m_beat, 0.0, 0.0f, 0.2f, GT::STEP, 1 });
            break;
        case SectionTypeE::PEAK:
            if (c.m_bleepsOn) s.m_gestures.push_back({ bleeps, sec.m_beat, 0.0, 0.2f, 0.0f, GT::STEP, 1 });
            break;
        case SectionTypeE::BRIDGE:
            grainsTo(sec.m_beat, sec.m_beat + sec.m_length * 0.5, 0.6f);
            windTo(sec.m_beat, sec.m_beat + sec.m_length * 0.6, 0.5f);
            s.m_gestures.push_back({ sweeps, sec.m_beat, 0.0, 0.0f, 0.3f, GT::STEP, 1 });
            break;
        case SectionTypeE::CODA:
            grainsTo(sec.m_beat + sec.m_length * 0.1, sec.m_beat + sec.m_length * 0.6, 0.7f);
            windTo(sec.m_beat, sec.m_beat + sec.m_length * 0.6, 0.6f);
            s.m_gestures.push_back({ sweeps, sec.m_beat, 0.0, 0.0f, 0.3f, GT::STEP, 1 });
            break;
        default: break;
        }
    }
}

/**
 * @brief Sets knob @p id of synth @p m / @p instance for the piece (compose.pick_sounds, 26.09.2026): on the piece's copy of
 *        the parameters, so every move written after it moves from there, and in the score's knob settings, which the
 *        engine puts on the knobs where the piece begins. A step of the same knob at the piece's start goes: the knob
 *        now is that value.
 * @param[in,out] c         the piece
 * @param[in]     m         the synth
 * @param[in]     instance  its instance
 * @param[in]     id        the knob
 * @param[in]     v         the value
 */
void setKnob(Piece& c, ModuleE m, int instance, int id, float v)
{
    const ParamDesc& d = c.m_p.desc(id);
    v = std::clamp(v, d.m_minValue, d.m_maxValue);
    c.m_p.set(id, v);
    auto& g = c.m_s.m_gestures;
    g.erase(std::remove_if(g.begin(), g.end(), [id](const Gesture& x) { return x.m_param == id && x.m_beat <= 1e-9 && x.m_shape == GestureShapeE::STEP; }),
            g.end());
    for (KnobSet& k : c.m_s.m_knobs) if (k.m_param == id && k.m_beat <= 1e-9) { k.m_value = v; return; }
    c.m_s.m_knobs.push_back({ 0.0, id, v, static_cast<int>(m), instance });
}

/**
 * @brief The piece's mix on the faders themselves (26.09.2026): every setting of a source's strip the piece makes at its
 *        start -- level, pan, the sends, the low cut, the distance, the punch, the spread, the auto pan -- becomes the
 *        knob's value (a KnobSet), and every later move of that knob along the form is rebased onto it, so what plays
 *        stays as it was while the mixer shows the piece's settings and a hand turns them from there.
 * @param[in,out] c  the piece
 */
void settleMix(Piece& c)
{
    static const char* const Mix[] = { "level", "pan", "echo", "echo2", "reverb", "blend", "early", "shimmer", "low_cut",
                                        "distance", "punch", "spread", "auto_pan" };
    ParamStore& p = c.m_p;
    auto& gestures = c.m_s.m_gestures;
    for (ModuleE m : { ModuleE::ROW, ModuleE::LEAD, ModuleE::DRONE, ModuleE::TAPE, ModuleE::STRINGS, ModuleE::POLY, ModuleE::DRUMS, ModuleE::ATMOS }) {
        const int instances = m == ModuleE::ROW ? Rows : 1;
        for (int inst = 0; inst < instances; ++inst) {
            for (int k = 0; k < ParamStore::moduleCount(m); ++k) {
                const int id = p.id(m, inst, k);
                const std::string key = p.desc(id).m_key;
                if (std::find_if(std::begin(Mix), std::end(Mix), [&](const char* x) { return key == x; }) == std::end(Mix)) continue;
                // The setting at the start: the last step at beat 0 (in the order the engine reads them).
                const Gesture* at0 = nullptr;
                for (const Gesture& g : gestures)
                    if (g.m_param == id && g.m_beat <= 1e-9 && (g.m_shape == GestureShapeE::STEP || g.m_length <= 0.0)) at0 = &g;
                if (at0 == nullptr) continue;
                const float knob = p.toNormalised(id, p.get(id));
                const float value = p.fromNormalised(id, std::clamp(knob + at0->m_to, 0.0f, 1.0f));
                const float shift = p.toNormalised(id, value) - knob;
                gestures.erase(std::remove_if(gestures.begin(), gestures.end(), [id](const Gesture& g) {
                    return g.m_param == id && g.m_beat <= 1e-9 && (g.m_shape == GestureShapeE::STEP || g.m_length <= 0.0);
                }), gestures.end());
                for (Gesture& g : gestures) if (g.m_param == id) { g.m_from -= shift; g.m_to -= shift; }
                p.set(id, value);
                c.m_s.m_knobs.push_back({ 0.0, id, value, static_cast<int>(m), inst, 1 });
            }
        }
    }
}

/**
 * @brief The piece's sounds (compose.pick_sounds, 25.09.2026): a factory preset for every synth, from the groups that
 *        fit its part -- the bass row a bass, the main sequence a squelchy, resonant or plucked one, the other counter
 *        rows glass, hollow or plucked, the lead and the kit the style's, the tape keys a group of the style's tape
 *        set -- on a row of the dark-to-bright grid near the style's darkness. Set on the knobs themselves as the piece
 *        begins (setKnob, 26.09.2026: a program change, no longer an offset), nudged where the part needs it (a bass not
 *        too bright, a main sequence with resonance and a deep envelope), and noted in the score for the pages. Its own
 *        stream: "reroll sounds" draws them again.
 * @param[in,out] c  the piece
 */
void writeSounds(Piece& c)
{
    ParamStore& p = c.m_p;
    Rng r = c.stream(S_SOUNDS);
    const float centre = std::clamp(3.5f + 12.0f * c.m_prof.m_darkness, 1.0f, 6.0f);
    using ValuesT = std::vector<std::pair<int, float>>;
    auto change = [](ValuesT& v, int k, const std::function<float(float)>& f) { for (auto& e : v) if (e.first == k) e.second = f(e.second); };
    auto pick = [&](ModuleE m, int inst, std::initializer_list<const char*> groups, const std::function<bool(const SoundPreset&)>& fits,
                    const std::function<void(ValuesT&)>& nudge) {
        const std::vector<SoundPreset>& list = factoryPresets(m);
        std::vector<int> starts;
        for (size_t g = 0; g * 64 < list.size(); ++g) {
            const SoundPreset& first = list[g * 64];
            bool named = groups.size() == 0;
            for (const char* n : groups) named = named || first.m_group == n;
            if (named && (!fits || fits(first))) starts.push_back(static_cast<int>(g * 64));
        }
        if (starts.empty()) return;
        const int bright = std::clamp(static_cast<int>(std::lround(centre + 3.0f * (r.uniform() - 0.5f))), 0, 7);
        const int index = starts[static_cast<size_t>(r.below(static_cast<int>(starts.size())))] + 8 * bright + r.below(8);
        const SoundPreset& preset = list[static_cast<size_t>(index)];
        ValuesT values;
        for (int k = 0; k < ParamStore::moduleCount(m); ++k) {
            if (presetLeaves(m, k)) continue;
            float v = p.desc(p.id(m, inst, k)).m_defValue;
            for (const auto& e : preset.m_values) if (e.first == k) v = e.second;
            values.push_back({ k, v });
        }
        if (nudge) nudge(values);
        for (const auto& [k, v] : values) setKnob(c, m, inst, p.id(m, inst, k), v);
        c.m_s.m_sounds.push_back({ 0.0, static_cast<int>(m), inst, index });
    };
    // The rows: the bass round and not too bright, the main sequence squelchy (resonance, a deep envelope, accents,
    // a short decay), the other counter rows with an envelope that speaks.
    pick(ModuleE::VOICE, 0, { "Ladder Bass", "Deep Ostinato", "Dark Throb", "Warm Unison" }, nullptr, [&](ValuesT& v) {
        change(v, voice::CUTOFF, [](float x) { return std::min(x, 420.0f); });
        change(v, voice::RESONANCE, [](float x) { return std::min(x, 0.45f); });
    });
    for (int k = 0; k < c.m_counters; ++k) {
        if (k == 0) {
            pick(ModuleE::VOICE, 1, { "Squelch Arp", "Resonant Sweep", "Pluck Sequence", "Tape Sequence", "Staccato Pulse", "Accent Ratchet" },
                 nullptr, [&](ValuesT& v) {
                change(v, voice::RESONANCE, [](float x) { return std::max(x, 0.45f); });
                change(v, voice::ENV_AMOUNT, [](float x) { return std::max(x, 3.0f); });
                change(v, voice::ACCENT, [](float x) { return std::max(x, 0.6f); });
                change(v, voice::DECAY, [](float x) { return std::clamp(x, 120.0f, 320.0f); });
            });
        } else {
            pick(ModuleE::VOICE, k + 1, { "Glass Arp", "Hollow Pulse", "Pluck Sequence", "Cosmic Drip", "Bright Stab", "Legato Glide", "Tape Sequence" },
                 nullptr, [&](ValuesT& v) { change(v, voice::ENV_AMOUNT, [](float x) { return std::max(x, 2.0f); }); });
        }
    }
    // The lead and the kit by style (Cosmic, Doom, Melodic, Modern, Drift).
    switch (c.m_style) {
    case StyleE::COSMIC: pick(ModuleE::LEAD, 0, { "Solo Saw", "Cosmic Siren", "Theremin", "Portamento", "Glass Whistle", "Twin Oscillator" }, nullptr, nullptr); break;
    case StyleE::DOOM: pick(ModuleE::LEAD, 0, { "Dusty Solo", "Warm Mono", "Hollow Oboe", "Theremin", "Soft Horn" }, nullptr, nullptr); break;
    case StyleE::MELODIC: pick(ModuleE::LEAD, 0, { "Singing Pulse", "Solo Saw", "Flute Lead", "Brass Lead", "Bell Lead", "Portamento" }, nullptr, nullptr); break;
    case StyleE::MODERN: pick(ModuleE::LEAD, 0, { "Screaming Filter", "Twin Oscillator", "Glass Whistle", "Singing Pulse", "Bell Lead" }, nullptr, nullptr); break;
    default: pick(ModuleE::LEAD, 0, { "Ethereal Sine", "Soft Horn", "Flute Lead", "Bell Lead", "Hollow Oboe" }, nullptr, nullptr); break;
    }
    pick(ModuleE::DRONE, 0, {}, nullptr, nullptr);
    // The tape keys: a group of the style's tape set (the choir, the strings, the flute), as the settings choose it.
    const int set = static_cast<int>(c.m_prof.m_tape);
    pick(ModuleE::TAPE, 0, {}, [set](const SoundPreset& s) {
        for (const auto& e : s.m_values) if (e.first == tape::SET) return static_cast<int>(std::lround(e.second)) == set;
        return false;
    }, nullptr);
    pick(ModuleE::STRINGS, 0, {}, nullptr, nullptr);
    switch (c.m_style) {
    case StyleE::MELODIC: case StyleE::MODERN:
        pick(ModuleE::DRUMS, 0, { "Tight Kit", "Punchy Kit", "Dry Machine", "Bright Machine", "Click Kit", "Snappy Kit" }, nullptr, nullptr); break;
    case StyleE::DOOM: pick(ModuleE::DRUMS, 0, { "Boom Kit", "Dark Machine", "Tribal Kit", "Metal Kit" }, nullptr, nullptr); break;
    default: pick(ModuleE::DRUMS, 0, { "Deep Kit", "Warm Analog", "Round Kit", "Soft Kit", "Sub Kit" }, nullptr, nullptr); break;
    }
    pick(ModuleE::ATMOS, 0, {}, nullptr, nullptr);
    // Wavetables on the counter rows after the main sequence (the style guide's 7.1: the counter sequence "thinner,
    // PWM, sync or wavetable" -- Stürtzer's and Quaeschning's Waldorfs, Redshift's PPG), the place in the table moved
    // from note to note by the row's modulation lane; in Modern now and then the main sequence as well. By style,
    // from draws of their own after all the others.
    {
        static const float Table[] = { 0.4f, 0.2f, 0.3f, 0.6f, 0.3f };   // Cosmic, Doom, Melodic, Modern, Drift
        static const char* const Choice[] = { "PPG 00", "PPG 03", "PPG 06", "PPG 07", "PPG 12", "PPG 17", "PPG 22", "PPG 25",
                                               "PPG Upper", "Sync", "Formant", "Glass", "Vocal", "FM Synth", "Overtones 1", "Morph 1" };
        const int si = std::clamp(static_cast<int>(c.m_style), 0, 4);
        Rng wt;
        wt.seed(mixSeed(c.m_seedOf(S_SOUNDS), 0x7774u));
        auto table = [&](int r) {
            const char* name = Choice[wt.below(static_cast<int>(sizeof(Choice) / sizeof(Choice[0])))];
            int index = 0;
            for (int i = 0; i < WavetableCount; ++i) if (std::string(WavetableNames[i]) == name) index = i;
            setKnob(c, ModuleE::VOICE, r, p.id(ModuleE::VOICE, r, voice::TABLE), static_cast<float>(index + 1));
            setKnob(c, ModuleE::VOICE, r, p.id(ModuleE::VOICE, r, voice::TABLE_POS), 0.1f + 0.6f * wt.uniform());
            setKnob(c, ModuleE::VOICE, r, p.id(ModuleE::VOICE, r, voice::TABLE_MOD), 0.3f + 0.5f * wt.uniform());
        };
        for (int k = 1; k < c.m_counters; ++k) if (wt.uniform() < Table[si]) table(k + 1);
        if (c.m_counters > 0 && c.m_style == StyleE::MODERN && wt.uniform() < 0.25f) table(1);
    }
    // The pad synth by style: analog and PPG planes in Cosmic, dark tubes and drones in Doom, the analog pad, Juno
    // strings and Oberheim brass in Melodic, the PPG and sync sweeps in Modern, air and overtones in Drift.
    switch (c.m_style) {
    case StyleE::COSMIC: pick(ModuleE::POLY, 0, { "Analog Pad", "PPG Choir", "Formant Pad", "Sync Sweep", "Glass Pad", "Overtone Pad" }, nullptr, nullptr); break;
    case StyleE::DOOM: pick(ModuleE::POLY, 0, { "Dark Drone Pad", "Tube Pad", "Bowed Pad", "Analog Pad", "Vocal Pad" }, nullptr, nullptr); break;
    case StyleE::MELODIC: pick(ModuleE::POLY, 0, { "Analog Pad", "Juno Strings", "Oberheim Brass", "Vocal Pad", "PPG Choir" }, nullptr, nullptr); break;
    case StyleE::MODERN: pick(ModuleE::POLY, 0, { "PPG Choir", "PPG Upper", "Sync Sweep", "Morph Pad", "Glass Pad", "Formant Pad" }, nullptr, nullptr); break;
    default: pick(ModuleE::POLY, 0, { "Sampled Air", "Overtone Pad", "Morph Pad", "Glass Pad", "Bowed Pad", "Single Cycle Pad" }, nullptr, nullptr); break;
    }
}

/**
 * @brief Step 5, the settings of the piece as steps at its start: tape set, hall, loudness.
 * @param[in,out] c  the piece
 */
void writeSettings(Piece& c)
{
    ParamStore& p = c.m_p;
    using GT = GestureShapeE;
    const int tapeSet = p.id(ModuleE::TAPE, 0, tape::SET);
    c.m_s.m_gestures.push_back({ tapeSet, 0.0, 0.0, 0.0f, offsetTo(p, tapeSet, static_cast<float>(c.m_prof.m_tape)), GT::STEP, 1 });
    const int hall = p.id(ModuleE::REVERB, 0, reverb::DECAY);
    c.m_s.m_gestures.push_back({ hall, 0.0, 0.0, 0.0f, offsetTo(p, hall, c.m_prof.m_hallSeconds), GT::STEP, 1 });
    const int level = p.id(ModuleE::MASTER, 0, master::LEVEL);
    c.m_s.m_gestures.push_back({ level, 0.0, 0.0, 0.0f, offsetTo(p, level, p.get(level) + c.m_prof.m_levelDb), GT::STEP, 1 });
    // The rooms of the style guide's 7.3: the bass dry, the pads and strings in the hall without echo.
    for (int id : { p.id(ModuleE::ROW, 0, row::ECHO_SEND), p.id(ModuleE::STRINGS, 0, strings::ECHO_SEND), p.id(ModuleE::TAPE, 0, tape::ECHO_SEND) })
        c.m_s.m_gestures.push_back({ id, 0.0, 0.0, 0.0f, offsetTo(p, id, 0.0f), GT::STEP, 1 });
    const int bassHall = p.id(ModuleE::ROW, 0, row::REVERB_SEND);
    c.m_s.m_gestures.push_back({ bassHall, 0.0, 0.0, 0.0f, offsetTo(p, bassHall, std::min(p.get(bassHall), 0.1f)), GT::STEP, 1 });
    // The stereo field (7.3): the bass in the middle, the main sequence near it, the other counter rows 30 to 50 % out
    // to alternating sides, the lead a little off the middle, the drone wandering slowly across. Drawn on the rows' unit.
    Rng pans;
    pans.seed(mixSeed(c.m_seedOf(S_ROWS), 0x70u));
    auto setTo = [&](int id, float v) { c.m_s.m_gestures.push_back({ id, 0.0, 0.0, 0.0f, offsetTo(p, id, v), GT::STEP, 1 }); };
    setTo(p.id(ModuleE::ROW, 0, row::PAN), 0.0f);
    const float side = pans.uniform() < 0.5f ? -1.0f : 1.0f;
    for (int k = 0; k < c.m_counters; ++k) {
        const float amount = k == 0 ? 0.1f * pans.uniform() : 0.3f + 0.2f * pans.uniform();
        setTo(p.id(ModuleE::ROW, k + 1, row::PAN), (k % 2 == 0 ? side : -side) * amount);
    }
    setTo(p.id(ModuleE::LEAD, 0, lead::PAN), -side * (0.1f + 0.1f * pans.uniform()));
    setTo(p.id(ModuleE::DRONE, 0, lead::PAN), 0.0f);   // (its auto pan, drone.auto_pan, lets it wander)
    // Each layer its own delay (4.4, 4.5): the main sequence on the tape echo's dotted eighths, the other counter
    // rows on the second echo in eighths or quarter triplets, with less of the first.
    const int e2time = p.id(ModuleE::ECHO2, 0, echo2::TIME);
    const float e2 = static_cast<float>(pans.uniform() < 0.6f ? EchoTimeE::EIGHTH : EchoTimeE::QUARTER_T);
    c.m_s.m_gestures.push_back({ e2time, 0.0, 0.0, 0.0f, offsetTo(p, e2time, e2), GT::STEP, 1 });
    for (int k = 1; k < c.m_counters; ++k) {
        setTo(p.id(ModuleE::ROW, k + 1, row::ECHO2_SEND), 0.35f);
        setTo(p.id(ModuleE::ROW, k + 1, row::ECHO_SEND), 0.1f);
    }
    // The frequency architecture of the production guide (4.1, 4.2): the bass row alone down to 30 Hz, the main
    // sequence from 90 Hz, the other counter rows from 200 Hz (the drone, the lead, the pads have their own defaults).
    setTo(p.id(ModuleE::ROW, 0, row::LOW_CUT), 30.0f);
    for (int k = 0; k < c.m_counters; ++k) setTo(p.id(ModuleE::ROW, k + 1, row::LOW_CUT), k == 0 ? 90.0f : 200.0f);
    // The hall's pre-delay a 64th at the piece's tempo (5.3: 32 ms at 118 BPM), which keeps the dry sounds in front.
    const int preDelay = p.id(ModuleE::REVERB, 0, reverb::PRE_DELAY);
    setTo(preDelay, static_cast<float>(60000.0 / (c.m_form.m_phaseBpm.front() * 16.0)));

    // Along the form, by the composer's own hand (3), apart from the player's two: a slow glide at the start of each
    // section towards the section's value (at most eight bars).
    auto alongForm = [&](int id, float start, const std::function<float(SectionTypeE)>& value) {
        float at = offsetTo(p, id, start);
        for (const Section& sec : c.m_form.m_sections) {
            const float to = offsetTo(p, id, value(sec.m_type));
            if (to == at) continue;
            c.m_s.m_gestures.push_back({ id, sec.m_beat, std::min(sec.m_length, 8.0 * BeatsPerBar), at, to, GT::MINIMUM_JERK, 3 });
            at = to;
        }
    };
    // The hall grows in the spaces and shrinks at the peak by 40 % (the style guide's 7.3, the production guide's 5.6).
    alongForm(hall, c.m_prof.m_hallSeconds, [&](SectionTypeE t) {
        switch (t) {
        case SectionTypeE::ATMO: case SectionTypeE::BRIDGE: return c.m_prof.m_hallSeconds * 1.3f;
        case SectionTypeE::CODA: return c.m_prof.m_hallSeconds * 1.4f;
        case SectionTypeE::PEAK: return c.m_prof.m_hallSeconds * 0.6f;
        case SectionTypeE::BREAKDOWN: return c.m_prof.m_hallSeconds * 1.1f;
        default: return c.m_prof.m_hallSeconds;
        }
    });
    // Automation instead of compression (7.2): the level follows the form, quieter spaces, the peak the loudest.
    const float base = p.get(level) + c.m_prof.m_levelDb;
    alongForm(level, base, [&](SectionTypeE t) {
        switch (t) {
        case SectionTypeE::ATMO: case SectionTypeE::BRIDGE: case SectionTypeE::CODA: return base - 3.0f;
        case SectionTypeE::ENTRY: case SectionTypeE::BREAKDOWN: return base - 1.5f;
        case SectionTypeE::BUILD: return base - 1.0f;
        case SectionTypeE::LEAD: return base - 0.5f;
        default: return base;
        }
    });
    // The width over the form (6.6): the widest in the spaces, narrow where the dry bass comes in, growing again.
    const int width = p.id(ModuleE::MASTER, 0, master::WIDTH);
    const float w0 = p.get(width);
    alongForm(width, w0, [&](SectionTypeE t) {
        switch (t) {
        case SectionTypeE::ATMO: case SectionTypeE::BRIDGE: case SectionTypeE::CODA: return w0 + 2.0f;
        case SectionTypeE::ENTRY: return w0 - 1.5f;
        case SectionTypeE::BUILD: return w0 - 0.5f;
        case SectionTypeE::PEAK: return w0 + 0.5f;
        case SectionTypeE::BREAKDOWN: return w0 + 1.0f;
        default: return w0;
        }
    });
    // The approach (the addon's 2): the bass comes in from afar over 45 s at each entry and recedes in the breakdown
    // over up to two minutes, and in the coda before it stops -- level, highs and hall together (row.distance).
    const int dist = p.id(ModuleE::ROW, 0, row::DISTANCE);
    const float far = offsetTo(p, dist, 0.7f), near = offsetTo(p, dist, 0.0f);
    for (int ph = 0; ph < c.m_phases; ++ph) {
        const double bpm = c.m_form.m_phaseBpm[static_cast<size_t>(ph)];
        if (const Section* entry = c.m_form.find(SectionTypeE::ENTRY, ph))
            c.m_s.m_gestures.push_back({ dist, entry->m_beat, std::min(entry->m_length, 45.0 * bpm / 60.0), far, near, GT::MINIMUM_JERK, 3 });
        if (const Section* bd = c.m_form.find(SectionTypeE::BREAKDOWN, ph))
            c.m_s.m_gestures.push_back({ dist, bd->m_beat, std::min(bd->m_length, 120.0 * bpm / 60.0), near, far, GT::MINIMUM_JERK, 3 });
    }
    if (const Section* coda = c.m_form.find(SectionTypeE::CODA, c.m_phases - 1)) {
        const double third = std::floor(coda->m_length / 3.0 / BeatsPerBar) * BeatsPerBar;
        if (c.m_prof.m_djOutro) {
            // The handover of a night set: where the next piece's bass comes in, this one's gives up its lows over
            // two bars, as a DJ swaps the bass on the EQ, and recedes to the end; where the next main sequence comes
            // in, this one's recedes.
            const double bars = std::floor(coda->m_length / BeatsPerBar);
            const double hb = coda->m_beat + (c.m_prof.m_bassHandover > 0 ? std::min<double>(c.m_prof.m_bassHandover, bars) : std::floor(bars / 2.0)) * BeatsPerBar;
            const double hm = std::max(hb, coda->m_beat + std::min<double>(c.m_prof.m_mainHandover, bars) * BeatsPerBar);
            const int cut = p.id(ModuleE::ROW, 0, row::LOW_CUT);
            if (c.m_form.m_lengthBeats > hb) {
                c.m_s.m_gestures.push_back({ cut, hb, 2.0 * BeatsPerBar, offsetTo(p, cut, 30.0f), offsetTo(p, cut, 250.0f), GT::MINIMUM_JERK, 3 });
                c.m_s.m_gestures.push_back({ dist, hb, c.m_form.m_lengthBeats - hb, near, far, GT::MINIMUM_JERK, 3 });
            }
            if (c.m_counters > 0 && c.m_form.m_lengthBeats > hm) {
                const int d1 = p.id(ModuleE::ROW, 1, row::DISTANCE);
                c.m_s.m_gestures.push_back({ d1, hm, c.m_form.m_lengthBeats - hm, offsetTo(p, d1, 0.0f), offsetTo(p, d1, 0.6f), GT::MINIMUM_JERK, 3 });
            }
        } else if (third > 0.0) {
            c.m_s.m_gestures.push_back({ dist, coda->m_beat, third, near, far, GT::MINIMUM_JERK, 3 });
        }
    }
    // In a break the echo's repeats carry on longer (its feedback up, back over a bar at the peak).
    const int feedback = p.id(ModuleE::ECHO, 0, echo::FEEDBACK);
    for (const auto& b : c.m_breaks) {
        c.m_s.m_gestures.push_back({ feedback, b.first, b.second - b.first, 0.0f, 0.2f, GT::EASE_OUT, 3 });
        c.m_s.m_gestures.push_back({ feedback, b.second, BeatsPerBar, 0.2f, 0.0f, GT::MINIMUM_JERK, 3 });
    }
    // The delay switched now and then (the style guide's 4.3: one to three times a piece): the tape echo on the
    // main sequence from its dotted eighths to eighths or quarter triplets for a section, and back. Its own stream.
    {
        Rng dl;
        dl.seed(mixSeed(c.m_seedOf(S_RACK), 0x646c79u));
        const int echoTime = p.id(ModuleE::ECHO, 0, echo::TIME);
        int left = 1 + dl.below(3);
        for (const Section& sec : c.m_form.m_sections) {
            if (left == 0) break;
            if (!(sec.m_type == SectionTypeE::LEAD || sec.m_type == SectionTypeE::PEAK || (sec.m_type == SectionTypeE::BUILD && sec.m_index > 0))) continue;
            if (dl.uniform() >= 0.45f) continue;
            const float to = static_cast<float>(dl.uniform() < 0.5f ? EchoTimeE::EIGHTH : EchoTimeE::QUARTER_T);
            if (static_cast<int>(to) == p.getInt(echoTime)) continue;
            const float off = offsetTo(p, echoTime, to);
            c.m_s.m_gestures.push_back({ echoTime, sec.m_beat, 0.0, 0.0f, off, GT::STEP, 3 });
            c.m_s.m_gestures.push_back({ echoTime, sec.m_beat + sec.m_length, 0.0, off, 0.0f, GT::STEP, 3 });
            --left;
        }
    }
    // The rooms per layer (the addon's 4, the production guide's 5.2): the second layer -- the counter rows, the lead,
    // the drums -- in the blend room with a little hall, the main sequence near it with less, the bass dry; the pads,
    // the drone and the atmosphere stay in the hall. A direct sound gets one room fully, never both.
    for (int k = 0; k < c.m_counters; ++k) {
        setTo(p.id(ModuleE::ROW, k + 1, row::BLEND_SEND), k == 0 ? 0.15f : 0.3f);
        setTo(p.id(ModuleE::ROW, k + 1, row::REVERB_SEND), 0.1f);
    }
    setTo(p.id(ModuleE::LEAD, 0, lead::BLEND_SEND), 0.35f);
    setTo(p.id(ModuleE::LEAD, 0, lead::REVERB_SEND), 0.25f);
    setTo(p.id(ModuleE::DRUMS, 0, drums::BLEND_SEND), 0.25f);
    setTo(p.id(ModuleE::DRUMS, 0, drums::REVERB_SEND), 0.05f);
    // Send A, the early reflections: distance and glue without a tail -- the second layer, a little of the main sequence.
    for (int k = 0; k < c.m_counters; ++k) setTo(p.id(ModuleE::ROW, k + 1, row::EARLY_SEND), k == 0 ? 0.1f : 0.25f);
    setTo(p.id(ModuleE::LEAD, 0, lead::EARLY_SEND), 0.2f);
    setTo(p.id(ModuleE::DRUMS, 0, drums::EARLY_SEND), 0.3f);
    // Send D, the effect hall: the drone, the atmosphere and the tape keys in the spaces and the transitions (the
    // atmosphere, the bridges, the coda), nothing while the machine runs.
    auto spaces = [](SectionTypeE t) { return t == SectionTypeE::ATMO || t == SectionTypeE::BRIDGE || t == SectionTypeE::CODA; };
    const std::pair<int, float> shimmerSends[3] = { { p.id(ModuleE::DRONE, 0, lead::SHIMMER_SEND), 0.2f },
                                                     { p.id(ModuleE::ATMOS, 0, atmos::SHIMMER_SEND), 0.35f },
                                                     { p.id(ModuleE::TAPE, 0, tape::SHIMMER_SEND), 0.15f } };
    for (const auto& [id, amount] : shimmerSends) {
        const float at = p.get(id), wet = amount;
        alongForm(id, at, [&, at, wet](SectionTypeE t) { return spaces(t) ? std::min(1.0f, at + wet) : at; });
    }
    // The sound of the sequences (25.09.2026, hypnosis): the main sequence squelchy -- more resonance, a deep and short
    // filter envelope, strong accents -- the bass round, the counter rows between; and their filter sweeps, slow sines
    // of their own period (the main sequence the widest), the Berlin School's long openings and closings.
    // With the composer's sounds on (compose.pick_sounds) every synth gets a factory preset of its part instead,
    // nudged to the same ends (writeSounds).
    const bool pick = p.getBool(p.id(ModuleE::COMPOSE, 0, compose::PICK_SOUNDS));
    auto voiceTo = [&](int r, int index, float v) { if (!pick) setTo(p.id(ModuleE::VOICE, r, index), v); };
    if (pick) writeSounds(c);
    voiceTo(0, voice::RESONANCE, 0.3f); voiceTo(0, voice::ENV_AMOUNT, 2.5f); voiceTo(0, voice::DECAY, 260.0f); voiceTo(0, voice::CUTOFF, 320.0f);
    voiceTo(0, voice::ACCENT, 0.6f);
    setTo(p.id(ModuleE::ROW, 0, row::SWEEP), 0.3f);
    for (int k = 0; k < c.m_counters; ++k) {
        const int r = k + 1;
        voiceTo(r, voice::RESONANCE, k == 0 ? 0.52f : 0.42f);
        voiceTo(r, voice::ENV_AMOUNT, k == 0 ? 3.6f : 3.0f);
        voiceTo(r, voice::DECAY, k == 0 ? 190.0f : 150.0f);
        voiceTo(r, voice::CUTOFF, k == 0 ? 450.0f : 700.0f);
        voiceTo(r, voice::ACCENT, k == 0 ? 0.75f : 0.55f);
        setTo(p.id(ModuleE::ROW, r, row::SWEEP), k == 0 ? 0.9f : 0.6f);
    }
    // The main sequence's attacks lifted, so it stays in front (the production guide's 7.5).
    if (c.m_counters > 0) setTo(p.id(ModuleE::ROW, 1, row::PUNCH), 0.5f);
    // The serial feed into the hall down to 5 % at the peak, where energy would pile up in the far room (the addon's 4).
    const int into = p.id(ModuleE::BLEND, 0, blend::INTO_HALL);
    const float into0 = p.get(into);
    alongForm(into, into0, [&](SectionTypeE t) { return t == SectionTypeE::PEAK ? 0.05f : into0; });
    // The foundation in pure intervals (the addon's 3): the bass row's two oscillators without detune.
    if (pick) setKnob(c, ModuleE::VOICE, 0, p.id(ModuleE::VOICE, 0, voice::DETUNE), 0.0f);
    else setTo(p.id(ModuleE::VOICE, 0, voice::DETUNE), 0.0f);
    // The pads step back 3 dB while the lead plays (7.2): its section and the peak after it.
    for (int ph = 0; ph < c.m_phases; ++ph) {
        const Section* lead = c.m_form.find(SectionTypeE::LEAD, ph);
        if (lead == nullptr) continue;
        const Section* peak = c.m_form.find(SectionTypeE::PEAK, ph);
        const double from = lead->m_beat, to = peak != nullptr ? peak->m_beat + peak->m_length : lead->m_beat + lead->m_length;
        for (int id : { p.id(ModuleE::TAPE, 0, tape::LEVEL), p.id(ModuleE::STRINGS, 0, strings::LEVEL), p.id(ModuleE::POLY, 0, poly::LEVEL) }) {
            const float at = offsetTo(p, id, p.get(id)), down = offsetTo(p, id, p.get(id) - 3.0f);
            c.m_s.m_gestures.push_back({ id, from, 2.0 * BeatsPerBar, at, down, GT::MINIMUM_JERK, 3 });
            c.m_s.m_gestures.push_back({ id, std::max(from + 2.0 * BeatsPerBar, to - 2.0 * BeatsPerBar), 2.0 * BeatsPerBar, down, at, GT::MINIMUM_JERK, 3 });
        }
    }
    // Timbre drift (the style guide's 4.3, "wave mix, PWM, sync, detune changed slowly -- all the time"; 0zk's one
    // slow modulation per layer): on every counter row one knob of its timbre wanders over the piece -- the place in
    // its wavetable, else the wave mix, or the pulse width where the pulse is heard -- and the pad synth's place in its
    // table; glides of one to three minutes around where the settings put the knob, by the composer's own hand. The
    // bass stays as it is, the ground. Their own stream on the sounds' unit.
    {
        Rng dr;
        dr.seed(mixSeed(c.m_seedOf(S_SOUNDS), 0x6472u));
        auto value = [&](int id) { return p.fromNormalised(id, std::clamp(p.toNormalised(id, p.get(id)) + c.m_s.gestureOffset(id, 0.0), 0.0f, 1.0f)); };
        auto wander = [&](int id, double from, float range) {
            if (from < 0.0) return;
            const float base = c.m_s.gestureOffset(id, 0.0);
            float at = base;
            for (double t = from; t < c.m_form.m_lengthBeats - 8.0 * BeatsPerBar;) {
                const double len = std::min((60.0 + 120.0 * dr.uniform()) * c.m_s.m_tempo.bpmAt(t) / 60.0, c.m_form.m_lengthBeats - t);
                const float to = std::clamp(base + range * (2.0f * dr.uniform() - 1.0f), -1.0f, 1.0f);
                c.m_s.m_gestures.push_back({ id, t, len, at, to, GT::MINIMUM_JERK, 3 });
                at = to;
                t += len;
            }
        };
        for (int k = 0; k < c.m_counters; ++k) {
            const int r = k + 1;
            const bool table = value(p.id(ModuleE::VOICE, r, voice::TABLE)) >= 0.5f;
            const bool pulse = value(p.id(ModuleE::VOICE, r, voice::WAVE)) >= 0.2f;
            const int id = table ? p.id(ModuleE::VOICE, r, voice::TABLE_POS)
                         : p.id(ModuleE::VOICE, r, pulse && dr.uniform() < 0.5f ? voice::PULSE_WIDTH : voice::WAVE);
            wander(id, c.m_rowFrom[static_cast<size_t>(r)], table ? 0.3f : 0.25f);
        }
        wander(p.id(ModuleE::POLY, 0, poly::POSITION), c.m_polyFrom, 0.25f);
    }
}

/**
 * @brief The events of the addon's 7: in the stages without a sequence -- the atmosphere, the bridges, the coda --
 *        now and then a near event, a lead fragment of one to three notes (root, fifth, third, octave of the scale
 *        at its beat), 20 to 90 s apart. It sets the ear's sense of depth anew; once the sequence runs, it is the
 *        reference. Their own stream, on the lead's unit.
 * @param[in,out] c  the piece
 */
void writeEvents(Piece& c)
{
    Rng ev;
    ev.seed(mixSeed(c.m_seedOf(S_LEAD), 0x65u));
    for (const Section& sec : c.m_form.m_sections) {
        if (sec.m_type != SectionTypeE::ATMO && sec.m_type != SectionTypeE::BRIDGE && sec.m_type != SectionTypeE::CODA) continue;
        const double bpm = c.m_s.m_tempo.bpmAt(sec.m_beat), secs = bpm / 60.0;
        const double to = sec.m_beat + (sec.m_type == SectionTypeE::CODA ? 0.7 : 0.9) * sec.m_length;
        for (double t = sec.m_beat + 0.1 * sec.m_length + (20.0 + 30.0 * ev.uniform()) * secs; t < to - 4.0; t += (20.0 + 70.0 * ev.uniform()) * secs) {
            const int root = pitchClass(c.m_key + c.m_s.rootAt(t));
            const int scale = c.m_s.scaleAt(t, c.m_scale);
            const int notes = 1 + ev.below(3);
            double b = std::floor(t);
            for (int k = 0; k < notes && b < to; ++k) {
                static const int Degrees[4] = { 0, 4, 2, 7 };
                int pitch = 60 + root + scaleSemitones(scale, Degrees[ev.below(4)]);
                while (pitch < 64) pitch += 12;
                while (pitch > 84) pitch -= 12;
                const double len = 1.5 + 1.5 * ev.uniform();
                c.m_s.m_notes.push_back({ b, len, PartE::LEAD, pitch, 0.6f + 0.12f * ev.uniform(), false, false });
                b += len + 0.5;
            }
        }
    }
}

/**
 * @brief Step 6, the hands on the knobs of what plays, following the form's energy.
 * @param[in,out] c  the piece
 */
void writeHands(Piece& c)
{
    ParamStore& p = c.m_p;
    const StyleProfile& prof = c.m_prof;
    const std::vector<double>& rowFrom = c.m_rowFrom;
    // A hand moves a knob around where the settings left it (a preset's cutoff, the main sequence's), not around the
    // bare knob: its range and centres are offsets from there.
    auto knob = [&](ModuleE m, int inst, int index, float low, float high, float rest, float peak, float weight, double from) {
        HandKnob k;
        k.m_param = p.id(m, inst, index);
        const float base = c.m_s.gestureOffset(k.m_param, 0.0);
        k.m_low = base + low; k.m_high = base + high;
        k.m_atRest = base + rest + prof.m_darkness; k.m_atPeak = base + peak + prof.m_darkness;
        k.m_weight = weight; k.m_from = std::max(0.0, from);
        return k;
    };
    std::vector<HandKnob> knobs;
    knobs.push_back(knob(ModuleE::VOICE, 0, voice::CUTOFF, -0.35f, 0.35f, -0.22f, 0.22f, 3.0f, rowFrom[0]));
    knobs.push_back(knob(ModuleE::VOICE, 0, voice::DECAY, -0.10f, 0.30f, 0.0f, 0.18f, 1.0f, rowFrom[0]));
    knobs.push_back(knob(ModuleE::VOICE, 0, voice::RESONANCE, -0.10f, 0.30f, 0.0f, 0.2f, 1.0f, rowFrom[0]));
    for (int k = 0; k < c.m_counters; ++k)
        if (rowFrom[static_cast<size_t>(k + 1)] >= 0.0)
            knobs.push_back(knob(ModuleE::VOICE, k + 1, voice::CUTOFF, -0.40f, 0.30f, -0.28f, 0.15f, 1.5f, rowFrom[static_cast<size_t>(k + 1)]));
    if (c.m_firstLead >= 0.0) knobs.push_back(knob(ModuleE::LEAD, 0, lead::CUTOFF, -0.30f, 0.30f, -0.10f, 0.20f, 1.0f, c.m_firstLead));
    knobs.push_back(knob(ModuleE::DRONE, 0, drone::CUTOFF, -0.20f, 0.30f, -0.10f, 0.15f, 0.8f, 0.0));
    knobs.push_back(knob(ModuleE::ATMOS, 0, atmos::WIND_TONE, -0.30f, 0.30f, 0.0f, 0.10f, 0.5f, 0.0));
    // The string machine's registration, a slow hand through its mixes (StringMachine.h), where it plays.
    if (c.m_stringsFrom >= 0.0) knobs.push_back(knob(ModuleE::STRINGS, 0, strings::REGISTRATION, 0.0f, 0.45f, 0.05f, 0.3f, 0.6f, c.m_stringsFrom));
    // The echo's feedback, thrown and caught: its home high where the energy is low and low at the peak (the style
    // guide's 6.3, U-shaped over the piece).
    HandKnob throwKnob = knob(ModuleE::ECHO, 0, echo::FEEDBACK, -0.15f, 0.4f, 0.0f, 0.05f, 0.6f, rowFrom[0]);
    throwKnob.m_atRest = 0.12f; throwKnob.m_atPeak = -0.08f;
    throwKnob.m_throws = true;
    throwKnob.m_scatter = 0.15f;
    knobs.push_back(throwKnob);
    Rng hands = c.stream(S_HANDS);
    const PieceForm& form = c.m_form;
    playHands(c.m_s, knobs, prof.m_hands, [&](double b) { return form.energy(b); }, 0.0, form.m_lengthBeats, hands);
}

} // namespace

Score composePiece(const ParamStore& params, uint64_t seed, double minutes, int keyShift,
                   const Curation* curation, const std::string& unit, const StyleProfile* profile)
{
    ParamStore p;
    p.copyValuesFrom(params);
    const StyleE style = static_cast<StyleE>(p.getInt(p.id(ModuleE::COMPOSE, 0, compose::STYLE)));
    // The profile: the one handed in (a concert's), else the user's own style where it is on, else compose.style's.
    StyleProfile own;
    if (profile == nullptr && customStyleOn(p)) { own = customProfile(p, styleProfile(style)); profile = &own; }
    const StyleProfile& prof = profile != nullptr ? *profile : styleProfile(style);
    // A unit's stream moves by its reroll counter; every other stream stays where it was.
    auto seedOf = [seed, curation, unit](StreamE k) {
        const int n = curation != nullptr ? curation->count(unit + UnitNames[k - 1]) : 0;
        return mixSeed(seed, static_cast<uint64_t>(k) + 131u * static_cast<uint64_t>(n));
    };
    auto stream = [&](StreamE k) { Rng r; r.seed(seedOf(k)); return r; };

    // Step 1: key, tempo, form.
    const int keyId = p.id(ModuleE::COMPOSE, 0, compose::KEY);
    const int key = pitchClass(p.getInt(keyId) + keyShift);
    p.set(keyId, static_cast<float>(key));
    double bpm = p.get(p.id(ModuleE::COMPOSE, 0, compose::BPM));
    if (p.getBool(p.id(ModuleE::COMPOSE, 0, compose::STYLE_TEMPO))) {
        Rng t = stream(S_TEMPO);
        bpm = std::round(prof.m_bpmLow + (prof.m_bpmHigh - prof.m_bpmLow) * t.uniform());
    }
    Rng formRng = stream(S_FORM);
    PieceForm form = drawForm(prof, minutes, bpm, formRng);

    Score s;
    s.clear(form.m_phaseBpm[0]);
    s.m_seed = seed;
    s.m_keyRoot = key;
    s.m_lengthBeats = form.m_lengthBeats;
    Piece c{ p, prof, style, s, form };
    c.m_key = key;
    c.m_phases = static_cast<int>(form.m_phaseBpm.size());
    c.m_seedOf = seedOf;
    setUpRows(c);         // step 2a
    // The builds end where the row they bring in meets the bass row again (Form.h).
    std::vector<double> cycles;
    for (int k = 0; k < c.m_counters; ++k) cycles.push_back(cycleBeats(p, k + 1));
    snapToConjunctions(form, cycleBeats(p, 0), cycles);
    writeMarkers(c);
    writeRack(c);         // step 2b
    writeLayers(c);       // step 3
    writeAtmosphere(c);   // step 4
    writeEvents(c);       // (the addon's events in the stages without a sequence)
    writeSettings(c);     // step 5
    writeHands(c);        // step 6
    settleMix(c);         // the mix's settings onto the faders
    // Where the piece's loudness is measured (Leveler.h): its peak, else its section of the most energy.
    {
        double peak = -1.0;
        float most = -1.0f;
        for (const Section& sec : form.m_sections) {
            if (sec.m_type == SectionTypeE::PEAK) { peak = sec.m_beat; break; }
            if (std::max(sec.m_e0, sec.m_e1) > most) { most = std::max(sec.m_e0, sec.m_e1); peak = sec.m_beat; }
        }
        s.m_levels.push_back({ 0.0, std::max(0.0, peak), prof.m_peakLufs, 0.0f });
    }
    s.sort();
    return s;
}

Score composeInterlude(const ParamStore& params, uint64_t seed, double minutes, int keyShift, const StyleProfile& profile)
{
    ParamStore p;
    p.copyValuesFrom(params);
    const StyleE style = static_cast<StyleE>(p.getInt(p.id(ModuleE::COMPOSE, 0, compose::STYLE)));
    const int keyId = p.id(ModuleE::COMPOSE, 0, compose::KEY);
    const int key = pitchClass(p.getInt(keyId) + keyShift);
    p.set(keyId, static_cast<float>(key));
    const int scale = p.getInt(p.id(ModuleE::COMPOSE, 0, compose::SCALE));
    StyleProfile prof = profile;
    prof.m_hallSeconds *= 1.4f;
    const double bpm = prof.m_bpmLow;
    // The form: the atmosphere, then a coda of a fifth of it; whole bars.
    const double bars = std::max(8.0, std::round(minutes * bpm / BeatsPerBar));
    const double codaBars = std::max(4.0, std::round(bars * 0.2));
    PieceForm form;
    form.m_sections.push_back({ SectionTypeE::ATMO, 0, 0, 0.0, (bars - codaBars) * BeatsPerBar, 0.1f, 0.25f });
    form.m_sections.push_back({ SectionTypeE::CODA, 0, 0, (bars - codaBars) * BeatsPerBar, codaBars * BeatsPerBar, 0.2f, 0.0f });
    form.m_phaseBpm = { bpm };
    form.m_phaseKey = { 0 };
    form.m_lengthBeats = bars * BeatsPerBar;

    Score s;
    s.clear(bpm);
    s.m_seed = seed;
    s.m_keyRoot = key;
    s.m_lengthBeats = form.m_lengthBeats;
    Piece c{ p, prof, style, s, form };
    c.m_key = key;
    c.m_phases = 1;
    c.m_scale = scale;
    c.m_rowFrom.assign(Rows, -1.0);
    c.m_seedOf = [seed](StreamE k) { return mixSeed(seed, static_cast<uint64_t>(k)); };
    writeMarkers(c);
    s.m_markers.front().m_text = "Zwischenspiel";
    s.m_rootShifts = { { 0.0, 0 } };
    s.m_scaleShifts = { { 0.0, scale } };
    const Section& atmo = form.m_sections[0];
    const Section& coda = form.m_sections[1];
    writeDrone(s, key, 0.0, coda.m_beat + coda.m_length * 0.9, &c.m_keys);
    // Slow chords, a chord every 16 to 32 bars (Drift's rhythm), on the string machine or the tape keys' choir.
    Rng pads = c.stream(S_PADS);
    PadPlan pp;
    pp.m_keyRoot = key;
    pp.m_scale = scale;
    pp.m_shifts = s.m_rootShifts;
    pp.m_scales = s.m_scaleShifts;
    if (pads.uniform() < 0.5f) {
        pp.m_part = PartE::STRINGS;
        pp.m_low = 62; pp.m_high = 81;
        pp.m_restrikeSeconds = 1e6;
        pp.m_choir = false;
    }
    const double from = 4.0 * BeatsPerBar;
    pp.m_chords = drawChordTrack(StyleE::DRIFT, scale, from, atmo.m_beat + atmo.m_length, pads);
    pp.m_chords.push_back({ atmo.m_beat + atmo.m_length, 0 });
    writeChords(s, pp, from, atmo.m_beat + atmo.m_length, pads);
    if (pp.m_part == PartE::STRINGS) c.m_stringsFrom = from;
    PadPlan fifth = pp;
    fifth.m_openFifth = true;
    writeChords(s, fifth, coda.m_beat, coda.m_beat + coda.m_length * 0.85, pads);
    c.m_grainsOn = true;
    writeAtmosphere(c);
    writeEvents(c);
    writeSettings(c);
    writeHands(c);
    settleMix(c);
    s.sort();
    return s;
}

namespace {

/**
 * @brief The parameters' registry (every store has the same ids), for what does not depend on the values.
 * @return a store with every parameter at its default
 */
const ParamStore& registry()
{
    static const ParamStore reg;
    return reg;
}

/**
 * @brief The rows' parameters (the modules with an instance per row, Row and Voice): the row of each id, and the id
 *        of the same parameter four rows away -- a night set's pieces take turns on rows 1-4 and 5-8.
 */
struct RowParams {
    std::vector<int> m_row;          ///< the row of a parameter id, -1 for every other parameter
    std::vector<int> m_other;        ///< the id of the same parameter on the row four away (itself for the others)
    std::vector<char> m_discrete;    ///< Int, Choice, Toggle: a knob that jumps, never glides
    std::vector<char> m_master;      ///< the master bus's knobs
    RowParams()
    {
        const ParamStore& p = registry();
        const size_t n = static_cast<size_t>(p.count());
        m_row.assign(n, -1);
        m_other.resize(n);
        m_discrete.assign(n, 0);
        m_master.assign(n, 0);
        for (int i = 0; i < ParamStore::moduleCount(ModuleE::MASTER); ++i) m_master[static_cast<size_t>(p.id(ModuleE::MASTER, 0, i))] = 1;
        for (int id = 0; id < p.count(); ++id) {
            m_other[static_cast<size_t>(id)] = id;
            const CurveE cv = p.desc(id).m_curve;
            m_discrete[static_cast<size_t>(id)] = cv == CurveE::INT || cv == CurveE::CHOICE || cv == CurveE::TOGGLE;
        }
        for (ModuleE m : { ModuleE::ROW, ModuleE::VOICE })
            for (int r = 0; r < Rows; ++r)
                for (int i = 0; i < ParamStore::moduleCount(m); ++i) {
                    const size_t id = static_cast<size_t>(p.id(m, r, i));
                    m_row[id] = r;
                    m_other[id] = p.id(m, (r + Rows / 2) % Rows, i);
                }
    }
};

/**
 * @brief The rows' parameter ids, looked up once.
 * @return the rows' parameter ids
 */
const RowParams& rowParams()
{
    static const RowParams rp;
    return rp;
}

/**
 * @brief Swaps the knobs of rows 1-4 and 5-8: a piece composed on rows 1-4 is then set against the knobs it will play
 *        on.
 * @param[in,out] p  the parameters
 */
void swapBanks(ParamStore& p)
{
    for (ModuleE m : { ModuleE::ROW, ModuleE::VOICE })
        for (int r = 0; r < Rows / 2; ++r)
            for (int i = 0; i < ParamStore::moduleCount(m); ++i) {
                const int a = p.id(m, r, i), b = p.id(m, r + Rows / 2, i);
                const float va = p.get(a);
                p.set(a, p.get(b));
                p.set(b, va);
            }
}

/**
 * @brief Moves a piece from rows 1-4 to rows 5-8 (its transposer from row 8 to row 4): notes, knobs, rack, shapes.
 * @param[in,out] s  the piece's score
 */
void toOtherBank(Score& s)
{
    const RowParams& rp = rowParams();
    for (NoteEvent& n : s.m_notes) {
        const int r = static_cast<int>(n.m_part) - static_cast<int>(PartE::ROW1);
        if (r >= 0 && r < Rows) n.m_part = rowPart((r + Rows / 2) % Rows);
    }
    for (Gesture& g : s.m_gestures)
        if (g.m_param >= 0 && g.m_param < static_cast<int>(rp.m_other.size())) g.m_param = rp.m_other[static_cast<size_t>(g.m_param)];
    for (RackEvent& e : s.m_rack) if (e.m_row >= 0) e.m_row = (e.m_row + Rows / 2) % Rows;
    for (RowShape& r : s.m_rowShapes) r.m_row = (r.m_row + Rows / 2) % Rows;
    for (SoundPick& k : s.m_sounds) if (k.m_module == static_cast<int>(ModuleE::VOICE)) k.m_instance = (k.m_instance + Rows / 2) % Rows;
    for (KnobSet& k : s.m_knobs) {
        if (k.m_param >= 0 && k.m_param < static_cast<int>(rp.m_other.size())) k.m_param = rp.m_other[static_cast<size_t>(k.m_param)];
        if (k.m_module == static_cast<int>(ModuleE::VOICE)) k.m_instance = (k.m_instance + Rows / 2) % Rows;
    }
}

/**
 * @brief Lays @p src into @p dst, @p overlap beats before dst's end (0: at its end); src's roots moved by
 *        @p rootOffset semitones.
 *
 * A knob src never sets but dst moved goes back to where src expects it -- the knob itself -- where src begins (a
 * piece of a concert used to keep the last one's granular cloud, say). With an overlap (a night set: two tracks in a
 * DJ's mix) src plays over dst's outro on the other bank of rows (@p srcBank, 0 or 1; -1: no banks); dst's other
 * layers end where src begins, its drone held half a beat into src's first note and sliding into it; the knobs src
 * owns (all but those of dst's rows) are src's from there on, its first settings gliding over up to eight bars from
 * where dst left them; the tempo stays dst's to dst's end, then ramps to src's (16 bars for every 4 BPM). The master
 * bus (its level and width along the form) stays dst's through the overlap as well, and glides at dst's end to where
 * src has it by then: a DJ's mix keeps its level while one track goes and the other comes.
 * @param[in,out] dst         the score laid into
 * @param[in]     src         the score laid in
 * @param[in]     overlap     the beats before dst's end at which src begins, 0 for its end
 * @param[in]     rootOffset  semitones src's roots are moved by
 * @param[in]     srcBank     the bank of rows src plays on
 */
void mixInto(Score& dst, const Score& src, double overlap, int rootOffset, int srcBank)
{
    using GT = GestureShapeE;
    constexpr double Eps = 1e-9;
    const RowParams& rp = rowParams();
    const int n = registry().count();
    const double end = dst.m_lengthBeats, at = std::max(0.0, end - overlap);
    const bool mixing = overlap > 0.0;
    auto owned = [&](int id) {
        const int r = rp.m_row[static_cast<size_t>(id)];
        return srcBank < 0 || r < 0 || r / (Rows / 2) == srcBank;
    };
    auto valid = [n](int id) { return id >= 0 && id < n; };
    bool drone = false;
    if (mixing) {
        // dst's layers other than the rows end where src begins: what starts later goes, what sounds on is cut two
        // beats in; the drone holds on half a beat and slides into src's first note.
        std::vector<NoteEvent> kept;
        kept.reserve(dst.m_notes.size());
        size_t last = 0;
        for (const NoteEvent& note : dst.m_notes) {
            const int r = static_cast<int>(note.m_part) - static_cast<int>(PartE::ROW1);
            if (r >= 0 && r < Rows) { kept.push_back(note); continue; }
            if (note.m_beat >= at - Eps) continue;
            kept.push_back(note);
            NoteEvent& k = kept.back();
            if (k.m_part == PartE::DRONE) {
                if (k.m_beat + k.m_length > at - 0.5) { k.m_length = at + 0.5 - k.m_beat; last = kept.size(); drone = true; }
            } else if (k.m_beat + k.m_length > at + 2.0) {
                k.m_length = at + 2.0 - k.m_beat;
            }
        }
        if (drone) kept[last - 1].m_slide = true;
        dst.m_notes.swap(kept);
        // dst's moves of src's knobs from there on go; the roots and the scale are src's.
        dst.m_gestures.erase(std::remove_if(dst.m_gestures.begin(), dst.m_gestures.end(),
                                          [&](const Gesture& g) { return g.m_beat >= at - Eps && valid(g.m_param) && owned(g.m_param); }),
                           dst.m_gestures.end());
        auto cut = [&](std::vector<std::pair<double, int>>& v) {
            v.erase(std::remove_if(v.begin(), v.end(), [&](const std::pair<double, int>& e) { return e.first >= at - Eps; }), v.end());
        };
        cut(dst.m_rootShifts);
        cut(dst.m_scaleShifts);
    }
    // Where dst leaves each of src's knobs: its latest move (in the order the engine reads them).
    std::vector<int> lastOf(static_cast<size_t>(n), -1);
    for (size_t i = 0; i < dst.m_gestures.size(); ++i) {
        const Gesture& g = dst.m_gestures[i];
        if (valid(g.m_param) && g.m_beat <= at + Eps && owned(g.m_param)) lastOf[static_cast<size_t>(g.m_param)] = static_cast<int>(i);
    }
    auto leftAt = [&](int id) { const int i = lastOf[static_cast<size_t>(id)]; return i < 0 ? 0.0f : gestureValue(dst.m_gestures[static_cast<size_t>(i)], at); };
    // src's settings at its start (the last move at beat 0 on each knob) and its next move on each knob.
    std::vector<int> atZero(static_cast<size_t>(n), -1);
    std::vector<double> next(static_cast<size_t>(n), 1e300);
    for (size_t i = 0; i < src.m_gestures.size(); ++i) {
        const Gesture& g = src.m_gestures[i];
        if (!valid(g.m_param)) continue;
        if (g.m_beat <= Eps) atZero[static_cast<size_t>(g.m_param)] = static_cast<int>(i);
        else next[static_cast<size_t>(g.m_param)] = std::min(next[static_cast<size_t>(g.m_param)], g.m_beat);
    }
    const double glideBeats = 8.0 * BeatsPerBar;
    std::vector<Gesture> added;
    added.reserve(src.m_gestures.size() + 64);
    // The master bus in a mix: dst's to its end, then a glide to where src has it at that point.
    auto held = [&](int id) { return mixing && rp.m_master[static_cast<size_t>(id)] != 0; };
    if (mixing) {
        for (int i = 0; i < ParamStore::moduleCount(ModuleE::MASTER); ++i) {
            const int id = registry().id(ModuleE::MASTER, 0, i);
            bool seen = lastOf[static_cast<size_t>(id)] >= 0;
            double after = 1e300;
            for (const Gesture& g : src.m_gestures) {
                if (g.m_param != id) continue;
                if (g.m_beat < overlap - Eps) seen = true;
                else after = std::min(after, g.m_beat - overlap);
            }
            if (!seen) continue;
            const float from = leftAt(id), to = src.gestureOffset(id, overlap);
            if (from == to) continue;
            const bool glide = !rp.m_discrete[static_cast<size_t>(id)];
            added.push_back({ id, end, glide ? std::min(glideBeats, after) : 0.0, from, to, glide ? GT::MINIMUM_JERK : GT::STEP, 3 });
        }
    }
    // The knobs dst moved and src does not set: back to the knob itself.
    for (int id = 0; id < n; ++id) {
        const int i = lastOf[static_cast<size_t>(id)];
        if (i < 0 || atZero[static_cast<size_t>(id)] >= 0 || held(id)) continue;
        const Gesture& g = dst.m_gestures[static_cast<size_t>(i)];
        const float v = gestureValue(g, at);
        const bool moving = g.m_shape != GT::STEP && g.m_beat + g.m_length > at + Eps;
        if (v == 0.0f && !moving) continue;
        const bool glide = mixing && !rp.m_discrete[static_cast<size_t>(id)];
        added.push_back({ id, at, glide ? std::min(glideBeats, next[static_cast<size_t>(id)]) : 0.0, v, 0.0f,
                          glide ? GT::MINIMUM_JERK : GT::STEP, 3 });
    }
    for (size_t i = 0; i < src.m_gestures.size(); ++i) {
        Gesture g = src.m_gestures[i];
        if (valid(g.m_param) && held(g.m_param) && g.m_beat < overlap - Eps) continue;   // (the master bus, above)
        if (mixing && valid(g.m_param) && atZero[static_cast<size_t>(g.m_param)] == static_cast<int>(i)) {
            // src's setting of the knob starts from where dst left it, and glides there over up to eight bars.
            g.m_from = leftAt(g.m_param);
            if ((g.m_shape == GT::STEP || g.m_length <= 0.0) && !rp.m_discrete[static_cast<size_t>(g.m_param)] && g.m_from != g.m_to) {
                g.m_shape = GT::MINIMUM_JERK;
                g.m_length = std::min(glideBeats, next[static_cast<size_t>(g.m_param)]);
            }
        }
        g.m_beat += at;
        added.push_back(g);
    }
    if (drone) {
        // The drone's slide into the new key, slow: its glide opened a beat before and closed four bars after.
        const int glide = registry().id(ModuleE::DRONE, 0, lead::GLIDE);
        added.push_back({ glide, at - 1.0, 0.0, 0.0f, 1.0f, GT::STEP, 3 });
        added.push_back({ glide, at + 4.0 * BeatsPerBar, 0.0, 1.0f, 0.0f, GT::STEP, 3 });
    }
    dst.m_gestures.insert(dst.m_gestures.end(), added.begin(), added.end());
    // The tempo: without an overlap src's own from its start; with one dst's to its end, then a ramp to src's, 16
    // bars for every 4 BPM (16 to 64 bars): the drift of a set, not a jump.
    if (!mixing) {
        for (const TempoPoint& tp : src.m_tempo.points()) dst.m_tempo.add(at + tp.m_beat, tp.m_bpm, tp.m_rampToNext);
    } else {
        const double from = dst.m_tempo.bpmAt(end), to = src.m_tempo.bpmAt(0.0);
        const double rampEnd = end + std::clamp(std::ceil(std::abs(from - to) / 4.0) * 16.0, 16.0, 64.0) * BeatsPerBar;
        if (std::abs(from - to) > 1e-6) {
            dst.m_tempo.add(end, from, true);
            dst.m_tempo.add(rampEnd, to, false);
        }
        for (const TempoPoint& tp : src.m_tempo.points())
            if (at + tp.m_beat > rampEnd + Eps) dst.m_tempo.add(at + tp.m_beat, tp.m_bpm, tp.m_rampToNext);
    }
    for (NoteEvent note : src.m_notes) { note.m_beat += at; dst.m_notes.push_back(note); }
    for (RackEvent e : src.m_rack) { e.m_beat += at; dst.m_rack.push_back(e); }
    for (Marker m : src.m_markers) { m.m_beat += at; dst.m_markers.push_back(m); }
    for (const auto& r : src.m_rootShifts) dst.m_rootShifts.push_back({ at + r.first, r.second + rootOffset });
    for (const auto& r : src.m_scaleShifts) dst.m_scaleShifts.push_back({ at + r.first, r.second });
    for (RowShape r : src.m_rowShapes) {
        // src's transposer lies in dst's bank: a display shows it there from dst's end.
        r.m_from = mixing && r.m_transposer ? std::max(r.m_from + at, end) : r.m_from + at;
        dst.m_rowShapes.push_back(r);
    }
    std::stable_sort(dst.m_rowShapes.begin(), dst.m_rowShapes.end(), [](const RowShape& a, const RowShape& b) { return a.m_from < b.m_from; });
    for (SoundPick k : src.m_sounds) { k.m_beat += at; dst.m_sounds.push_back(k); }
    for (KnobSet k : src.m_knobs) { k.m_beat += at; dst.m_knobs.push_back(k); }
    for (LevelMark m : src.m_levels) { m.m_beat += at; m.m_peakBeat += at; dst.m_levels.push_back(m); }
    dst.m_lengthBeats = at + src.m_lengthBeats;
    dst.sort();
}

} // namespace

void appendScore(Score& dst, const Score& src, int rootOffset)
{
    mixInto(dst, src, 0.0, rootOffset, -1);
}

Score composeConcert(const ParamStore& p, uint64_t seed, double minutes, const Curation* curation)
{
    if (p.getBool(p.id(ModuleE::COMPOSE, 0, compose::NIGHT_SET))) return composeNightSet(p, seed, minutes, curation);
    const int styleId = p.id(ModuleE::COMPOSE, 0, compose::STYLE);
    const StyleE from = static_cast<StyleE>(p.getInt(styleId));
    const int morph = p.getInt(p.id(ModuleE::COMPOSE, 0, compose::MORPH_TO)) - 1;
    const bool morphing = morph >= 0 && morph < static_cast<int>(StyleE::COUNT) && morph != static_cast<int>(from);
    const float arc = p.get(p.id(ModuleE::COMPOSE, 0, compose::CONCERT_ARC));
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
    const bool album = p.getBool(p.id(ModuleE::COMPOSE, 0, compose::ALBUM));
    const int scaleId = p.id(ModuleE::COMPOSE, 0, compose::SCALE);
    const int baseScale = p.getInt(scaleId);
    bool middleDone = false;
    int interludes = 0;
    for (int i = 0; elapsed < minutes * 60.0 - 90.0 && i < 64; ++i) {
        if (shaped) {
            // The profile at the share of the concert already played: between the two styles (the nearer one's
            // drums), then along the arc of tension.
            const float t = static_cast<float>(elapsed / (minutes * 60.0));
            prof = morphing ? morphProfile(start, styleProfile(static_cast<StyleE>(morph)), t) : start;
            if (morphing) q.set(styleId, static_cast<float>(t < 0.5f ? static_cast<int>(from) : morph));
            prof = arcProfile(prof, concertArc(t) - 0.5f, arc);
        }
        const double left = minutes - elapsed / 60.0;
        double m = prof.m_minutesLow + (prof.m_minutesHigh - prof.m_minutesLow) * r.uniform();
        if (left - m < prof.m_minutesLow * 0.6) m = left;   // no short piece at the end: the last takes the rest
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
                own.m_darkness -= 0.12f;
                own.m_peakRows = std::min(Rows - 1, own.m_peakRows + 1);
            } else if (last && i > 0) {
                if (baseScale == 0 || baseScale == 2) q.set(scaleId, 1.0f);
                own.m_hallSeconds *= 1.3f;
                own.m_peakRows = std::max(1, own.m_peakRows - 1);
                own.m_drumsChance = 0.0f;
                own.m_leadIntensity *= 0.7f;
                own.m_darkness += 0.05f;
            }
        }
        Score piece = composePiece(q, mixSeed(seed, 1000 + static_cast<uint64_t>(i)), m, shift, curation,
                                   "piece" + std::to_string(i + 1) + ".", album ? &own : (shaped ? &prof : nullptr));
        for (Marker& mk : piece.m_markers) mk.m_text = "Stueck " + std::to_string(i + 1) + ": " + mk.m_text;
        if (i == 0) { out = piece; out.m_rootShifts.clear(); out.m_scaleShifts.clear(); out.m_lengthBeats = 0.0; out.m_notes.clear(); out.m_gestures.clear();
                      out.m_rack.clear(); out.m_markers.clear(); out.m_rowShapes.clear(); out.m_sounds.clear();
                      out.m_knobs.clear(); out.m_levels.clear(); }
        const double before = out.m_tempo.secondsAt(out.m_lengthBeats);
        appendScore(out, piece, shift);
        elapsed += out.m_tempo.secondsAt(out.m_lengthBeats) - before;
        // An interlude of three to five minutes between two long pieces, in the key of the one before.
        if (album && !last) {
            const double im = 3.0 + 2.0 * r.uniform();
            if (minutes * 60.0 - elapsed - im * 60.0 > prof.m_minutesLow * 60.0 * 0.8) {
                q.set(scaleId, static_cast<float>(baseScale));
                Score inter = composeInterlude(q, mixSeed(seed, 5000 + static_cast<uint64_t>(i)), im, shift, prof);
                ++interludes;
                for (Marker& mk : inter.m_markers)
                    mk.m_text = "Zwischenspiel " + std::to_string(interludes) + (mk.m_text == "Zwischenspiel" ? std::string() : ": " + mk.m_text);
                const double b0 = out.m_tempo.secondsAt(out.m_lengthBeats);
                appendScore(out, inter, shift);
                elapsed += out.m_tempo.secondsAt(out.m_lengthBeats) - b0;
            }
        }
        // The next key: a fourth, a fifth, the relative, a tone.
        const int moves[5] = { 5, -5, 3, -2, 2 };
        shift += moves[r.below(5)];
        shift = pitchClass(shift + 6) - 6;
    }
    return out;
}

Score composeNightSet(const ParamStore& p, uint64_t seed, double minutes, const Curation* curation)
{
    const int styleId = p.id(ModuleE::COMPOSE, 0, compose::STYLE);
    const int bpmId = p.id(ModuleE::COMPOSE, 0, compose::BPM);
    const int tempoId = p.id(ModuleE::COMPOSE, 0, compose::STYLE_TEMPO);
    // The styles from the calmest to the most driving (tempo, pulse, density): the ladder the night climbs and
    // descends, a rung at a time, so that two neighbours are near in tempo and in energy.
    static const StyleE Ladder[5] = { StyleE::DRIFT, StyleE::DOOM, StyleE::COSMIC, StyleE::MODERN, StyleE::MELODIC };
    const StyleE first = static_cast<StyleE>(std::clamp(p.getInt(styleId), 0, 4));
    int rung = 0;
    for (int k = 0; k < 5; ++k) if (Ladder[k] == first) rung = k;
    Rng r;
    r.seed(mixSeed(seed, 0x4e5u + 131u * static_cast<uint64_t>(curation != nullptr ? curation->count("night") : 0)));
    const double total = std::max(30.0, minutes) * 60.0;
    // The waves of the style guide's 6.3: the energy rises and falls over 80 to 120 minutes, on the night's own arc.
    const double period = (80.0 + 40.0 * static_cast<double>(r.uniform())) * 60.0;
    Score out;
    int shift = 0, overlapIn = 0, introIn = 0, entryIn = 0, run = 1;
    double elapsed = 0.0, endBpm = 120.0;
    for (int i = 0; i < 160 && elapsed < total - 60.0; ++i) {
        const float t = static_cast<float>(elapsed / total);
        if (i > 0) {
            const double wave = 0.5 - 0.5 * std::cos(6.283185307179586 * elapsed / period);
            const double target = 4.0 * (0.55 * static_cast<double>(concertArc(t)) + 0.45 * wave) + static_cast<double>(r.uniform()) - 0.5;
            const int want = std::clamp(static_cast<int>(std::lround(target)), 0, 4);
            int step = std::clamp(want - rung, -1, 1);
            // Never a third piece of one style in a row.
            if (step == 0 && run >= 2) step = rung == 0 ? 1 : (rung == 4 ? -1 : (r.uniform() < 0.5f ? 1 : -1));
            run = step == 0 ? run + 1 : 1;
            rung += step;
        }
        // The length: eight to fifteen minutes; no short piece at the end, the last one takes the rest.
        const double inSeconds = overlapIn * BeatsPerBar * 60.0 / endBpm;
        const double left = (total - elapsed + inSeconds) / 60.0;
        double m = 8.0 + 7.0 * static_cast<double>(r.uniform());
        if (left - m < 6.0) m = left;
        m = std::max(6.0, m);
        const bool last = m >= left - 1e-9;
        // The mix into the next piece: its intro of 8, 12 or 16 bars, its bass alone for 8, then its main sequence
        // filling up for 16 more over this one's outro -- 32 to 40 bars together.
        const int introNext = 8 + 4 * r.below(3), entryNext = 8;
        const int overlapOut = last ? 0 : introNext + entryNext + 16;
        const StyleE style = Ladder[rung];
        StyleProfile prof = arcProfile(styleProfile(style), concertArc(t) - 0.5f, 0.5f);
        prof.m_peakRows = std::min(prof.m_peakRows, Rows / 2);   // one bank of rows: the neighbour plays on the other
        // The DJ's gain: Doom and Drift, measured 2.3 dB under the others, half of that nearer to them.
        if (style == StyleE::DOOM || style == StyleE::DRIFT) prof.m_levelDb += 1.2f;
        prof.m_introBars = introIn;                            // over the last one's outro
        prof.m_entryBars = introIn > 0 ? entryIn : 0;
        prof.m_codaBars = overlapOut;
        prof.m_djOutro = overlapOut > 0;
        prof.m_bassHandover = introNext;
        prof.m_mainHandover = introNext + entryNext;
        ParamStore q;
        q.copyValuesFrom(p);
        q.set(styleId, static_cast<float>(style));
        if (i > 0) {
            // The tempo the last piece ended in, a little faster or slower, within the style's range: the mix is
            // beat-matched, and a style of another range ramps there after the handover.
            q.set(tempoId, 0.0f);
            q.set(bpmId, static_cast<float>(std::clamp(std::round(endBpm - 2.0 + 6.0 * static_cast<double>(r.uniform())),
                                                       static_cast<double>(prof.m_bpmLow) - 6.0, static_cast<double>(prof.m_bpmHigh) + 6.0)));
        }
        const int bank = i % 2;
        if (bank == 1) swapBanks(q);
        Score piece = composePiece(q, mixSeed(seed, 1000 + static_cast<uint64_t>(i)), m, shift, curation,
                                   "piece" + std::to_string(i + 1) + ".", &prof);
        if (bank == 1) toOtherBank(piece);
        for (Marker& mk : piece.m_markers) mk.m_text = "Stueck " + std::to_string(i + 1) + " (" + prof.m_name + "): " + mk.m_text;
        if (i == 0) { out = piece; out.m_rootShifts.clear(); out.m_scaleShifts.clear(); out.m_lengthBeats = 0.0; out.m_notes.clear(); out.m_gestures.clear();
                      out.m_rack.clear(); out.m_markers.clear(); out.m_rowShapes.clear(); out.m_sounds.clear();
                      out.m_knobs.clear(); out.m_levels.clear(); }
        mixInto(out, piece, overlapIn * BeatsPerBar, shift, bank);
        elapsed = out.m_tempo.secondsAt(out.m_lengthBeats);
        endBpm = out.m_tempo.bpmAt(out.m_lengthBeats);
        overlapIn = overlapOut;
        introIn = introNext;
        entryIn = entryNext;
        if (last) break;
        // The next key, as a DJ mixes harmonically: a fourth or a fifth, now and then a minor third.
        const int moves[6] = { 5, -5, 5, -5, 3, -3 };
        shift = pitchClass(shift + moves[r.below(6)] + 6) - 6;
    }
    return out;
}

} // namespace eph
