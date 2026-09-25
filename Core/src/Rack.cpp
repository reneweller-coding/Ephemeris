/**
 * @file Rack.cpp
 * @brief The sequencer rack.
 */
#include "eph/Rack.h"
#include "eph/compose/Harmony.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace eph {

namespace {

// compose.scale order: Aeolian, Dorian, Phrygian, Harmonic Minor, Minor Pentatonic (its own table below),
// Mixolydian, Lydian, Locrian.
const int kScale7[kScales][7] = {
    { 0, 2, 3, 5, 7, 8, 10 },
    { 0, 2, 3, 5, 7, 9, 10 },
    { 0, 1, 3, 5, 7, 8, 10 },
    { 0, 2, 3, 5, 7, 8, 11 },
    { 0, 2, 3, 5, 7, 8, 10 },   // (the pentatonic's place)
    { 0, 2, 4, 5, 7, 9, 10 },
    { 0, 2, 4, 6, 7, 9, 11 },
    { 0, 1, 3, 5, 6, 8, 10 },
};
const int kPentatonic[5] = { 0, 3, 5, 7, 10 };

int floorDiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

} // namespace

int scaleSize(int scale) { return scale == 4 ? 5 : 7; }

int scaleSemitones(int scale, int degree)
{
    const int n = scaleSize(scale);
    const int oct = floorDiv(degree, n);
    const int d = degree - oct * n;
    const int semis = scale == 4 ? kPentatonic[d] : kScale7[std::clamp(scale, 0, kScales - 1)][d];
    return oct * 12 + semis;
}

int conjunctionSteps(int a, int b)
{
    return a <= 0 || b <= 0 ? 0 : std::lcm(a, b);
}

void Rack::setup(const ParamStore& p, uint64_t seed)
{
    keyRoot_ = p.getInt(p.id(Module::Compose, 0, compose::Key));
    scale_ = p.getInt(p.id(Module::Compose, 0, compose::Scale));
    style_ = static_cast<Style>(p.getInt(p.id(Module::Compose, 0, compose::Style)));
    shift_ = base_ = degree_ = 0;
    shiftLog_.assign(1, { 0.0, 0 });
    scaleLog_.assign(1, { 0.0, scale_ });
    for (int i = 0; i < kRows; ++i) {
        Row& r = rows_[i];
        auto get = [&](int index) { return p.get(p.id(Module::Row, i, index)); };
        r.length = std::clamp(static_cast<int>(get(row::Length)), 1, kMaxSteps);
        r.divBeats = rowDivisionBeats(static_cast<RowDivision>(static_cast<int>(get(row::Division))));
        r.direction = static_cast<RowDirection>(static_cast<int>(get(row::Direction)));
        r.octave = static_cast<int>(get(row::Octave));
        r.transpose = static_cast<int>(get(row::Transpose));
        r.chord = 0;
        r.mutation = get(row::Mutation);
        r.gate = get(row::Gate) * 0.01f;
        r.running = get(row::Active) >= 0.5f;
        r.transposer = static_cast<int>(get(row::Mode)) == static_cast<int>(RowMode::Transposer);
        r.startBeat = 0.0;
        r.step = 0;
        r.pos = 0;
        r.dir = 1;
        r.mutations = 0;
        r.rng.seed(mixSeed(seed, static_cast<uint64_t>(i) + 1));
        r.dice.seed(mixSeed(seed, static_cast<uint64_t>(i) + 101));
        r.lanes.seed(mixSeed(seed, static_cast<uint64_t>(i) + 201));
        r.figure = Figure::Classic;
        for (Step& s : r.steps) s = Step{};
    }
    position_ = 0.0;
}

