/**
 * @file Pads.cpp
 * @brief Held chords with the least movement.
 */
#include "eph/compose/Pads.h"
#include "eph/compose/Harmony.h"
#include "eph/Rack.h"
#include <algorithm>
#include <cmath>

namespace eph {

void writeDrone(Score& s, int keyRoot, double from, double to, const std::vector<std::pair<double, int>>* roots)
{
    const std::vector<std::pair<double, int>>& r = roots != nullptr ? *roots : s.rootShifts;
    for (size_t i = 0; i < r.size(); ++i) {
        const double b0 = std::max(from, r[i].first);
        const double b1 = std::min(to, i + 1 < r.size() ? r[i + 1].first : to);
        if (b1 <= b0 + 1.0) continue;
        int pitch = 45 + ((keyRoot - 9 + 12) % 12) + r[i].second;
        while (pitch > 52) pitch -= 12;
        while (pitch < 40) pitch += 12;
        s.notes.push_back({ b0, b1 - b0 - 0.05, Part::Drone, pitch, 0.8f, false, false });
    }
}

int writeChords(Score& score, const PadPlan& plan, double from, double to, Rng& rng)
{
    // The span in segments: a new one wherever the root, the chord or the scale changes.
    std::vector<double> seg = { from };
    for (const auto* list : { &plan.shifts, &plan.chords, &plan.scales })
        for (const auto& e : *list) if (e.first > from && e.first < to) seg.push_back(e.first);
    std::sort(seg.begin(), seg.end());
    seg.erase(std::unique(seg.begin(), seg.end()), seg.end());

    std::vector<int> voices;   // the sounding voicing, low to high
    int struck = 0;
    for (size_t i = 0; i < seg.size(); ++i) {
        const double s0 = seg[i], s1 = i + 1 < seg.size() ? seg[i + 1] : to;
        const int shift = rootShiftAt(plan.shifts, s0);
        const int degree = plan.openFifth ? 0 : rootShiftAt(plan.chords, s0);
        const int scale = plan.scales.empty() ? plan.scale : rootShiftAt(plan.scales, s0);
        // The chord: on the chord track's degree of the scale on the transposer's root. The transposer moves
        // the whole sequence in parallel -- a row in A minor transposed to E plays in E minor -- so the chord is
        // built the way the rows are, not diatonically in the home key.
        const int base = plan.keyRoot + shift;
        const ChordKind kind = plan.openFifth ? ChordKind::Fifth : drawChordKind(scale, degree, plan.choir, rng);
        std::vector<int> tones;
        for (int t : chordTones(scale, degree, kind)) tones.push_back(pitchClass(base + t));
        // A doubled tone (the open fifth's octave) only where the register holds two of it.
        for (size_t a = 1; a < tones.size(); ++a) {
            const bool doubled = std::find(tones.begin(), tones.begin() + static_cast<std::ptrdiff_t>(a), tones[a]) != tones.begin() + static_cast<std::ptrdiff_t>(a);
            int room = 0;
            for (int p = plan.low; p <= plan.high; ++p) room += p % 12 == tones[a] ? 1 : 0;
            if (doubled && room < 2) tones.erase(tones.begin() + static_cast<std::ptrdiff_t>(a--));
        }
        // Voicing with the least movement: every combination of octaves inside the register is tried (a
        // few dozen at most) and the one kept whose voices lie nearest to those of the last chord, with a
        // small pull towards the middle of the register and a penalty for a spread wider than a tenth.
        const int mid = (plan.low + plan.high) / 2;
        std::vector<std::vector<int>> options;
        for (int pc : tones) {
            std::vector<int> o;
            for (int p = plan.low; p <= plan.high; ++p) if (p % 12 == pc) o.push_back(p);
            if (!o.empty()) options.push_back(o);
        }
        std::vector<int> next, pick(options.size(), 0);
        double bestCost = 1e30;
        for (;;) {
            std::vector<int> v;
            for (size_t t = 0; t < options.size(); ++t) v.push_back(options[t][static_cast<size_t>(pick[t])]);
            std::sort(v.begin(), v.end());
            if (std::adjacent_find(v.begin(), v.end()) == v.end()) {
                double cost = 0.0, mean = 0.0;
                for (int x : v) {
                    mean += x;
                    if (!voices.empty()) {
                        int d = 1000;
                        for (int y : voices) d = std::min(d, std::abs(x - y));
                        cost += d;
                    }
                }
                mean /= static_cast<double>(v.size());
                cost += 0.15 * std::fabs(mean - mid) + (v.back() - v.front() > 16 ? 4.0 : 0.0);
                if (cost < bestCost) { bestCost = cost; next = v; }
            }
            size_t t = 0;
            while (t < pick.size() && ++pick[t] == static_cast<int>(options[t].size())) pick[t++] = 0;
            if (t == pick.size()) break;
        }
        voices = next;
        // Strike, and strike again before the tape runs out.
        for (double t = s0; t < s1 - 0.25;) {
            const double maxBeats = plan.restrikeSeconds * score.tempo.bpmAt(t) / 60.0;
            const double end = std::min(s1, t + maxBeats);
            for (size_t v = 0; v < voices.size(); ++v) {
                NoteEvent e;
                e.part = plan.part;
                e.beat = t + 0.02 * static_cast<double>(rng.uniform());   // fingers, a few ms apart
                e.length = std::max(0.25, end - e.beat - 0.05);
                e.pitch = voices[v];
                e.velocity = 0.7f + 0.1f * rng.uniform();
                score.notes.push_back(e);
            }
            ++struck;
            t = end;
        }
    }
    return struck;
}

} // namespace eph
