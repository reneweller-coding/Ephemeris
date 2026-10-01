/**
 * @file Harmony.cpp
 * @brief The chord track, the transposer's chain and the chord vocabulary (the style guide's 3.x).
 */
#include "eph/compose/Harmony.h"
#include "eph/Rack.h"
#include <algorithm>
#include <cmath>

namespace eph {

namespace {

/** @brief The index of @p style, clamped into the styles. */
int styleIndex(Style style) { return std::clamp(static_cast<int>(style), 0, static_cast<int>(Style::Count) - 1); }

/** @brief The transposer's moves of a style (3.7) and their weights. */
struct StyleMoves {
    std::vector<int> roots;   ///< the moves, semitones
    std::vector<float> weights;   ///< their weights
};

/** @brief The transposer's moves of @p style. */
const StyleMoves& movesOf(Style style)
{
    static const StyleMoves kStyles[] = {
        { { 7, 5 },     { 0.55f, 0.45f } },          // Cosmic
        { { 5, -3 },    { 0.6f, 0.4f } },            // Doom
        { { 7, 5, -3 }, { 0.4f, 0.35f, 0.25f } },    // Melodic
        { { 7, 5 },     { 0.5f, 0.5f } },            // Modern
        { { 5 },        { 1.0f } },                  // Drift
    };
    return kStyles[styleIndex(style)];
}

/** @brief A style's chord classes (Static, Pendulum, Loop, Walk), harmonic rhythm and the Markov table's "stays". */
struct StyleChords {
    float cls[4];   ///< the weights of the chord classes: Static, Pendulum, Loop, Walk
    int barsLow;   ///< the harmonic rhythm: a chord's least bars
    int barsHigh;   ///< ... and its most
    float stay;   ///< the Markov table's chance that a chord stays
};

/** @brief The chord classes, harmonic rhythm and stays of @p style. */
const StyleChords& chordsOf(Style style)
{
    static const StyleChords kStyles[] = {
        // Calibrated 25.09.2026: half the style guide's values, half the reference measurement (Tools/analyze_harmony.py,
        // Tools/ref_harmony.json; six recordings a style, so the measurement is rough and weighs no more than the guide).
        { { 0.60f, 0.22f, 0.10f, 0.08f }, 8, 16, 0.75f },    // Cosmic: 94 % of the time on i, 5 of 6 static
        { { 0.25f, 0.43f, 0.27f, 0.05f }, 4, 16, 0.50f },    // Doom: pendulums and loops, chords of about 4.5 bars
        { { 0.15f, 0.37f, 0.30f, 0.18f }, 4, 8, 0.45f },     // Melodic: mostly pendulums (4 bars at least: hypnosis)
        { { 0.37f, 0.35f, 0.15f, 0.13f }, 4, 16, 0.55f },    // Modern: a third of the time on i, chords of 2 to 4 bars
        { { 0.35f, 0.45f, 0.20f, 0.00f }, 8, 32, 0.55f },    // Drift: pendulums, chords of about 4 bars
    };
    return kStyles[styleIndex(style)];
}

/** @brief A progression and its weight. */
struct Weighted {
    std::vector<int> degrees;   ///< its chords, as scale degrees
    float weight;               ///< how often it is drawn
};

/** @brief The pendulum partners of the tonic per mode (3.5 B), as degrees of the mode. */
const std::vector<Weighted>& pendulumsOf(int scale)
{
    static const std::vector<Weighted> kModes[kScales] = {
        { { { 6 }, 0.25f }, { { 5 }, 0.25f }, { { 3 }, 0.15f }, { { 4 }, 0.10f }, { { 2 }, 0.25f } },   // Aeolian (III measured as often as VI, VII)
        { { { 3 }, 0.35f }, { { 6 }, 0.30f }, { { 4 }, 0.15f }, { { 2 }, 0.10f }, { { 1 }, 0.10f } },   // Dorian: IV major
        { { { 1 }, 0.50f }, { { 3 }, 0.25f }, { { 6 }, 0.15f }, { { 5 }, 0.10f } },                     // Phrygian: bII
        { { { 5 }, 0.50f }, { { 3 }, 0.50f } },                                                         // Harmonic minor
        { { { 4 }, 0.40f }, { { 2 }, 0.35f }, { { 3 }, 0.25f } },                                       // Pentatonic: bVII, iv, v
        { { { 6 }, 0.50f }, { { 3 }, 0.35f }, { { 4 }, 0.15f } },                                       // Mixolydian: bVII, IV
        { { { 1 }, 0.60f }, { { 4 }, 0.25f }, { { 6 }, 0.15f } },                                       // Lydian: II
        { { { 1 }, 0.50f }, { { 4 }, 0.25f }, { { 6 }, 0.25f } },                                       // Locrian
    };
    return kModes[std::clamp(scale, 0, kScales - 1)];
}

/** @brief Three- and four-chord loops per mode (3.5 C), the tonic first. */
const std::vector<Weighted>& loopsOf(int scale)
{
    static const std::vector<Weighted> kModes[kScales] = {
        { { { 0, 5, 6 }, 0.25f }, { { 0, 6, 5 }, 0.20f }, { { 0, 3, 5 }, 0.15f }, { { 0, 2, 6 }, 0.10f },
          { { 0, 5, 2, 6 }, 0.15f }, { { 0, 6, 5, 6 }, 0.10f }, { { 0, 2, 3, 2 }, 0.05f } },          // Aeolian
        { { { 0, 3, 6 }, 0.45f }, { { 0, 6, 3 }, 0.30f }, { { 0, 2, 6 }, 0.25f } },                   // Dorian
        { { { 0, 1, 0, 6 }, 0.50f }, { { 0, 1, 6 }, 0.30f }, { { 0, 6, 5 }, 0.20f } },                // Phrygian
        { { { 0, 5, 3 }, 0.50f }, { { 0, 3, 5 }, 0.50f } },                                           // Harmonic minor
        { { { 0, 4, 2 }, 0.50f }, { { 0, 2, 4 }, 0.30f }, { { 0, 3, 4 }, 0.20f } },                   // Pentatonic
        { { { 0, 6, 3 }, 0.60f }, { { 0, 3, 6 }, 0.40f } },                                           // Mixolydian
        { { { 0, 1, 4 }, 0.50f }, { { 0, 1, 0, 6 }, 0.50f } },                                        // Lydian
        { { { 0, 1, 6 }, 0.50f }, { { 0, 6, 1 }, 0.50f } },                                           // Locrian
    };
    return kModes[std::clamp(scale, 0, kScales - 1)];
}

/**
 * @brief The Markov table of 3.6 over the degrees i, bII, III, iv, v, VI, VII (0 .. 6), rows from, columns to,
 *        without the "stays" column (the style's). bII only in Phrygian, where it is a degree; in Dorian the
 *        column iv is IV and 5 points heavier; in Phrygian bII from i rises to 15, VI and VII give 7 each.
 */
float markov(int scale, int from, int to)
{
    //                         i     bII   III   iv    v     VI    VII
    static const float kT[7][7] = {
        /* i   */ { 0.0f,  0.01f, 0.04f, 0.08f, 0.04f, 0.14f, 0.14f },
        /* bII */ { 0.70f, 0.0f,  0.0f,  0.05f, 0.05f, 0.0f,  0.0f  },
        /* III */ { 0.30f, 0.0f,  0.0f,  0.05f, 0.0f,  0.10f, 0.25f },
        /* iv  */ { 0.35f, 0.0f,  0.05f, 0.0f,  0.05f, 0.15f, 0.10f },
        /* v   */ { 0.45f, 0.0f,  0.0f,  0.10f, 0.0f,  0.10f, 0.05f },
        /* VI  */ { 0.25f, 0.0f,  0.10f, 0.05f, 0.0f,  0.0f,  0.30f },
        /* VII */ { 0.40f, 0.0f,  0.05f, 0.05f, 0.0f,  0.20f, 0.0f  },
    };
    float w = kT[std::clamp(from, 0, 6)][std::clamp(to, 0, 6)];
    if (to == 1 && scale != 2) return 0.0f;
    if (scale == 1 && to == 3) w += 0.05f;
    if (scale == 2 && from == 0) {
        if (to == 1) w = 0.15f;
        if (to == 5 || to == 6) w -= 0.07f;
    }
    return std::max(0.0f, w);
}

template <typename T>
/** @brief One of @p v, drawn from @p rng with the weights @p w. */
const T& pick(const std::vector<T>& v, const std::vector<float>& w, Rng& rng)
{
    float total = 0.0f;
    for (float x : w) total += x;
    float u = rng.uniform() * total;
    for (size_t i = 0; i < v.size(); ++i) {
        u -= w[i];
        if (u <= 0.0f && w[i] > 0.0f) return v[i];
    }
    return v.back();
}

/** @brief One of @p list, those with a degree the mode cannot carry left out (the list's first if none is left). */
const std::vector<int>& pickDegrees(const std::vector<Weighted>& list, int scale, Rng& rng)
{
    std::vector<std::vector<int>> v;
    std::vector<float> w;
    for (const Weighted& x : list) {
        bool ok = true;
        for (int d : x.degrees) ok = ok && usableDegree(scale, d);
        v.push_back(x.degrees);
        w.push_back(ok ? x.weight : 0.0f);
    }
    float total = 0.0f;
    for (float x : w) total += x;
    if (total <= 0.0f) return list.front().degrees;
    const std::vector<int>& chosen = pick(v, w, rng);
    for (const Weighted& x : list) if (x.degrees == chosen) return x.degrees;
    return list.front().degrees;
}

} // namespace

const std::vector<int>& progressionRoots(Style style) { return movesOf(style).roots; }

std::vector<int> drawProgression(Style style, int scale, int steps, int barsPerStep, Rng& rng)
{
    StyleMoves m;
    for (size_t k = 0; k < movesOf(style).roots.size(); ++k) {
        const int move = movesOf(style).roots[k];
        bool centreIn = false;
        for (int d = 0; d < scaleSize(scale); ++d) centreIn = centreIn || pitchClass(scaleSemitones(scale, d) + move) == 0;
        if (!centreIn) continue;
        m.roots.push_back(move);
        m.weights.push_back(movesOf(style).weights[k]);
    }
    const int n = std::max(steps, 1);
    std::vector<int> out(static_cast<size_t>(n), 0);
    int pos = 0, current = 0, lastMove = 0;
    if (m.roots.empty()) return out;
    while (pos < n) {
        // A stage of four or eight bars, now and then sixteen on the tonic; at least one step.
        const int bars = current == 0 && rng.uniform() < 0.25f ? 16 : (rng.uniform() < 0.5f ? 4 : 8);
        int hold = std::max(1, bars / std::max(1, barsPerStep));
        if (lastMove == 0) hold = std::min(hold, std::max(1, n / 2));   // the row's first move within its first half
        for (int i = 0; i < hold && pos < n; ++i) out[static_cast<size_t>(pos++)] = current;
        if (current != 0) { current = 0; continue; }
        // The next move, another than the last where the style has one: tonic, fifth, tonic, fourth, tonic.
        std::vector<float> w = m.weights;
        if (m.roots.size() > 1)
            for (size_t k = 0; k < m.roots.size(); ++k) if (m.roots[k] == lastMove) w[k] *= 0.25f;
        current = lastMove = pick(m.roots, w, rng);
    }
    out.back() = 0;
    return out;
}

bool usableDegree(int scale, int degree)
{
    if (scale == 4) return true;   // the pentatonic's "chords" are stacked fourths, always open
    const int n = scaleSize(scale);
    const int d = ((degree % n) + n) % n;
    if (d == 0) return true;
    return scaleSemitones(scale, d + 4) - scaleSemitones(scale, d) == 7;
}

int bassDegree(int scale, int degree)
{
    const int n = scaleSize(scale);
    const int d = ((degree % n) + n) % n;
    return scaleSemitones(scale, d) > 6 ? d - n : d;
}

std::vector<std::pair<double, int>> drawChordTrack(Style style, int scale, double from, double to, Rng& rng, ChordClass* drawn)
{
    const StyleChords& sc = chordsOf(style);
    std::vector<std::pair<double, int>> out;
    out.push_back({ from, 0 });
    const ChordClass cls = pick(std::vector<ChordClass>{ ChordClass::Static, ChordClass::Pendulum, ChordClass::Loop, ChordClass::Walk },
                                std::vector<float>{ sc.cls[0], sc.cls[1], sc.cls[2], sc.cls[3] }, rng);
    if (drawn != nullptr) *drawn = cls;
    // The harmonic rhythm: 4, 8, 16 or 32 bars, inside the style's range.
    std::vector<int> choices;
    for (int b : { 2, 4, 8, 16, 32 }) if (b >= sc.barsLow && b <= sc.barsHigh) choices.push_back(b);
    const double span = static_cast<double>(choices[static_cast<size_t>(rng.below(static_cast<int>(choices.size())))]) * kBeatsPerBar;
    if (cls == ChordClass::Static || to - from < 2.0 * span) return out;

    std::vector<int> cycle;
    if (cls == ChordClass::Pendulum) cycle = { 0, pickDegrees(pendulumsOf(scale), scale, rng).front() };
    else if (cls == ChordClass::Loop) cycle = pickDegrees(loopsOf(scale), scale, rng);
    if (!cycle.empty()) {
        size_t k = 1;
        for (double b = from + span; b < to - kBeatsPerBar; b += span, k = (k + 1) % cycle.size())
            if (cycle[k] != out.back().second) out.push_back({ b, cycle[k] });
        return out;
    }
    // The walk (3.6): one draw per block; the minor modes on the guide's table, the others among their pendulums.
    const bool minor = scale <= 3;
    int current = 0;
    for (double b = from + span; b < to - kBeatsPerBar; b += span) {
        const float stay = current == 0 ? sc.stay : std::max(0.2f, sc.stay - 0.25f);
        if (rng.uniform() < stay) continue;
        std::vector<int> to7;
        std::vector<float> w;
        if (minor) {
            for (int d = 0; d < 7; ++d) {
                if (d == current || !usableDegree(scale, d)) continue;
                to7.push_back(d);
                w.push_back(markov(scale, current, d));
            }
        } else if (current != 0) {
            to7 = { 0 };
            w = { 1.0f };
        } else {
            for (const Weighted& x : pendulumsOf(scale))
                if (usableDegree(scale, x.degrees.front())) { to7.push_back(x.degrees.front()); w.push_back(x.weight); }
        }
        float total = 0.0f;
        for (float x : w) total += x;
        if (to7.empty() || total <= 0.0f) continue;
        current = pick(to7, w, rng);
        out.push_back({ b, current });
    }
    return out;
}

ChordKind drawChordKind(int scale, int degree, bool choir, Rng& rng)
{
    using K = ChordKind;
    static const std::vector<K> kinds = { K::Fifth, K::Triad, K::Sus2, K::Sus4, K::Add9, K::Quartal, K::Stack, K::Seventh };
    // The choirs (3.4: "simple minor triads are the standard") and the pads' whole vocabulary.
    static const std::vector<float> kChoir = { 0.15f, 0.70f, 0.05f, 0.05f, 0.05f, 0.0f, 0.0f, 0.0f };
    static const std::vector<float> kPads  = { 0.20f, 0.30f, 0.08f, 0.07f, 0.15f, 0.10f, 0.05f, 0.05f };
    std::vector<float> w = choir ? kChoir : kPads;
    // The seventh as the guide has it: a major seventh held over VI; elsewhere a little less often.
    const int n = scaleSize(scale);
    if (((degree % n) + n) % n != 5) w[7] *= 0.4f;
    return pick(kinds, w, rng);
}

std::vector<int> chordTones(int scale, int degree, ChordKind kind)
{
    auto st = [&](int k) { return scaleSemitones(scale, degree + k); };
    const int root = st(0);
    std::vector<int> triad = { root, st(2), st(4) };
    if (scale == 4) return triad;   // on the pentatonic every "triad" is already open
    const bool fifthOk = st(4) - root == 7;
    switch (kind) {
    case ChordKind::Fifth:
        if (fifthOk) return { root, st(4), root + 12 };
        break;
    case ChordKind::Sus2:
        if (fifthOk && st(1) - root == 2) return { root, st(1), st(4) };
        break;
    case ChordKind::Sus4:
        if (fifthOk && st(3) - root == 5) return { root, st(3), st(4) };
        break;
    case ChordKind::Add9:
        triad.push_back(st(8));
        return triad;
    case ChordKind::Quartal:
        // Fourths stacked from the root (D G C F): only where all three are perfect.
        if (st(3) - root == 5 && st(6) - st(3) == 5 && st(9) - st(6) == 5) return { root, st(3), st(6), st(9) };
        break;
    case ChordKind::Stack:
        // The triad and the triad a fifth up (Dm + Am = Dm9).
        if (fifthOk) return { root, st(2), st(4), st(6), st(8) };
        break;
    case ChordKind::Seventh:
        triad.push_back(st(6));
        return triad;
    default: break;
    }
    return triad;
}

} // namespace eph
