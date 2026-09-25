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
        for (Step& s : r.steps) s = Step{};
    }
    position_ = 0.0;
}

void Rack::generate(int row, RowRole role)
{
    Row& r = rows_[row];
    Rng& g = r.rng;
    const int n = scaleSize(scale_);
    int walk = 0;   // the Walk role's current degree
    std::vector<int> roots;
    if (role == RowRole::Transposer)
        roots = drawProgression(style_, scale_, r.length, std::max(1, static_cast<int>(std::lround(r.divBeats / kBeatsPerBar))), g);
    for (int i = 0; i < kMaxSteps; ++i) {
        Step s;
        s.velocity = 0.72f + 0.12f * g.uniform();
        switch (role) {
        case RowRole::Bass: {
            // Mostly the root; an octave up on about a third of the steps; now and then the fifth,
            // the seventh or the third; the downbeat of every four steps always sounds.
            const float u = g.uniform();
            if (i == 0) { s.degree = 0; s.octave = 0; }
            else if (u < 0.30f) s.octave = 1;
            else if (u < 0.42f) { const int pick[3] = { 4, 6, 2 }; s.degree = pick[g.below(3)] % n; }
            s.gate = (i % 4 == 0) || g.uniform() < 0.88f;
            s.accent = (i % 8 == 0) || g.uniform() < 0.08f;
            s.slide = i != 0 && g.uniform() < 0.05f;
            break;
        }
        case RowRole::Counter: {
            // Chord tones of the tonic (1, 3, 5, octave) as a rising and falling figure.
            const int chord[4] = { 0, 2, 4, n };
            const int up = i % 6 < 3 ? i % 6 : 6 - i % 6;
            s.degree = chord[(up + (g.uniform() < 0.2f ? 1 : 0)) % 4];
            s.gate = i == 0 || g.uniform() < 0.85f;
            s.accent = i == 0;
            break;
        }
        case RowRole::Walk: {
            const float u = g.uniform();
            if (i > 0) walk += u < 0.4f ? 1 : (u < 0.8f ? -1 : (g.uniform() < 0.5f ? 3 : -2));
            walk = std::clamp(walk, -2, n + 2);
            s.degree = walk;
            s.gate = i == 0 || g.uniform() < 0.8f;
            s.accent = i == 0;
            break;
        }
        case RowRole::Transposer:
            s.degree = i < static_cast<int>(roots.size()) ? roots[static_cast<size_t>(i)] : 0;
            break;
        }
        r.steps[i] = s;
    }
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
    // A cycle ends every `length` steps, whatever the direction: that is when the register shifts.
    if (r.step % L == 0 && r.mutation > 0.0f && r.rng.uniform() < r.mutation) mutate(r, index, beat, log);
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
    } else if (s.gate) {
        NoteEvent e;
        e.beat = beat;
        e.part = rowPart(index);
        e.pitch = std::clamp(rootNote(r) + r.transpose + shift_ + scaleSemitones(scale_, s.degree + r.chord) + 12 * s.octave, 0, 127);
        e.accent = s.accent;
        e.velocity = std::min(1.0f, s.velocity + (s.accent ? 0.15f : 0.0f));
        e.slide = s.slide;
        // A slide holds into the next step so the voice glides instead of retriggering.
        e.length = r.divBeats * (s.slide ? 1.05 : static_cast<double>(std::max(0.05f, r.gate)));
        score.notes.push_back(e);
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
                    break;
                case RackOp::Stop:
                    r.running = false;
                    // A transposer that stops takes the rows home to the tonic.
                    if (r.transposer && degree_ != 0) { degree_ = 0; shift_ = base_; shiftLog_.push_back({ e.beat, shift_ }); }
                    break;
                case RackOp::Transpose: r.transpose = e.value; break;
                case RackOp::Chord: r.chord = e.value; break;
                case RackOp::SetLength:
                    r.length = std::clamp(e.value, 1, kMaxSteps);
                    r.pos %= r.length;
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