namespace {

constexpr int kRest = -99;

/** @brief The archetypes as eight steps: degrees of a seven-note mode (kRest a rest) and octaves. */
struct Shape { int degree[8]; int octave[8]; };

const Shape& shapeOf(Figure f)
{
    static const Shape kShapes[] = {
        { { 0, 0, 0, 0, 0, 0, 0, 0 },          { 0, 0, 0, 0, 0, 0, 0, 0 } },   // Classic (unused)
        { { 0, 0, 0, 0, 0, 0, 4, 0 },          { 0, 1, 0, 1, 0, 1, 0, 1 } },   // octave pendulum
        { { 0, 4, 0, 4, 0, 4, 6, 4 },          { 0, 0, 0, 1, 0, 0, 0, 0 } },   // fifth anchor
        { { 0, 0, 0, kRest, 0, 0, 4, kRest },  { 0, 1, 0, 0, 0, 1, 0, 0 } },   // 3+1 pulse
        { { 0, 1, 2, 3, 4, 6, 7, 4 },          { 0, 0, 0, 0, 0, 0, 0, 0 } },   // stairs up
        { { 7, 6, 4, 3, 2, 1, 0, 4 },          { 0, 0, 0, 0, 0, 0, 0, 0 } },   // stairs down
        { { 0, 2, 4, 5, 7, 4, 2, 0 },          { 0, 0, 0, 0, 0, 0, 0, 0 } },   // colour (the 5 is the mode's degree)
        { { 0, 1, 0, 4, 0, 1, 0, 6 },          { 0, 0, 0, 0, 0, 0, 0, 0 } },   // Phrygian push
        { { 0, 2, 4, 7, -2, 0, 2, 5 },         { 0, 0, 0, 0, 0, 0, 0, 0 } },   // spiral: i, then VI
    };
    return kShapes[std::clamp(static_cast<int>(f), 0, static_cast<int>(Figure::Spiral))];
}

/** @brief A degree of a seven-note figure in @p scale: itself, or the nearest of the pentatonic's. */
int fromSeven(int scale, int d)
{
    if (scaleSize(scale) == 7) return d;
    static const int kPent[7] = { 0, 1, 1, 2, 3, 3, 4 };
    const int oct = floorDiv(d, 7);
    return kPent[d - 7 * oct] + 5 * oct;
}

/** @brief Share of a row's steps that are probability gates, by style (the style guide's 4.6). */
float chanceShare(Style style)
{
    static const float kShare[] = { 0.05f, 0.05f, 0.08f, 0.20f, 0.15f };   // Cosmic, Doom, Melodic, Modern, Drift
    return kShare[std::clamp(static_cast<int>(style), 0, 4)];
}

/** @brief Share of a row's steps that play a quantised random note, by style (the style guide's 4.6). */
float randomShare(Style style)
{
    static const float kShare[] = { 0.03f, 0.03f, 0.05f, 0.12f, 0.20f };   // Cosmic, Doom, Melodic, Modern, Drift
    return kShare[std::clamp(static_cast<int>(style), 0, 4)];
}

/** @brief A note through the quantiser: root, fifth, octave, seventh, fourth or third, the lower ones likelier. */
int quantisedDegree(int scale, Rng& dice)
{
    static const int kDegrees[6] = { 0, 4, 7, 6, 3, 2 };
    static const float kWeights[6] = { 3.0f, 3.0f, 2.0f, 1.0f, 1.0f, 1.0f };
    float u = dice.uniform() * 11.0f;
    int k = 0;
    while (k < 5 && (u -= kWeights[k]) > 0.0f) ++k;
    return fromSeven(scale, kDegrees[k]);
}

template <size_t N>
Figure drawFigure(const Figure (&f)[N], const float (&w)[N], Rng& g)
{
    float total = 0.0f;
    for (float x : w) total += x;
    float u = g.uniform() * total;
    for (size_t i = 0; i < N; ++i) {
        u -= w[i];
        if (u <= 0.0f && w[i] > 0.0f) return f[i];
    }
    return f[N - 1];
}

} // namespace

const char* figureName(Figure f)
{
    static const char* const kNames[] = { "Classic", "Octave Pendulum", "Fifth Anchor", "3+1 Pulse", "Stairs Up",
                                          "Stairs Down", "Colour", "Phrygian Push", "Spiral", "Canon" };
    return kNames[std::clamp(static_cast<int>(f), 0, static_cast<int>(Figure::Count) - 1)];
}

