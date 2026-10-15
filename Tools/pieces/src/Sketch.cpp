/**
 * @file Sketch.cpp
 * @brief The sketch of Phase 2.
 * @author Rene Weller
 */
#include "eph/Sketch.h"
#include "eph/compose/GestureEngine.h"
#include "eph/compose/Lead.h"
#include "eph/compose/Pads.h"
#include "eph/Rack.h"
#include <algorithm>
#include <cmath>

namespace eph {

float sketchEnergy(double f)
{
    f = std::clamp(f, 0.0, 1.0);
    // A smooth rise to the peak at 0.65 and a fall to the end (smoothstep on both sides).
    auto smooth = [](double x) { x = std::clamp(x, 0.0, 1.0); return x * x * (3.0 - 2.0 * x); };
    const double e = f < 0.65 ? 0.15 + 0.85 * smooth(f / 0.65) : 1.0 - 0.9 * smooth((f - 0.65) / 0.35);
    return static_cast<float>(e);
}

Score buildSketch(const ParamStore& params, uint64_t seed, double minutes)
{
    // The rows of the sketch: its own settings on top of the user's, in a private copy.
    ParamStore p;
    p.copyValuesFrom(params);
    p.parseText("row1.active=0 row1.length=16 row1.division=1/16 row1.octave=-1 row1.mutation=0.15 row1.pan=0\n"
                "row2.active=0 row2.length=13 row2.division=1/16 row2.octave=0 row2.mutation=0.25 row2.pan=-0.45\n"
                "row3.active=0 row3.length=12 row3.division=1/8 row3.octave=1 row3.mutation=0.3 row3.pan=0.45 row3.gate=40\n"
                "row4.active=0 row4.length=8 row4.division=4 Bars row4.mode=Transposer row4.mutation=0.3\n");

    const double bpm = p.get(p.id(ModuleE::COMPOSE, 0, compose::BPM));
    Score s;
    s.clear(bpm);
    s.m_seed = seed;
    s.m_keyRoot = p.getInt(p.id(ModuleE::COMPOSE, 0, compose::KEY));
    const int bars = std::max(64, static_cast<int>(std::lround(std::max(4.0, minutes) * bpm / BeatsPerBar)));
    s.m_lengthBeats = bars * BeatsPerBar;
    auto at = [&](double f) { return std::floor(f * bars / 4.0 + 0.5) * 4.0 * BeatsPerBar; };   // on four-bar lines

    s.m_rack.push_back({ at(0.06), 0, RackOpE::START, 0 });   // after the cosmic intro
    s.m_rack.push_back({ at(0.12), 1, RackOpE::START, 0 });
    s.m_rack.push_back({ at(0.20), 3, RackOpE::START, 0 });
    s.m_rack.push_back({ at(0.30), 2, RackOpE::START, 0 });
    s.m_rack.push_back({ at(0.85), 3, RackOpE::STOP, 0 });
    s.m_rack.push_back({ at(0.88), 2, RackOpE::STOP, 0 });
    s.m_rack.push_back({ at(0.95), 1, RackOpE::STOP, 0 });
    s.m_markers = { { 0.0, "Atmo" }, { at(0.06), "Einsatz" }, { at(0.12), "Aufbau" }, { at(0.20), "Transposer" }, { at(0.40), "Lead" },
                  { at(0.62), "Hoehepunkt" }, { at(0.85), "Ausklang" } };

    Rack rack;
    rack.setup(p, seed);
    rack.generate(0, RowRoleE::BASS);
    rack.generate(1, RowRoleE::COUNTER);
    rack.generate(2, RowRoleE::WALK);
    rack.generate(3, RowRoleE::TRANSPOSER);
    s.sort();
    rack.run(s, s.m_lengthBeats);
    s.m_rootShifts = rack.shifts();

    // The drone: the root in the second octave, held through each root, from the first bar to the end.
    writeDrone(s, rack.keyRoot(), 0.0, at(0.97));

    // The atmosphere: wind swells in the intro and recedes under the sequence, comes back for the coda;
    // sweeps in the intro and the coda, bleeps in the middle. Steps and slow moves of knobs that start
    // at nothing (Params.cpp: every layer off).
    const int wind = p.id(ModuleE::ATMOS, 0, atmos::WIND), sweeps = p.id(ModuleE::ATMOS, 0, atmos::SWEEPS);
    const int bleeps = p.id(ModuleE::ATMOS, 0, atmos::BLEEPS);
    using GT = GestureShapeE;
    s.m_gestures.push_back({ wind, 0.0, at(0.05), 0.0f, 0.62f, GT::MINIMUM_JERK, 0 });
    s.m_gestures.push_back({ wind, at(0.07), at(0.15) - at(0.07), 0.62f, 0.35f, GT::MINIMUM_JERK, 0 });
    s.m_gestures.push_back({ wind, at(0.88), at(0.98) - at(0.88), 0.35f, 0.6f, GT::MINIMUM_JERK, 0 });
    s.m_gestures.push_back({ sweeps, 0.0, 0.0, 0.0f, 0.3f, GT::STEP, 1 });
    s.m_gestures.push_back({ sweeps, at(0.12), 0.0, 0.3f, 0.0f, GT::STEP, 1 });
    s.m_gestures.push_back({ sweeps, at(0.88), 0.0, 0.0f, 0.3f, GT::STEP, 1 });
    s.m_gestures.push_back({ bleeps, at(0.30), 0.0, 0.0f, 0.2f, GT::STEP, 1 });
    s.m_gestures.push_back({ bleeps, at(0.80), 0.0, 0.2f, 0.0f, GT::STEP, 1 });

    // The lead, in two passages, the second denser; it follows the transposer's roots.
    Rng leadRng;
    leadRng.seed(mixSeed(seed, 300));
    LeadPlan lp;
    lp.m_keyRoot = rack.keyRoot();
    lp.m_scale = rack.scale();
    lp.m_shifts = rack.shifts();
    lp.m_intensity = 0.35f;
    writeLead(s, lp, at(0.40), at(0.58), leadRng);
    lp.m_intensity = 0.8f;
    writeLead(s, lp, at(0.60), at(0.80), leadRng);

    // The tape keys: the choir from about a fifth of the way, the strings tape from the peak on (the
    // player switches the tape set, a step on its knob), chords following the transposer.
    Rng padRng;
    padRng.seed(mixSeed(seed, 500));
    PadPlan pp;
    pp.m_keyRoot = rack.keyRoot();
    pp.m_scale = rack.scale();
    pp.m_shifts = rack.shifts();
    writeChords(s, pp, at(0.22), at(0.92), padRng);
    s.m_gestures.push_back({ p.id(ModuleE::TAPE, 0, tape::SET), at(0.62), 0.0, 0.0f, 0.5f, GestureShapeE::STEP, 1 });
    s.m_markers.push_back({ at(0.22), "Tape Keys" });
    // The string machine above them from the middle to the close, held through each root.
    PadPlan sp = pp;
    sp.m_part = PartE::STRINGS;
    sp.m_low = 62;
    sp.m_high = 81;
    sp.m_restrikeSeconds = 1e6;
    sp.m_choir = false;
    writeChords(s, sp, at(0.46), at(0.88), padRng);

    // The hands.
    auto knob = [&](ModuleE m, int inst, int index, float low, float high, float rest, float peak, float weight, double from) {
        HandKnob k;
        k.m_param = p.id(m, inst, index);
        k.m_low = low; k.m_high = high; k.m_atRest = rest; k.m_atPeak = peak; k.m_weight = weight; k.m_from = from;
        return k;
    };
    std::vector<HandKnob> knobs = {
        knob(ModuleE::VOICE, 0, voice::CUTOFF,    -0.35f, 0.35f, -0.22f, 0.22f, 3.0f, 0.0),
        knob(ModuleE::VOICE, 0, voice::DECAY,     -0.10f, 0.30f,  0.00f, 0.18f, 1.0f, 0.0),
        knob(ModuleE::VOICE, 0, voice::RESONANCE, -0.10f, 0.30f,  0.00f, 0.20f, 1.0f, 0.0),
        knob(ModuleE::VOICE, 1, voice::CUTOFF,    -0.40f, 0.30f, -0.30f, 0.15f, 2.0f, at(0.12)),
        knob(ModuleE::VOICE, 2, voice::CUTOFF,    -0.40f, 0.30f, -0.25f, 0.15f, 1.5f, at(0.30)),
        knob(ModuleE::LEAD, 0, lead::CUTOFF,      -0.30f, 0.30f, -0.10f, 0.20f, 1.0f, at(0.40)),
        knob(ModuleE::DRONE, 0, drone::CUTOFF,    -0.20f, 0.30f, -0.10f, 0.15f, 1.0f, 0.0),
        knob(ModuleE::ATMOS, 0, atmos::WIND_TONE,  -0.30f, 0.30f,  0.00f, 0.10f, 0.5f, 0.0),
    };
    HandKnob throwKnob = knob(ModuleE::ECHO, 0, echo::FEEDBACK, 0.0f, 0.4f, 0.0f, 0.05f, 0.6f, at(0.10));
    throwKnob.m_throws = true;
    throwKnob.m_scatter = 0.15f;
    knobs.push_back(throwKnob);
    Rng hands;
    hands.seed(mixSeed(seed, 400));
    const double len = s.m_lengthBeats;
    playHands(s, knobs, HandStyle{}, [&](double b) { return sketchEnergy(b / len); }, 0.0, len, hands);
    s.sort();
    return s;
}

} // namespace eph
