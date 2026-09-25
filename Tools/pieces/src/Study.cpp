/**
 * @file Study.cpp
 * @brief The study of Phase 1.
 */
#include "eph/Study.h"
#include "eph/Rack.h"
#include <algorithm>
#include <cmath>

namespace eph {

Score buildStudy(const ParamStore& p, uint64_t seed, double minutes)
{
    const double bpm = p.get(p.id(Module::Compose, 0, compose::Bpm));
    Score s;
    s.clear(bpm);
    s.seed = seed;
    s.keyRoot = p.getInt(p.id(Module::Compose, 0, compose::Key));
    const int bars = std::max(32, static_cast<int>(std::lround(minutes * bpm / kBeatsPerBar)));
    s.lengthBeats = bars * kBeatsPerBar;
    // A fraction of the piece, on a bar line, in beats.
    auto at = [&](double f) { return std::floor(f * bars + 0.5) * kBeatsPerBar; };
    auto span = [&](double f0, double f1) { return std::max(4.0, at(f1) - at(f0)); };

    const int cutoff1 = p.id(Module::Voice, 0, voice::Cutoff);
    const int decay1 = p.id(Module::Voice, 0, voice::Decay);
    const int reso1 = p.id(Module::Voice, 0, voice::Resonance);
    const int cutoff2 = p.id(Module::Voice, 1, voice::Cutoff);
    const int feedback = p.id(Module::Echo, 0, echo::Feedback);
    using G = GestureShape;
    auto hand = [&](int param, double f0, double f1, float from, float to, G shape, int h) {
        s.gestures.push_back({ param, at(f0), span(f0, f1), from, to, shape, static_cast<uint8_t>(h) });
    };

    // Hand 0 on the bass row's filter, hand 1 on whatever joins it.
    hand(cutoff1, 0.00, 0.03, -0.30f, -0.30f, G::Step, 0);          // closed at the start
    hand(cutoff1, 0.03, 0.25, -0.30f, 0.10f, G::MinimumJerk, 0);    // the long opening
    s.rack.push_back({ at(0.11), 1, RackOp::Start, 0 });
    hand(cutoff2, 0.00, 0.11, -0.40f, -0.40f, G::Step, 1);
    hand(cutoff2, 0.11, 0.27, -0.40f, 0.00f, G::MinimumJerk, 1);    // the counter row surfaces
    hand(decay1, 0.30, 0.38, 0.00f, 0.25f, G::MinimumJerk, 1);      // staccato into legato ...
    hand(decay1, 0.41, 0.46, 0.25f, 0.00f, G::MinimumJerk, 1);      // ... and back
    hand(feedback, 0.43, 0.44, 0.00f, 0.35f, G::EaseOut, 0);        // an echo thrown ...
    hand(feedback, 0.45, 0.48, 0.35f, 0.00f, G::MinimumJerk, 0);    // ... and caught
    hand(cutoff1, 0.49, 0.59, 0.10f, -0.08f, G::MinimumJerk, 0);    // the dip before the peak
    hand(cutoff1, 0.60, 0.70, -0.08f, 0.22f, G::MinimumJerk, 0);    // the peak
    hand(reso1, 0.60, 0.70, 0.00f, 0.20f, G::MinimumJerk, 1);
    hand(reso1, 0.76, 0.84, 0.20f, 0.00f, G::MinimumJerk, 1);
    hand(cutoff1, 0.84, 0.97, 0.22f, -0.35f, G::MinimumJerk, 0);    // the close
    hand(cutoff2, 0.86, 0.95, 0.00f, -0.45f, G::MinimumJerk, 1);
    s.rack.push_back({ at(0.95), 1, RackOp::Stop, 0 });

    // The transposer: a pedal first, then i - i - bVI - bVII every eight bars, home for the close.
    const int cycle[4] = { 0, 0, -4, -2 };
    int k = 0;
    for (double b = at(0.22); b < at(0.84); b += 8.0 * kBeatsPerBar, ++k)
        s.rack.push_back({ b, -1, RackOp::Transpose, cycle[k % 4] });
    s.rack.push_back({ at(0.84), -1, RackOp::Transpose, 0 });

    s.markers = { { 0.0, "Einsatz" }, { at(0.11), "Aufbau" }, { at(0.22), "Transposition" },
                  { at(0.60), "Hoehepunkt" }, { at(0.84), "Ausklang" } };

    Rack rack;
    rack.setup(p, seed);
    rack.generate(0, RowRole::Bass);
    rack.generate(1, RowRole::Counter);
    s.sort();
    rack.run(s, s.lengthBeats);
    s.sort();
    return s;
}

} // namespace eph