int characterDegree(int scale)
{
    static const int kChar[kScales] = { 5, 5, 1, 6, 4, 6, 3, 4 };
    return kChar[std::clamp(scale, 0, kScales - 1)];
}

void Rack::generate(int row, RowRole role)
{
    Row& r = rows_[row];
    for (bool& t : r.thinned) t = false;
    // The modulation lane: a length against the row's (3, 5, 7 or 12, never the row's own), a few bright steps over
    // a darker ground -- accents of timbre that wander against the notes and make the groove. Its depth by style.
    {
        static const float kDepth[] = { 0.8f, 0.6f, 0.7f, 1.0f, 0.5f };   // Cosmic, Doom, Melodic, Modern, Drift
        const float depth = role == RowRole::Transposer ? 0.0f : kDepth[std::clamp(static_cast<int>(style_), 0, 4)];
        const int lengths[4] = { 3, 5, 7, 12 };
        int len = lengths[r.rng.below(4)];
        if (len == r.length) len = len == 12 ? 7 : len + 2;
        r.modLength = len;
        r.modPos = 0;
        for (int i = 0; i < kMaxSteps; ++i) {
            const float u = r.rng.uniform();
            r.mod[i] = i >= len ? 0.0f : depth * (u < 0.3f ? 0.9f + 0.6f * r.rng.uniform() : (u < 0.6f ? 0.0f : -0.5f + 0.3f * r.rng.uniform()));
        }
        // The decay lane: long open notes, plucked short ones, a length against both (Boddy's "the shape of the AR
        // envelopes" as the main control). From its own stream, so the patterns stay as drawn.
        static const int dLengths[5] = { 3, 5, 7, 9, 12 };
        int dl = dLengths[r.lanes.below(5)];
        for (int tries = 0; (dl == r.length || dl == len) && tries < 5; ++tries) dl = dLengths[(tries + 1 + r.lanes.below(4)) % 5];
        r.decayLength = dl;
        r.decayPos = 0;
        for (int i = 0; i < kMaxSteps; ++i) {
            const float u = r.lanes.uniform();
            r.decay[i] = i >= dl || role == RowRole::Transposer ? 0.0f
                       : (u < 0.35f ? 0.6f + 0.6f * r.lanes.uniform() : (u < 0.7f ? 0.0f : -0.8f + 0.4f * r.lanes.uniform()));
        }
    }
    Rng& g = r.rng;
    const int n = scaleSize(scale_);
    int walk = 0;   // the Walk role's current degree
    std::vector<int> roots;
    if (role == RowRole::Transposer)
        roots = drawProgression(style_, scale_, r.length, std::max(1, static_cast<int>(std::lround(r.divBeats / kBeatsPerBar))), g);
    // The figure (the style guide's 4.2).
    using F = Figure;
    const bool phrygian = scale_ == 2 || scale_ == 7;
    const bool canon = row > 0 && !rows_[row - 1].transposer;
    switch (role) {
    case RowRole::Bass: {
        static const F f[] = { F::Classic, F::OctavePendulum, F::FifthAnchor, F::ThreePlusOne };
        static const float w[] = { 0.25f, 0.30f, 0.30f, 0.15f };
        r.figure = drawFigure(f, w, g);
        break;
    }
    case RowRole::Counter: {
        static const F f[] = { F::Classic, F::StairsUp, F::StairsDown, F::Colour, F::PhrygianPush, F::Spiral, F::Canon, F::FifthAnchor };
        const float w[] = { 0.15f, 0.12f, 0.10f, 0.18f, phrygian ? 0.20f : 0.0f, 0.12f, canon ? 0.15f : 0.0f, 0.08f };
        r.figure = drawFigure(f, w, g);
        break;
    }
    case RowRole::Walk: {
        static const F f[] = { F::Classic, F::StairsUp, F::StairsDown, F::Colour, F::Spiral, F::Canon };
        const float w[] = { 0.35f, 0.15f, 0.15f, 0.15f, 0.10f, canon ? 0.10f : 0.0f };
        r.figure = drawFigure(f, w, g);
        break;
    }
    default: r.figure = F::Classic; break;
    }
    // The canon: the row before, three steps later or a fifth up.
    const bool canonLater = g.uniform() < 0.5f;
    const Row& before = rows_[std::max(0, row - 1)];
    for (int i = 0; i < kMaxSteps; ++i) {
        Step s;
        s.velocity = 0.72f + 0.12f * g.uniform();
        if (role == RowRole::Transposer) {
            s.degree = i < static_cast<int>(roots.size()) ? roots[static_cast<size_t>(i)] : 0;
            r.steps[i] = s;
            continue;
        }
        if (r.figure == F::Canon) {
            const int L = std::max(1, before.length);
            const Step& src = before.steps[canonLater ? ((i - 3) % L + L) % L : i % L];
            s.degree = src.degree + (canonLater ? 0 : fromSeven(scale_, 4));
            s.octave = src.octave;
            s.gate = src.gate;
        } else if (r.figure != F::Classic) {
            const Shape& sh = shapeOf(r.figure);
            int d = sh.degree[i % 8];
            if (d == kRest) { s.gate = false; d = 0; }
            s.degree = fromSeven(scale_, d);
            if (r.figure == F::Colour && i % 8 == 3) {
                // The mode's own degree, above the fifth: B in D Dorian, Eb' in D Phrygian.
                const int c = characterDegree(scale_);
                s.degree = n == 7 && c < 4 ? c + 7 : c;
            }
            s.octave = sh.octave[i % 8];
            // The second eight and on: the same figure with the odd step an octave away.
            if (i >= 8 && i % 8 != 0 && g.uniform() < 0.2f) s.octave = s.octave == 0 ? 1 : 0;
        } else {
            switch (role) {
            case RowRole::Bass: {
                // Mostly the root; an octave up on about a third of the steps; now and then the fifth,
                // the seventh or the third.
                const float u = g.uniform();
                if (i == 0) { s.degree = 0; s.octave = 0; }
                else if (u < 0.30f) s.octave = 1;
                else if (u < 0.42f) { const int pick[3] = { 4, 6, 2 }; s.degree = pick[g.below(3)] % n; }
                break;
            }
            case RowRole::Counter: {
                // Chord tones of the tonic (1, 3, 5, octave) as a rising and falling figure.
                const int chord[4] = { 0, 2, 4, n };
                const int up = i % 6 < 3 ? i % 6 : 6 - i % 6;
                s.degree = chord[(up + (g.uniform() < 0.2f ? 1 : 0)) % 4];
                break;
            }
            case RowRole::Walk: {
                const float u = g.uniform();
                if (i > 0) walk += u < 0.4f ? 1 : (u < 0.8f ? -1 : (g.uniform() < 0.5f ? 3 : -2));
                walk = std::clamp(walk, -2, n + 2);
                s.degree = walk;
                break;
            }
            default: break;
            }
        }
        if (role == RowRole::Bass) s.slide = i != 0 && r.figure != F::ThreePlusOne && g.uniform() < 0.05f;
        r.steps[i] = s;
    }
    if (role == RowRole::Transposer) return;
    // The first step is the root or the fifth: it names the centre.
    if (r.figure != F::Canon && r.steps[0].degree != 0 && r.steps[0].degree != fromSeven(scale_, 4)) r.steps[0].degree = 0;
    r.steps[0].gate = true;
    // Rests, accents and probability gates per eight steps.
    const bool threeThreeTwo = g.uniform() < 0.25f;
    const float share = chanceShare(style_), randomly = randomShare(style_);
    for (int b = 0; b < kMaxSteps; b += 8) {
        int rests = 0;
        for (int i = b; i < b + 8; ++i) rests += r.steps[i].gate ? 0 : 1;
        const float u = g.uniform();
        const int want = role == RowRole::Bass ? (u < 0.3f ? 0 : (u < 0.8f ? 1 : 2)) : (u < 0.45f ? 1 : (u < 0.85f ? 2 : 3));
        for (int tries = 0; rests < want && tries < 16; ++tries) {
            const int i = b + 1 + g.below(7);
            if (i % 4 == 0 || !r.steps[i].gate) continue;   // the first of every four always sounds
            r.steps[i].gate = false;
            ++rests;
        }
        for (int i = b; i < b + 8; ++i) {
            Step& s = r.steps[i];
            const int k = i - b;
            s.accent = threeThreeTwo ? (k == 0 || k == 3 || k == 6) : (k == 0 || k == 4);
            if (role != RowRole::Bass && !threeThreeTwo && k == 4) s.accent = g.uniform() < 0.5f;
            // The chance and the random notes only on the counter rows after the first: the bass and the main
            // sequence stay exactly what they are, the ground the ear locks onto.
            const bool loose = role != RowRole::Bass && row >= 2;
            if (i % 4 != 0 && g.uniform() < share && loose) s.chance = 0.6f + 0.3f * g.uniform();
            if (i != 0 && g.uniform() < randomly && loose) s.random = true;
        }
    }
    std::copy(r.steps, r.steps + kMaxSteps, r.theme);
}

