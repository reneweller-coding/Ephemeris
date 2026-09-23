/**
 * @file Sketch.cpp
 * @brief The sketch of Phase 2.
 */
#include "eph/Sketch.h"
#include "eph/GestureEngine.h"
#include "eph/Lead.h"
#include "eph/Pads.h"
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

    const double bpm = p.get(p.id(Module::Compose, 0, compose::Bpm));
    Score s;
    s.clear(bpm);
    s.seed = seed;
    s.keyRoot = p.getInt(p.id(Module::Compose, 0, compose::Key));
    const int bars = std::max(64, static_cast<int>(std::lround(std::max(4.0, minutes) * bpm / kBeatsPerBar)));
    s.lengthBeats = bars * kBeatsPerBar;
    auto at = [&](double f) { return std::floor(f * bars / 4.0 + 0.5) * 4.0 * kBeatsPerBar; };   // on four-bar lines

    s.rack.push_back({ at(0.06), 0, RackOp::Start, 0 });   // after the cosmic intro
    s.rack.push_back({ at(0.12), 1, RackOp::Start, 0 });
    s.rack.push_back({ at(0.20), 3, RackOp::Start, 0 });
    s.rack.push_back({ at(0.30), 2, RackOp::Start, 0 });
    s.rack.push_back({ at(0.85), 3, RackOp::Stop, 0 });
    s.rack.push_back({ at(0.88), 2, RackOp::Stop, 0 });
    s.rack.push_back({ at(0.95), 1, RackOp::Stop, 0 });
    s.markers = { { 0.0, "Atmo" }, { at(0.06), "Einsatz" }, { at(0.12), "Aufbau" }, { at(0.20), "Transposer" }, { at(0.40), "Lead" },
                  { at(0.62), "Hoehepunkt" }, { at(0.85), "Ausklang" } };

    Rack rack;
    rack.setup(p, seed);
    rack.generate(0, RowRole::Bass);
    rack.generate(1, RowRole::Counter);
    rack.generate(2, RowRole::Walk);
    rack.generate(3, RowRole::Transposer);
    s.sort();
    rack.run(s, s.lengthBeats);
    s.rootShifts = rack.shifts();

    // The drone: the root in the second octave, held through each root, from the first bar to the end.
    for (size_t i = 0; i < s.rootShifts.size(); ++i) {
        const double b0 = std::max(0.0, s.rootShifts[i].first);
        const double b1 = i + 1 < s.rootShifts.size() ? s.rootShifts[i + 1].first : at(0.97);
        if (b1 <= b0 + 1.0 || b0 >= at(0.97)) continue;
        int pitch = 45 + ((rack.keyRoot() - 9 + 12) % 12) + s.rootShifts[i].second;
        while (pitch > 52) pitch -= 12;
        while (pitch < 40) pitch += 12;
        s.notes.push_back({ b0, b1 - b0 - 0.05, Part::Drone, pitch, 0.8f, false, false });
    }

    // The atmosphere: wind swells in the intro and recedes under the sequence, comes back for the coda;
    // sweeps in the intro and the coda, bleeps in the middle. Steps and slow moves of knobs that start
    // at nothing (Params.cpp: every layer off).
    const int wind = p.id(Module::Atmos, 0, atmos::Wind), sweeps = p.id(Module::Atmos, 0, atmos::Sweeps);
    const int bleeps = p.id(Module::Atmos, 0, atmos::Bleeps);
    using G = GestureShape;
    s.gestures.push_back({ wind, 0.0, at(0.05), 0.0f, 0.62f, G::MinimumJerk, 0 });
    s.gestures.push_back({ wind, at(0.07), at(0.15) - at(0.07), 0.62f, 0.35f, G::MinimumJerk, 0 });
    s.gestures.push_back({ wind, at(0.88), at(0.98) - at(0.88), 0.35f, 0.6f, G::MinimumJerk, 0 });
    s.gestures.push_back({ sweeps, 0.0, 0.0, 0.0f, 0.3f, G::Step, 1 });
    s.gestures.push_back({ sweeps, at(0.12), 0.0, 0.3f, 0.0f, G::Step, 1 });
    s.gestures.push_back({ sweeps, at(0.88), 0.0, 0.0f, 0.3f, G::Step, 1 });
    s.gestures.push_back({ bleeps, at(0.30), 0.0, 0.0f, 0.2f, G::Step, 1 });
    s.gestures.push_back({ bleeps, at(0.80), 0.0, 0.2f, 0.0f, G::Step, 1 });

    // The lead, in two passages, the second denser; it follows the transposer's roots.
    Rng leadRng;
    leadRng.seed(mixSeed(seed, 300));
    LeadPlan lp;
    lp.keyRoot = rack.keyRoot();
    lp.scale = rack.scale();
    lp.shifts = rack.shifts();
    lp.intensity = 0.35f;
    writeLead(s, lp, at(0.40), at(0.58), leadRng);
    lp.intensity = 0.8f;
    writeLead(s, lp, at(0.60), at(0.80), leadRng);

    // The tape keys: the choir from about a fifth of the way, the strings tape from the peak on (the
    // player switches the tape set, a step on its knob), chords following the transposer.
    Rng padRng;
    padRng.seed(mixSeed(seed, 500));
    PadPlan pp;
    pp.keyRoot = rack.keyRoot();
    pp.scale = rack.scale();
    pp.shifts = rack.shifts();
    writeChords(s, pp, at(0.22), at(0.92), padRng);
    s.gestures.push_back({ p.id(Module::Tape, 0, tape::Set), at(0.62), 0.0, 0.0f, 0.5f, GestureShape::Step, 1 });
    s.markers.push_back({ at(0.22), "Tape Keys" });

    // The hands.
    auto knob = [&](Module m, int inst, int index, float low, float high, float rest, float peak, float weight, double from) {
        HandKnob k;
        k.param = p.id(m, inst, index);
        k.low = low; k.high = high; k.atRest = rest; k.atPeak = peak; k.weight = weight; k.from = from;
        return k;
    };
    std::vector<HandKnob> knobs = {
        knob(Module::Voice, 0, voice::Cutoff,    -0.35f, 0.35f, -0.22f, 0.22f, 3.0f, 0.0),
        knob(Module::Voice, 0, voice::Decay,     -0.10f, 0.30f,  0.00f, 0.18f, 1.0f, 0.0),
        knob(Module::Voice, 0, voice::Resonance, -0.10f, 0.30f,  0.00f, 0.20f, 1.0f, 0.0),
        knob(Module::Voice, 1, voice::Cutoff,    -0.40f, 0.30f, -0.30f, 0.15f, 2.0f, at(0.12)),
        knob(Module::Voice, 2, voice::Cutoff,    -0.40f, 0.30f, -0.25f, 0.15f, 1.5f, at(0.30)),
        knob(Module::Lead, 0, lead::Cutoff,      -0.30f, 0.30f, -0.10f, 0.20f, 1.0f, at(0.40)),
        knob(Module::Drone, 0, drone::Cutoff,    -0.20f, 0.30f, -0.10f, 0.15f, 1.0f, 0.0),
        knob(Module::Atmos, 0, atmos::WindTone,  -0.30f, 0.30f,  0.00f, 0.10f, 0.5f, 0.0),
    };
    HandKnob throwKnob = knob(Module::Echo, 0, echo::Feedback, 0.0f, 0.4f, 0.0f, 0.05f, 0.6f, at(0.10));
    throwKnob.throws = true;
    throwKnob.scatter = 0.15f;
    knobs.push_back(throwKnob);
    Rng hands;
    hands.seed(mixSeed(seed, 400));
    const double len = s.lengthBeats;
    playHands(s, knobs, HandStyle{}, [&](double b) { return sketchEnergy(b / len); }, 0.0, len, hands);
    s.sort();
    return s;
}

} // namespace eph
