/**
 * @file Harmony.cpp
 * @brief Progressions of the transposer.
 */
#include "eph/Harmony.h"
#include <algorithm>

namespace eph {

namespace {

struct StyleHarmony {
    std::vector<int> roots;      ///< the moves away from the tonic
    std::vector<float> weights;  ///< their weights
    float stay;                  ///< chance that a step stays where it is
};

const StyleHarmony& harmonyOf(Style style)
{
    static const StyleHarmony kStyles[] = {
        { { -4, -2, 3, 5 },     { 0.35f, 0.35f, 0.15f, 0.15f },         0.45f },   // Cosmic
        { { 1, -4, 6, -1 },     { 0.35f, 0.30f, 0.20f, 0.15f },         0.50f },   // Doom
        { { -4, -2, 3, 5, 7 },  { 0.25f, 0.25f, 0.2f, 0.15f, 0.15f },   0.25f },   // Melodic
        { { -4, -2, 3, 5, 2 },  { 0.25f, 0.25f, 0.2f, 0.15f, 0.15f },   0.30f },   // Modern
        { { -2, 5 },            { 0.6f, 0.4f },                         0.65f },   // Drift
    };
    const int i = std::clamp(static_cast<int>(style), 0, static_cast<int>(Style::Count) - 1);
    return kStyles[i];
}

} // namespace

const std::vector<int>& progressionRoots(Style style) { return harmonyOf(style).roots; }

std::vector<int> drawProgression(Style style, int steps, Rng& rng)
{
    const StyleHarmony& h = harmonyOf(style);
    std::vector<int> out(static_cast<size_t>(std::max(steps, 1)), 0);
    int current = 0;
    for (int i = 1; i < steps - 1; ++i) {
        if (rng.uniform() < h.stay) { out[static_cast<size_t>(i)] = current; continue; }
        if (current != 0 && rng.uniform() < 0.5f) { current = 0; out[static_cast<size_t>(i)] = 0; continue; }
        // A move to one of the roots, never to the one already sounding.
        float total = 0.0f;
        for (size_t k = 0; k < h.roots.size(); ++k) total += h.roots[k] == current ? 0.0f : h.weights[k];
        float u = rng.uniform() * total;
        int pick = h.roots[0];
        for (size_t k = 0; k < h.roots.size(); ++k) {
            if (h.roots[k] == current) continue;
            pick = h.roots[k];
            u -= h.weights[k];
            if (u <= 0.0f) break;
        }
        current = pick;
        out[static_cast<size_t>(i)] = current;
    }
    return out;
}

} // namespace eph