int Rack::rootNote(const Row& r) const
{
    // The key's root in A2 .. G#3, then the row's octave: A minor with octave -1 is A1 (55 Hz).
    return 45 + ((keyRoot_ - 9 + 12) % 12) + 12 * r.octave;
}

void Rack::mutate(Row& r, int index, double beat, std::vector<RackEvent>& log)
{
    const int i = r.rng.below(r.length);
    Step& s = r.steps[i];
    const float what = r.rng.uniform();
    if (r.transposer) {
        // A transposer mutates its roots only, and never its first step: the piece keeps its tonic.
        const std::vector<int>& roots = progressionRoots(style_);
        if (i != 0) s.degree = r.rng.uniform() < 0.4f ? 0 : roots[static_cast<size_t>(r.rng.below(static_cast<int>(roots.size())))];
    } else if (what < 0.45f) {
        const int n = scaleSize(scale_);
        const int choices[5] = { 0, 2, 4, 6 % n, n - 1 };
        s.degree = choices[r.rng.below(5)];
    } else if (what < 0.70f) s.octave = s.octave == 0 ? 1 : 0;
    else if (what < 0.88f) { if (i != 0) s.gate = !s.gate; }
    else s.accent = !s.accent;
    ++r.mutations;
    log.push_back({ beat, index, RackOp::Mutate, i });
}

