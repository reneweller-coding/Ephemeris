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
#include "eph/Midi.h"
#include "eph/ModVoice.h"
#include "eph/Params.h"
#include "eph/Rack.h"
#include "eph/Score.h"
#include "eph/Study.h"
#include "eph/TapeEcho.h"
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
