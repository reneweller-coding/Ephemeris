/**
 * @file GestureEngine.cpp
 * @brief The player's hands.
 */
#include "eph/compose/GestureEngine.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {

double gaussian(Rng& rng) { return rng.gaussian(); }

} // namespace

void playHands(Score& score, const std::vector<HandKnob>& knobs, const HandStyle& style,
               const std::function<float(double)>& energy, double start, double end, Rng& rng)
{
    if (knobs.empty() || end <= start) return;
    std::vector<float> value(knobs.size(), 0.0f);        // where each knob was left
    std::vector<double> busyUntil(knobs.size(), start);  // beat until which a hand holds it
    // Each knob starts at its resting centre, set once and silently (a step at the start).
    for (size_t k = 0; k < knobs.size(); ++k) {
        value[k] = std::clamp(knobs[k].atRest, knobs[k].low, knobs[k].high);
        score.gestures.push_back({ knobs[k].param, start, 0.0, value[k], value[k], GestureShape::Step, 0 });
    }
    double freeAt[2] = { start + 2.0, start + 6.0 };     // the second hand joins a little later
    const auto beatsOf = [&](double beat, double seconds) { return seconds * score.tempo.bpmAt(beat) / 60.0; };

    for (int guard = 0; guard < 100000; ++guard) {
        const int h = freeAt[0] <= freeAt[1] ? 0 : 1;
        const double t = freeAt[h];
        if (t >= end) break;
        // Choose a knob nobody holds and that may be touched now.
        float total = 0.0f;
        for (size_t k = 0; k < knobs.size(); ++k)
            if (busyUntil[k] <= t && knobs[k].from <= t) total += knobs[k].weight;
        if (total <= 0.0f) { freeAt[h] = t + beatsOf(t, 1.0); continue; }
        float u = rng.uniform() * total;
        size_t pick = 0;
        for (size_t k = 0; k < knobs.size(); ++k) {
            if (busyUntil[k] > t || knobs[k].from > t) continue;
            pick = k;
            u -= knobs[k].weight;
            if (u <= 0.0f) break;
        }
        const HandKnob& knob = knobs[pick];

        // How long, and which shape.
        double seconds;
        GestureShape shape = GestureShape::MinimumJerk;
        const float kind = rng.uniform();
        if (knob.throws || kind < style.quickChance) {
            seconds = 1.0 + rng.uniform();
            shape = GestureShape::EaseOut;
        } else if (kind < style.quickChance + style.longChance) {
            seconds = style.medianSeconds * (4.0 + 6.0 * rng.uniform());
        } else {
            seconds = style.medianSeconds * std::exp(style.spread * gaussian(rng));
            if (rng.uniform() < 0.2f) shape = GestureShape::EaseIn;
        }
        double length = beatsOf(t, seconds);
        length = std::min(length, end - t);
        if (length < 0.25) break;

        // Where to: the energy's centre with scatter; a throw goes up and is caught back after it.
        const float e = std::clamp(energy(t + length), 0.0f, 1.0f);
        const float centre = knob.atRest + e * (knob.atPeak - knob.atRest);
        float target = centre + knob.scatter * static_cast<float>(gaussian(rng));
        if (knob.throws) target = std::max(value[pick], centre) + knob.scatter * (1.0f + rng.uniform());
        target = std::clamp(target, knob.low, knob.high);
        score.gestures.push_back({ knob.param, t, length, value[pick], target, shape, static_cast<uint8_t>(h) });
        value[pick] = target;
        double done = t + length;
        if (knob.throws) {
            // Caught: back to the centre over two to four seconds, by the same hand.
            const double back = std::min(beatsOf(done, 2.0 + 2.0 * rng.uniform()), end - done);
            if (back > 0.25) {
                const float home = std::clamp(centre, knob.low, knob.high);
                score.gestures.push_back({ knob.param, done, back, target, home, GestureShape::MinimumJerk, static_cast<uint8_t>(h) });
                value[pick] = home;
                done += back;
            }
        }
        busyUntil[pick] = done;
        freeAt[h] = done + beatsOf(done, style.restSeconds * std::exp(0.6 * gaussian(rng)));
    }
}

} // namespace eph