void Rack::advance(Row& r, int index, double beat, std::vector<RackEvent>& log)
{
    const int L = r.length;
    switch (r.direction) {
    case RowDirection::Forward:  r.pos = (r.pos + 1) % L; break;
    case RowDirection::Backward: r.pos = (r.pos - 1 + L) % L; break;
    case RowDirection::Pendulum:
        if (L == 1) r.pos = 0;
        else {
            if (r.pos + r.dir < 0 || r.pos + r.dir >= L) r.dir = -r.dir;
            r.pos += r.dir;
        }
        break;
    case RowDirection::RandomWalk:
        r.pos = (r.pos + (r.rng.uniform() < 0.5f ? L - 1 : 1)) % L;
        break;
    default: r.pos = (r.pos + 1) % L; break;
    }
    ++r.step;
    r.modPos = (r.modPos + 1) % std::max(1, r.modLength);
    r.decayPos = (r.decayPos + 1) % std::max(1, r.decayLength);
    // A cycle ends every `length` steps, whatever the direction: that is when the register shifts.
    // Hypnosis (25.09.2026): a row changes only where a 16-bar block begins -- at the first end of its cycle on or
    // after the mark -- so a pattern holds for sixteen bars and one thing changes at a time; the chance is the
    // row's mutation, doubled for the longer wait.
    if (r.step % L == 0 && r.mutation > 0.0f && beat + r.divBeats >= r.nextMutation - 1e-9) {
        while (r.nextMutation <= beat + r.divBeats + 1e-9) r.nextMutation += 64.0;
        if (r.rng.uniform() < std::min(1.0f, 2.0f * r.mutation)) mutate(r, index, beat, log);
    }
}

