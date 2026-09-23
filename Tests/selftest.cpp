/**
 * @file selftest.cpp
 * @brief eph_selftest: every building block measured against an independently derived value.
 *
 * Sections are registered in the table at the bottom; `eph_selftest --list` prints their names (ctest
 * registers one test per name) and `--only a,b` runs the named ones. Only checks that protect
 * something real belong here (the user's rule, 24.09.2026): no test of what cannot break.
 */
#include "eph/Clock.h"
#include "eph/Engine.h"
#include "eph/GestureEngine.h"
#include "eph/Lead.h"
#include "eph/Midi.h"
#include "eph/ModVoice.h"
#include "eph/Pads.h"
#include "eph/Params.h"
#include "eph/Rack.h"
#include "eph/Score.h"
#include "eph/Study.h"
#include "eph/TapeEcho.h"
#include "eph/TapeKeys.h"
#include "TestSupport.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace eph;
using namespace ephtest;

namespace {

/**
 * The tempo map converts beats to seconds in closed form. Measured against a numerical integral of
 * 60/bpm over a ramp that spans a whole 20-minute piece, the way a piece that speeds up from its
 * atmosphere into its sequence would: the two must agree, and beatAt must invert secondsAt.
 */
void testTempoMap()
{
    section("tempo map against numerical integration");
    TempoMap m;
    m.add(0.0, 96.0, true);
    m.add(1200.0, 128.0, false);
    m.add(1600.0, 110.0, false);
    double integral = 0.0, maxErr = 0.0, maxInv = 0.0;
    const int steps = 400000;
    const double end = 2000.0, h = end / steps;
    for (int i = 0; i < steps; ++i) {
        const double b0 = i * h;
        // Simpson over one step; the tempo is continuous inside each step except at 1600 (held segment).
        const double f0 = 60.0 / m.bpmAt(b0), f1 = 60.0 / m.bpmAt(b0 + 0.5 * h), f2 = 60.0 / m.bpmAt(b0 + h - 1e-12);
        integral += h / 6.0 * (f0 + 4.0 * f1 + f2);
        if ((i + 1) % 1000 == 0) {
            const double b = b0 + h;
            maxErr = std::max(maxErr, std::fabs(m.secondsAt(b) - integral));
            maxInv = std::max(maxInv, std::fabs(m.beatAt(m.secondsAt(b)) - b));
        }
    }
    check(maxErr < 1e-6, "secondsAt equals the integral of 60/bpm", fmt("max error %.3g s over %.0f s", maxErr, integral));
    check(maxInv < 1e-9, "beatAt inverts secondsAt", fmt("max error %.3g beats", maxInv));
}

/**
 * Gestures are the melody of this music (PLAN 6.4), so their curves are checked for the properties
 * the ear relies on: they start and end where they say, the minimum-jerk path is monotonic and
 * starts and ends at rest, and a knob keeps the value the last gesture left it at.
 */
void testGestures()
{
    section("gesture curves");
    // Minimum jerk: x(0)=0, x(1)=1, x(1/2)=1/2, monotonic, and velocity zero at both ends.
    bool mono = true;
    double prev = -1.0;
    for (int i = 0; i <= 1000; ++i) {
        const double x = gestureShape(GestureShape::MinimumJerk, i / 1000.0);
        if (x < prev) mono = false;
        prev = x;
    }
    const double v0 = gestureShape(GestureShape::MinimumJerk, 1e-4) / 1e-4;
    const double v1 = (1.0 - gestureShape(GestureShape::MinimumJerk, 1.0 - 1e-4)) / 1e-4;
    check(mono && gestureShape(GestureShape::MinimumJerk, 0.5) == 0.5, "minimum jerk monotonic and symmetric", "");
    check(v0 < 1e-6 && v1 < 1e-6, "minimum jerk starts and ends at rest", fmt("end speeds %.2g, %.2g", v0, v1));
    // Peak speed of the minimum-jerk profile is 15/8 of the mean speed (Flash and Hogan 1985).
    const double vPeak = (gestureShape(GestureShape::MinimumJerk, 0.5 + 1e-5) - gestureShape(GestureShape::MinimumJerk, 0.5 - 1e-5)) / 2e-5;
    check(std::fabs(vPeak - 1.875) < 1e-6, "peak speed 15/8 of the mean", fmt("%.6f", vPeak));

    Score s;
    s.clear(120.0);
    s.gestures.push_back({ 7, 16.0, 32.0, 0.0f, 0.6f, GestureShape::MinimumJerk, 0 });
    s.gestures.push_back({ 7, 80.0, 8.0, 0.6f, -0.2f, GestureShape::Linear, 1 });
    s.gestures.push_back({ 3, 0.0, 4.0, 0.0f, 1.0f, GestureShape::Linear, 0 });
    s.sort();
    check(s.gestureOffset(7, 0.0) == 0.0f, "no offset before the first gesture", "");
    check(s.gestureOffset(7, 32.0) == 0.3f, "half way through a symmetric gesture", fmt("%.6f", static_cast<double>(s.gestureOffset(7, 32.0))));
    check(s.gestureOffset(7, 60.0) == 0.6f, "the knob stays where the hand let go", "");
    check(std::fabs(s.gestureOffset(7, 84.0) - 0.2f) < 1e-6f, "the next gesture takes over", fmt("%.6f", static_cast<double>(s.gestureOffset(7, 84.0))));
    check(s.gestureOffset(3, 100.0) == 1.0f, "another knob is independent", "");
}

/** Presets and `.ephset` files are the parameter text form; it has to come back exactly. */
void testParams()
{
    section("parameter text form");
    ParamStore a, b;
    std::string err;
    const bool ok = a.parseText("compose.key=F# compose.scale=Harmonic Minor row3.active=on row3.length=13\n"
                                "row5.mutation=0.37; master.level=-3.25 # comment", &err);
    check(ok, "assignments parse", err);
    check(a.getInt(a.find("compose.scale")) == 3, "a choice name with a space", a.format(a.find("compose.scale")));
    b.parseText(a.toText(true));
    bool same = true;
    for (int i = 0; i < a.count(); ++i) same = same && a.get(i) == b.get(i);
    check(same, "toText round-trips every value", "");
    check(a.id(Module::Row, 2, row::Length) == a.find("row3.length"), "ids and keys agree", "");
    check(!a.parseText("row9.length=3"), "an unknown key is refused", "");
}

/** The MIDI file must hold the tempo map so that every beat lands where the audio has it. */
void testMidiTempo()
{
    section("MIDI tempo map in time with the render");
    Score s;
    s.clear(100.0);
    s.tempo.add(0.0, 100.0, true);
    s.tempo.add(64.0, 124.0, false);
    s.lengthBeats = 96.0;
    const std::vector<uint8_t> f = encodeMidi(s);
    // Walk the conductor track's tempo events and integrate the file's own time per beat.
    size_t i = 14 + 8;   // header chunk, then the first track's header
    double tickUs = 0.0, fileSec = 0.0;
    int64_t tick = 0, lastTick = 0;
    double maxErr = 0.0;
    int tempos = 0;
    while (i + 3 < f.size()) {
        uint32_t d = 0;
        while (f[i] & 0x80) d = (d << 7) | (f[i++] & 0x7F);
        d = (d << 7) | f[i++];
        tick += d;
        if (f[i] != 0xFF) break;
        const uint8_t type = f[i + 1];
        const uint8_t len = f[i + 2];
        if (type == 0x2F) break;
        fileSec += static_cast<double>(tick - lastTick) * tickUs * 1e-6;
        lastTick = tick;
        if (type == 0x51) {
            const uint32_t us = (static_cast<uint32_t>(f[i + 3]) << 16) | (static_cast<uint32_t>(f[i + 4]) << 8) | f[i + 5];
            tickUs = us / static_cast<double>(kMidiPpq);
            ++tempos;
            const double beat = static_cast<double>(tick) / kMidiPpq;
            maxErr = std::max(maxErr, std::fabs(fileSec - s.tempo.secondsAt(beat)));
        }
        i += 3 + len;
    }
    check(tempos == 65, "one tempo event per beat of the ramp, one after", fmt("%d events", tempos));
    check(maxErr < 64e-6, "every beat within a microsecond per beat of the render", fmt("max %.3g s", maxErr));
}

/**
 * The rack (PLAN 5.1): a transposition lands on the first step at or after its beat and never inside a
 * note; the same seed plays the same notes; a mutation chance of one changes exactly one step per
 * cycle and never silences step 0.
 */
void testRack()
{
    section("sequencer rack");
    ParamStore p;
    p.parseText("row1.active=1 row1.length=16 row1.division=1/16 row1.mutation=0 row2.length=13 row2.division=1/16");
    auto play = [&](uint64_t seed, float mutation, Score& s) {
        p.set(p.find("row1.mutation"), mutation);
        s.clear(120.0);
        s.lengthBeats = 64.0;
        s.rack.push_back({ 10.1, 0, RackOp::Transpose, 5 });
        s.rack.push_back({ 0.0, 1, RackOp::Start, 0 });
        Rack r;
        r.setup(p, seed);
        r.generate(0, RowRole::Bass);
        r.generate(1, RowRole::Counter);
        s.sort();
        r.run(s, 32.0);   // in two runs, as the composer's lookahead would
        s.sort();
        r.run(s, 64.0);
        s.sort();
        return r.mutations(0);
    };
    Score a, b, c;
    play(3, 0.0f, a);
    play(3, 0.0f, b);
    bool same = a.notes.size() == b.notes.size();
    for (size_t i = 0; same && i < a.notes.size(); ++i)
        same = a.notes[i].beat == b.notes[i].beat && a.notes[i].pitch == b.notes[i].pitch && a.notes[i].part == b.notes[i].part;
    check(same && !a.notes.empty(), "same seed, same notes", fmt("%zu notes", a.notes.size()));
    play(4, 0.0f, c);
    bool differ = c.notes.size() != a.notes.size();
    for (size_t i = 0; !differ && i < a.notes.size(); ++i) differ = a.notes[i].pitch != c.notes[i].pitch;
    check(differ, "another seed, another row", "");

    // Transposition at 10.1 beats: the first transposed step of row 1 is the one at 10.25.
    int wrong = 0;
    for (const NoteEvent& n : a.notes) {
        if (n.part != Part::Row1) continue;
        // Row 1 is a bass row: its pitches are the root 33 plus scale steps and octaves; transposed by 5.
        const bool transposed = ((n.pitch - 33 - 5) % 12 == 0) || n.beat >= 10.25;
        if (n.beat < 10.25 && transposed && (n.pitch - 33) % 12 != 0 && (n.pitch - 38) % 12 == 0) ++wrong;
        if (n.beat < 10.1 && n.beat + n.length > 10.1 && n.beat + n.length > 10.25) ++wrong;   // a note across the change
    }
    check(wrong == 0, "transposition only at step boundaries", fmt("%d notes wrong", wrong));

    Score m;
    const int mutations = play(3, 1.0f, m);
    // Row 1 runs 64 beats = 256 sixteenths = 16 cycles; a chance of one mutates at the end of each.
    check(mutations == 16, "one mutation per cycle at chance 1", fmt("%d mutations", mutations));
    int silentDownbeats = 0;
    for (double beat = 0.0; beat < 64.0; beat += 4.0) {
        bool found = false;
        for (const NoteEvent& n : m.notes) found = found || (n.part == Part::Row1 && n.beat == beat);
        if (!found) ++silentDownbeats;
    }
    check(silentDownbeats == 0, "step 0 of the row always sounds", fmt("%d silent", silentDownbeats));
    check(conjunctionSteps(16, 13) == 208, "sixteen against thirteen meet after 208 steps", "");
}

/**
 * The transposer row (PLAN 5.1): every note of a note row is shifted by the root of the transposer step
 * that sounds at its beat -- from the downbeat of the bar on, never a step late -- and the progression
 * starts and ends on the tonic.
 */
void testTransposer()
{
    section("transposer row");
    auto play = [](bool transposer, std::vector<int>* roots) {
        ParamStore p;
        p.parseText("compose.style=Melodic row1.active=1 row1.length=16 row1.division=1/16 "
                    "row3.length=8 row3.division=1 Bar row3.mode=Transposer");
        p.set(p.find("row3.active"), transposer ? 1.0f : 0.0f);
        Score s;
        s.clear(120.0);
        Rack r;
        r.setup(p, 11);
        r.generate(0, RowRole::Bass);
        r.generate(2, RowRole::Transposer);
        if (roots != nullptr) for (int i = 0; i < 8; ++i) roots->push_back(r.steps(2)[i].degree);
        r.run(s, 64.0);
        s.sort();
        return s;
    };
    std::vector<int> roots;
    const Score plain = play(false, nullptr), moved = play(true, &roots);
    int wrong = 0, moves = 0;
    for (size_t i = 0; i < plain.notes.size() && i < moved.notes.size(); ++i) {
        const int bar = static_cast<int>(plain.notes[i].beat / 4.0);
        if (moved.notes[i].pitch - plain.notes[i].pitch != roots[static_cast<size_t>(bar % 8)]) ++wrong;
        moves += roots[static_cast<size_t>(bar % 8)] != 0 ? 1 : 0;
    }
    check(plain.notes.size() == moved.notes.size() && wrong == 0, "each note in the root of its bar",
          fmt("%d of %zu wrong", wrong, plain.notes.size()));
    check(moves > 0 && roots.front() == 0 && roots.back() == 0, "the progression moves and comes home",
          fmt("roots %d %d %d %d %d %d %d %d", roots[0], roots[1], roots[2], roots[3], roots[4], roots[5], roots[6], roots[7]));
}

/**
 * The hands (PLAN 6.4): never more than two movements at once, never a jump (every movement starts where
 * the knob was left), every target inside the knob's range, and the rate the defaults are calibrated to
 * (GestureEngine.h: the references move about five times a minute).
 */
void testHands()
{
    section("the player's hands");
    Score s;
    s.clear(118.0);
    std::vector<HandKnob> knobs;
    for (int k = 0; k < 5; ++k) {
        HandKnob h;
        h.param = 100 + k;
        h.low = -0.3f; h.high = 0.35f; h.atRest = -0.2f; h.atPeak = 0.25f;
        h.throws = k == 4;
        knobs.push_back(h);
    }
    Rng rng;
    rng.seed(21);
    const double end = 118.0 * 10.0;   // ten minutes
    playHands(s, knobs, HandStyle{}, [&](double b) { return static_cast<float>(b / end); }, 0.0, end, rng);
    s.sort();
    // Concurrency: sweep over starts and ends of the movements (steps of zero length excluded).
    std::vector<std::pair<double, int>> edges;
    int moves = 0;
    for (const Gesture& g : s.gestures) {
        if (g.length <= 0.0) continue;
        edges.push_back({ g.beat, 1 });
        edges.push_back({ g.beat + g.length, -1 });
        ++moves;
    }
    std::sort(edges.begin(), edges.end());   // ends (-1) before starts (+1) at equal beats
    int live = 0, most = 0;
    for (const auto& e : edges) { live += e.second; most = std::max(most, live); }
    check(most <= 2, "never more than two hands", fmt("%d at once", most));
    int jumps = 0, outside = 0;
    for (const HandKnob& k : knobs) {
        float last = 0.0f;
        bool first = true;
        for (const Gesture& g : s.gestures) {
            if (g.param != k.param) continue;
            if (!first && std::fabs(g.from - last) > 1e-6f) ++jumps;
            if (g.to < k.low - 1e-6f || g.to > k.high + 1e-6f) ++outside;
            last = g.to;
            first = false;
        }
    }
    check(jumps == 0, "every movement starts where the knob was left", fmt("%d jumps", jumps));
    check(outside == 0, "every target inside the knob's range", fmt("%d outside", outside));
    const double perMinute = moves / 10.0;
    check(perMinute > 3.5 && perMinute < 6.5, "4 to 6 movements a minute (references: about 5)", fmt("%.1f per minute", perMinute));
}

/**
 * The lead (Lead.h): every note in the scale of the root sounding at its beat, inside the register,
 * one note at a time except the glides from below, and phrases with rests between them.
 */
void testLead()
{
    section("lead phrases");
    LeadPlan lp;
    lp.keyRoot = 9;
    lp.scale = 3;   // harmonic minor: the glide from a whole tone below is not always in the scale
    lp.low = 64;
    lp.high = 86;
    lp.intensity = 0.6f;
    lp.shifts = { { 0.0, 0 }, { 64.0, -4 }, { 128.0, -2 }, { 192.0, 0 } };
    Score s;
    s.clear(118.0);
    Rng rng;
    rng.seed(5);
    writeLead(s, lp, 0.0, 256.0, rng);
    s.sort();
    int outOfScale = 0, outOfRange = 0, overlaps = 0, phrases = 0;
    double lastEnd = -100.0;
    for (size_t i = 0; i < s.notes.size(); ++i) {
        const NoteEvent& n = s.notes[i];
        int shift = 0;
        for (const auto& e : lp.shifts) if (e.first <= n.beat) shift = e.second;
        if (!inScale(n.pitch, ((lp.keyRoot + shift) % 12 + 12) % 12, lp.scale)) ++outOfScale;
        if (n.pitch < lp.low || n.pitch > lp.high) ++outOfRange;
        if (i > 0 && n.beat < lastEnd - 1e-9 && !s.notes[i - 1].slide) ++overlaps;
        if (n.beat - lastEnd >= kBeatsPerBar - 1e-9) ++phrases;
        lastEnd = n.beat + n.length;
    }
    check(!s.notes.empty() && outOfScale == 0, "every note in the scale of its root", fmt("%d of %zu out", outOfScale, s.notes.size()));
    check(outOfRange == 0, "every note inside the register", fmt("%d outside", outOfRange));
    check(overlaps == 0, "one note at a time, except a glide", fmt("%d overlaps", overlaps));
    check(phrases >= 4, "phrases with rests of a bar or more between them", fmt("%d phrases in 64 bars", phrases));
}

/**
 * The tape keyboard's machine (TapeKeys.h): a held key falls silent when its tape ends, however long it
 * is held; it sounds before that; and the capstan slows with every key pressed.
 */
void testTapeKeys()
{
    section("tape keyboard");
    TapeKeys t;
    t.prepare(48000.0, 3);
    TapeSettings s;
    s.age = 0.0f;   // no hiss, so silence is silence
    t.set(s);
    t.noteOn(57, 0.8f, 1);
    std::vector<float> out(48000 * 10);
    for (size_t i = 0; i < out.size(); i += 32) t.process(out.data() + i, 32);
    auto energy = [&](double a, double b) {
        double e = 0.0;
        for (size_t i = static_cast<size_t>(a * 48000); i < static_cast<size_t>(b * 48000); ++i) e += static_cast<double>(out[i]) * out[i];
        return e;
    };
    const double sounding = energy(2.0, 3.0), after = energy(8.2, 9.2);
    check(sounding > 1.0, "a pressed key sounds", fmt("%.1f", sounding));
    check(after < sounding * 1e-6, "silent after the tape ends, key still held", fmt("%.1f dB below", 10.0 * std::log10(sounding / std::max(after, 1e-30))));
    check(!t.active(), "the key is free again", "");
    s.sagCents = 1.5f;
    t.set(s);
    check(t.speedFor(1) == 1.0 && t.speedFor(3) < 1.0 && t.speedFor(6) < t.speedFor(3), "the capstan slows with every key",
          fmt("%.2f and %.2f cents at 3 and 6 keys", 1200.0 * std::log2(t.speedFor(3)), 1200.0 * std::log2(t.speedFor(6))));
}

/**
 * The chords (Pads.h): every tone in the scale built on the root sounding at its beat, no chord longer
 * than a tape can hold, and the voices moving little from chord to chord.
 */
void testChords()
{
    section("held chords");
    PadPlan pp;
    pp.keyRoot = 9;
    pp.scale = 0;
    pp.shifts = { { 0.0, 0 }, { 32.0, -4 }, { 64.0, -2 }, { 96.0, 3 }, { 128.0, 0 } };
    Score s;
    s.clear(118.0);
    Rng rng;
    rng.seed(8);
    const int chords = writeChords(s, pp, 0.0, 160.0, rng);
    s.sort();
    int outside = 0;
    double longest = 0.0;
    for (const NoteEvent& n : s.notes) {
        int shift = 0;
        for (const auto& e : pp.shifts) if (e.first <= n.beat) shift = e.second;
        if (!inScale(n.pitch, ((pp.keyRoot + shift) % 12 + 12) % 12, pp.scale)) ++outside;
        longest = std::max(longest, s.tempo.secondsAt(n.beat + n.length) - s.tempo.secondsAt(n.beat));
    }
    // Movement: the mean distance of each chord's lowest voice from the one before.
    // A chord is the notes struck within a tenth of a beat of its first (the fingers are a few ms apart).
    std::vector<int> lows;
    double chordStart = -1.0;
    for (const NoteEvent& n : s.notes) {
        if (n.beat - chordStart > 0.1) { lows.push_back(n.pitch); chordStart = n.beat; }
        else lows.back() = std::min(lows.back(), n.pitch);
    }
    double move = 0.0;
    for (size_t i = 1; i < lows.size(); ++i) move += std::abs(lows[i] - lows[i - 1]);
    move /= std::max<size_t>(1, lows.size() - 1);
    check(chords >= 5 && outside == 0, "every tone in the scale of its root", fmt("%d chords, %d tones outside", chords, outside));
    check(longest < kTapeSeconds - 0.5, "no chord held longer than a tape", fmt("longest %.2f s", longest));
    check(move <= 3.0, "the lowest voice moves little", fmt("%.1f semitones on average", move));
}

/**
 * The offline render is the oracle only if a host's block size cannot change a sample: the study
 * rendered with blocks of 1, 37 and 512 must agree bit for bit (Engine.h).
 */
void testBlockSizes()
{
    section("bit-identical output for any block size");
    auto render = [](int block) {
        Engine e;
        e.prepare(48000.0, block);
        e.load(buildStudy(e.params(), 5, 1.2));
        std::vector<float> out;
        const int total = 48000 * 20;
        std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
        for (int done = 0; done < total; done += block) {
            const int n = std::min(block, total - done);
            e.process(L.data(), R.data(), n);
            out.insert(out.end(), L.begin(), L.begin() + n);
            out.insert(out.end(), R.begin(), R.begin() + n);
        }
        return out;
    };
    // Interleaving differs with the block size, so compare per channel: deinterleave by blocks.
    auto channels = [](const std::vector<float>& v, int block) {
        std::vector<float> l, r;
        const size_t total = v.size() / 2;
        size_t pos = 0;
        for (size_t done = 0; done < total; done += static_cast<size_t>(block)) {
            const size_t n = std::min(static_cast<size_t>(block), total - done);
            l.insert(l.end(), v.begin() + static_cast<std::ptrdiff_t>(pos), v.begin() + static_cast<std::ptrdiff_t>(pos + n));
            r.insert(r.end(), v.begin() + static_cast<std::ptrdiff_t>(pos + n), v.begin() + static_cast<std::ptrdiff_t>(pos + 2 * n));
            pos += 2 * n;
        }
        l.insert(l.end(), r.begin(), r.end());
        return l;
    };
    const std::vector<float> a = channels(render(512), 512);
    double energy = 0.0;
    for (float x : a) energy += static_cast<double>(x) * x;
    check(energy > 1.0, "the study makes sound", fmt("energy %.1f", energy));
    for (int block : { 1, 37 }) {
        const std::vector<float> b = channels(render(block), block);
        size_t first = 0;
        while (first < a.size() && first < b.size() && std::memcmp(&a[first], &b[first], sizeof(float)) == 0) ++first;
        check(a.size() == b.size() && first == a.size(), fmt("blocks of %d equal blocks of 512", block).c_str(),
              first < a.size() ? fmt("first difference at sample %zu of %zu (%.9g against %.9g)", first % (a.size() / 2), a.size() / 2,
                                     static_cast<double>(a[first]), static_cast<double>(b[first])) : std::string());
    }
}

/**
 * The tape echo (PLAN 5.8): with the head standing still, the repeats of an impulse come at the delay,
 * each quieter than the one before and darker, because the loop's low pass takes the top off every pass.
 */
void testEcho()
{
    section("tape echo repeats");
    TapeEcho e;
    const double sr = 48000.0;
    e.prepare(sr, 1.0, 1);
    EchoSettings s;
    s.delaySeconds = 0.25;
    s.feedback = 0.6f;
    s.toneHz = 3000.0f;
    s.wowMs = 0.0f;
    s.flutterMs = 0.0f;
    s.driveDb = 0.0f;
    s.pingPong = false;
    e.set(s);
    const int n = static_cast<int>(sr * 1.2);
    std::vector<float> in(static_cast<size_t>(n), 0.0f), outL(static_cast<size_t>(n), 0.0f), outR(static_cast<size_t>(n), 0.0f);
    in[0] = 1.0f;
    for (int i = 0; i < n; i += 32) {
        const int m = std::min(32, n - i);
        e.process(in.data() + i, in.data() + i, outL.data() + i, outR.data() + i, m);
    }
    const int d = 12000;
    double prevE = 1e9, prevB = 1e9;
    bool ok = true;
    std::string detail;
    for (int k = 1; k <= 4; ++k) {
        // Energy and brightness (share of the first difference) in a window around repeat k.
        double en = 0.0, diff = 0.0;
        int peakAt = 0;
        float peak = 0.0f;
        for (int i = k * d - 300; i < k * d + 300; ++i) {
            const float x = outL[static_cast<size_t>(i)];
            en += static_cast<double>(x) * x;
            const float dx = x - outL[static_cast<size_t>(i - 1)];
            diff += static_cast<double>(dx) * dx;
            if (std::fabs(x) > peak) { peak = std::fabs(x); peakAt = i; }
        }
        const double bright = diff / std::max(en, 1e-30);
        // The loop's filters add a few samples of group delay per pass, as a real loop's do.
        ok = ok && std::abs(peakAt - k * d) <= 5 * k && en < prevE && bright < prevB;
        detail += fmt("#%d at %+d, %.1f dB, brightness %.4f; ", k, peakAt - k * d, 10.0 * std::log10(en), bright);
        prevE = en;
        prevB = bright;
    }
    check(ok, "repeats on time (within the loop's group delay), each quieter and darker", detail);
}

/** The drift (ModVoice.h): the Ornstein-Uhlenbeck spread must be what the knob says, in cents. */
void testDrift()
{
    section("oscillator drift spread");
    OuProcess ou;
    Rng rng;
    rng.seed(9);
    const double dt = 32.0 / 48000.0, tau = 14.0, sigma = 3.0;
    double sum = 0.0, sumSq = 0.0;
    const int steps = static_cast<int>(3600.0 / dt);   // an hour of drift
    for (int i = 0; i < steps; ++i) {
        const double x = ou.step(dt, tau, sigma, rng);
        sum += x;
        sumSq += x * x;
    }
    const double mean = sum / steps, sd = std::sqrt(sumSq / steps - mean * mean);
    check(std::fabs(sd - sigma) < 0.15 * sigma, "stationary spread equals the knob", fmt("%.2f cents for 3", sd));
}

struct Section {
    const char* name;
    std::function<void()> fn;
};

const Section kSections[] = {
    { "testTempoMap", testTempoMap },
    { "testGestures", testGestures },
    { "testParams", testParams },
    { "testMidiTempo", testMidiTempo },
    { "testRack", testRack },
    { "testTransposer", testTransposer },
    { "testHands", testHands },
    { "testLead", testLead },
    { "testTapeKeys", testTapeKeys },
    { "testChords", testChords },
    { "testBlockSizes", testBlockSizes },
    { "testEcho", testEcho },
    { "testDrift", testDrift },
};

} // namespace

int main(int argc, char** argv)
{
    std::string only;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0) {
            for (const Section& s : kSections) std::printf("%s\n", s.name);
            return 0;
        }
        if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) only = std::string(",") + argv[++i] + ",";
    }
    for (const Section& s : kSections)
        if (only.empty() || only.find(std::string(",") + s.name + ",") != std::string::npos) s.fn();
    return finish();
}
