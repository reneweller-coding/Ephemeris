/**
 * @file Pads.cpp
 * @brief Held chords with the least movement.
 */
#include "eph/Pads.h"
#include "eph/Rack.h"
#include <algorithm>
#include <cmath>

namespace eph {

int writeChords(Score& score, const PadPlan& plan, double from, double to, Rng& rng)
{
    // The roots in the span, as segments.
    std::vector<std::pair<double, int>> seg;
    int root = 0;
    for (const auto& e : plan.shifts) if (e.first <= from) root = e.second;
    seg.push_back({ from, root });
    for (const auto& e : plan.shifts)
        if (e.first > from && e.first < to) seg.push_back(e);

    std::vector<int> voices;   // the sounding voicing, low to high
    int struck = 0;
    for (size_t i = 0; i < seg.size(); ++i) {
        const double s0 = seg[i].first, s1 = i + 1 < seg.size() ? seg[i + 1].first : to;
        const int shift = seg[i].second;
        // The chord: the triad of the key's scale built on the transposer's root. The transposer moves the
        // whole sequence in parallel -- a row in A minor transposed to F plays in F minor -- so the chord is
        // built the way the rows are, not diatonically in the home key.
        const int base = plan.keyRoot + shift;
        std::vector<int> pcs = { 0, 2, 4 };
        if (rng.uniform() < plan.colour) pcs.push_back(rng.uniform() < 0.5f ? 6 : 8);
        std::vector<int> tones;
        for (int d : pcs) tones.push_back(((base + scaleSemitones(plan.scale, d)) % 12 + 12) % 12);
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