void Rack::playStep(int index, Row& r, Score& score, std::vector<RackEvent>& log)
{
    const double beat = nextStepBeat(r);
    const Step& s = r.steps[r.pos];
    if (r.transposer) {
        if (s.gate) {
            degree_ = s.degree + 12 * s.octave;
            shift_ = base_ + degree_;
            if (shiftLog_.empty() || shiftLog_.back().second != shift_) shiftLog_.push_back({ beat, shift_ });
        }
    } else if (s.gate && (s.chance >= 1.0f || r.dice.uniform() < s.chance)) {
        // A ratchet splits the step into quick triggers, each a little softer.
        const int sub = std::max(1, s.ratchet);
        const int degree = s.random ? quantisedDegree(scale_, r.dice) : s.degree;
        for (int k = 0; k < sub; ++k) {
            NoteEvent e;
            e.beat = beat + r.divBeats * static_cast<double>(k) / sub;
            e.part = rowPart(index);
            e.pitch = std::clamp(rootNote(r) + r.transpose + shift_ + scaleSemitones(scale_, degree + r.chord) + 12 * s.octave, 0, 127);
            e.accent = s.accent && k == 0;
            e.bright = r.mod[r.modPos];
            e.decay = r.decay[r.decayPos];
            e.velocity = std::min(1.0f, s.velocity + (e.accent ? 0.15f : 0.0f) - 0.06f * static_cast<float>(k));
            e.slide = s.slide && sub == 1;
            // A slide holds into the next step so the voice glides instead of retriggering.
            e.length = sub > 1 ? 0.5 * r.divBeats / sub : r.divBeats * (s.slide ? 1.05 : static_cast<double>(std::max(0.05f, r.gate)));
            score.notes.push_back(e);
        }
    }
    advance(r, index, beat, log);
}

void Rack::run(Score& score, double endBeat)
{
    std::vector<RackEvent> log;
    // The composer's events in this span, by beat. Chosen by beat rather than by index, so the score
    // may be re-sorted between two runs (the mutations appended below move) without an event being
    // applied twice.
    std::vector<RackEvent> ev;
    for (const RackEvent& e : score.rack)
        if (e.op != RackOp::Mutate && e.beat >= position_ && e.beat < endBeat) ev.push_back(e);
    std::stable_sort(ev.begin(), ev.end(), [](const RackEvent& a, const RackEvent& b) { return a.beat < b.beat; });
    size_t cursor = 0;
    for (;;) {
        // The earliest pending thing: a rack event or a step of a running row.
        double best = endBeat;
        int bestRow = -1;
        for (int i = 0; i < kRows; ++i) {
            if (!rows_[i].running) continue;
            const double b = nextStepBeat(rows_[i]);
            // A transposer steps first at equal beats, so the downbeat already sounds in the new root.
            if (b < best || (b == best && bestRow >= 0 && rows_[i].transposer && !rows_[bestRow].transposer)) { best = b; bestRow = i; }
        }
        if (cursor < ev.size() && ev[cursor].beat <= best) {
            // Rack events first at equal beats: a transposition applies to the step on its beat.
            const RackEvent& e = ev[cursor++];
            if (e.op == RackOp::Key) {
                base_ = e.value;
                shift_ = base_ + degree_;
                if (shiftLog_.back().second != shift_) shiftLog_.push_back({ e.beat, shift_ });
                continue;
            }
            if (e.op == RackOp::Scale) {
                scale_ = std::clamp(e.value, 0, kScales - 1);
                if (scaleLog_.back().second != scale_) scaleLog_.push_back({ e.beat, scale_ });
                continue;
            }
            const int lo = e.row < 0 ? 0 : e.row, hi = e.row < 0 ? kRows - 1 : e.row;
            for (int i = lo; i <= hi && i < kRows; ++i) {
                Row& r = rows_[i];
                switch (e.op) {
                case RackOp::Start:
                    r.running = true; r.startBeat = e.beat; r.step = 0; r.pos = 0; r.dir = 1;
                    r.nextMutation = e.beat + 64.0;
                    break;
                case RackOp::Stop:
                    r.running = false;
                    // A transposer that stops takes the rows home to the tonic.
                    if (r.transposer && degree_ != 0) { degree_ = 0; shift_ = base_; shiftLog_.push_back({ e.beat, shift_ }); }
                    break;
                case RackOp::Transpose: r.transpose = e.value; break;
                case RackOp::Chord: r.chord = e.value; break;
                case RackOp::Ratchet: {
                    // Steps drawn anew from the row's dice: two triggers mostly, three or four now and then.
                    for (Step& st : r.steps) st.ratchet = 1;
                    for (int k = 0, tries = 0; k < e.value && tries < 32; ++tries) {
                        Step& st = r.steps[1 + r.dice.below(std::max(1, r.length - 1))];
                        if (!st.gate || st.ratchet > 1 || r.length < 2) continue;
                        const float u = r.dice.uniform();
                        st.ratchet = u < 0.7f ? 2 : (u < 0.9f ? 3 : 4);
                        ++k;
                    }
                    break;
                }
                case RackOp::Thin:
                    for (int k = 0, tries = 0; k < e.value && tries < 32 && r.length > 1; ++tries) {
                        const int at = 1 + r.dice.below(r.length - 1);
                        Step& st = r.steps[at];
                        if (!st.gate) continue;
                        st.gate = false;
                        r.thinned[at] = true;
                        ++k;
                    }
                    break;
                case RackOp::Fill:
                    for (int k = 0, tries = 0; k < e.value && tries < 64 && r.length > 1; ++tries) {
                        const int at = 1 + r.dice.below(r.length - 1);
                        if (!r.thinned[at]) continue;
                        r.steps[at].gate = true;
                        r.thinned[at] = false;
                        ++k;
                    }
                    break;
                case RackOp::Division: {
                    // From the next step on: the steps played so far stay where they were.
                    const double div = rowDivisionBeats(static_cast<RowDivision>(e.value));
                    if (r.running) r.startBeat = nextStepBeat(r) - static_cast<double>(r.step) * div;
                    r.divBeats = div;
                    break;
                }
                case RackOp::SetLength:
                    r.length = std::clamp(e.value, 1, kMaxSteps);
                    r.pos %= r.length;
                    break;
                case RackOp::Gate:
                    // A step switched: never step 0, never a thinned one (a Fill brings those), and half the row keeps
                    // sounding.
                    for (int k = 0, tries = 0; k < e.value && tries < 32 && r.length > 2; ++tries) {
                        const int at = 1 + r.dice.below(r.length - 1);
                        if (r.thinned[at]) continue;
                        int sounding = 0;
                        for (int j = 0; j < r.length; ++j) sounding += r.steps[j].gate ? 1 : 0;
                        Step& st = r.steps[at];
                        if (st.gate && 2 * (sounding - 1) < r.length) continue;
                        st.gate = !st.gate;
                        ++k;
                    }
                    break;
                case RackOp::OctaveStep:
                    // A sounding step an octave off (up mostly, the bass only up), or one that was back where it was.
                    for (int k = 0, tries = 0; k < e.value && tries < 32 && r.length > 1; ++tries) {
                        Step& st = r.steps[1 + r.dice.below(r.length - 1)];
                        if (!st.gate) continue;
                        st.octave = st.octave != 0 ? 0 : (i == 0 || r.dice.uniform() < 0.7f ? 1 : -1);
                        ++k;
                    }
                    break;
                case RackOp::Direction:
                    r.direction = static_cast<RowDirection>(std::clamp(e.value, 0, static_cast<int>(RowDirection::Count) - 1));
                    r.dir = 1;
                    break;
                case RackOp::Theme:
                    // Back to the pattern as drawn; the ratchets of the moment stay, and the thinned steps stay silent.
                    for (int j = 0; j < kMaxSteps; ++j) {
                        const int ratchet = r.steps[j].ratchet;
                        r.steps[j] = r.theme[j];
                        r.steps[j].ratchet = ratchet;
                        if (r.thinned[j]) r.steps[j].gate = false;
                    }
                    break;
                default: break;
                }
            }
            continue;
        }
        if (bestRow < 0) break;
        playStep(bestRow, rows_[bestRow], score, log);
    }
    position_ = endBeat;
    score.rack.insert(score.rack.end(), log.begin(), log.end());
}

} // namespace eph
