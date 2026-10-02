/**
 * @file selftest.cpp
 * @brief eph_selftest: every building block measured against an independently derived value.
 *
 * Sections are registered in the table at the bottom; `eph_selftest --list` prints their names (ctest
 * registers one test per name) and `--only a,b` runs the named ones. Only checks that protect
 * something real belong here (the user's rule, 24.09.2026): no test of what cannot break.
 */
#include "eph/Clock.h"
#include "eph/compose/Composer.h"
#include "eph/Engine.h"
#include "eph/Loudness.h"
#include "eph/Leveler.h"
#include "eph/Presets.h"
#include "eph/Cue.h"
#include "eph/fx/Plate.h"
#include "eph/synth/Atmos.h"
#include "eph/synth/Modulation.h"
#include "eph/synth/Vco.h"
#include "eph/synth/VoiceKernel.h"
#include "eph/synth/Wavetable.h"
#include "eph/synth/StringMachine.h"
#include "eph/fx/Bbd.h"
#if defined(_WIN32)
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <winsock2.h>
  #include <ws2tcpip.h>
  using socklen_t = int;
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
#endif
#include "eph/compose/Form.h"
#include "eph/compose/GestureEngine.h"
#include "eph/compose/Harmony.h"
#include "eph/compose/Lead.h"
#include "eph/Midi.h"
#include "eph/NoteTap.h"
#include "eph/synth/ModVoice.h"
#include "eph/compose/Pads.h"
#include "eph/Params.h"
#include "eph/synth/Filters.h"
#include "eph/synth/Wavetable.h"
#include "eph/synth/Poly.h"
#include "eph/Rack.h"
#include "eph/Score.h"
#include "eph/SetFile.h"
#include "eph/Study.h"
#include "eph/fx/TapeEcho.h"
#include "eph/synth/TapeKeys.h"
#include "TestSupport.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <set>
#include <vector>

using namespace eph;
using namespace ephtest;

namespace {

/** @brief Knob @p id where piece @p s begins: its setting on the knob (Score::knobs, 26.09.2026), else the knob in @p p. */
float knobAt(const ParamStore& p, const Score& s, int id)
{
    float v = p.get(id);
    for (const KnobSet& k : s.knobs) if (k.param == id && k.beat <= 1e-9) v = k.value;
    return v;
}

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
    // Row 1 runs 64 beats = 16 bars: one 16-bar block, so a chance of one mutates once, at its end.
    check(mutations == 1, "one mutation per 16-bar block at chance 1", fmt("%d mutations", mutations));
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
 * The form (Form.h) for every style and a handful of seeds: sections follow each other without gap or
 * overlap, the piece opens with the atmosphere and closes with the coda, every phase has an entry, a
 * build and a peak, phases are joined by bridges, no section is shorter than its floor.
 */
void testForm()
{
    section("form grammar");
    int bad = 0, pieces = 0;
    std::string why;
    for (int st = 0; st < static_cast<int>(Style::Count); ++st) {
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            Rng rng;
            rng.seed(seed);
            const PieceForm f = drawForm(styleProfile(static_cast<Style>(st)), 14.0, 110.0, rng);
            ++pieces;
            double at = 0.0;
            int entries = 0, peaks = 0, bridges = 0;
            bool ok = f.sections.front().type == SectionType::Atmo && f.sections.back().type == SectionType::Coda;
            for (const Section& s : f.sections) {
                ok = ok && std::fabs(s.beat - at) < 1e-9 && s.length >= 4 * kBeatsPerBar - 1e-9;
                if ((s.type == SectionType::Peak || s.type == SectionType::Lead) && s.length < 8 * kBeatsPerBar) ok = false;
                at = s.beat + s.length;
                entries += s.type == SectionType::Entry;
                peaks += s.type == SectionType::Peak;
                bridges += s.type == SectionType::Bridge;
            }
            const int phases = static_cast<int>(f.phaseBpm.size());
            ok = ok && std::fabs(at - f.lengthBeats) < 1e-9 && entries == phases && peaks == phases && bridges == phases - 1;
            if (!ok) { ++bad; if (why.empty()) why = fmt("style %d seed %llu", st, static_cast<unsigned long long>(seed)); }
        }
    }
    check(bad == 0, "every form follows the grammar", fmt("%d of %d bad %s", bad, pieces, why.c_str()));
}

/** Conjunctions as form boundaries (Form.h): builds end where their row meets the bass row, the grammar holds. */
void testConjunctions()
{
    section("conjunctions as form boundaries");
    const double bass = 4.0;
    const std::vector<double> rows = { 13 * 0.25, 7 * 0.5, 9 * 0.25 };   // 13 sixteenths, 7 eighths, 9 sixteenths
    int bad = 0, builds = 0, onConjunction = 0, moved = 0;
    for (int st = 0; st < static_cast<int>(Style::Count); ++st) {
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            Rng rng;
            rng.seed(seed);
            PieceForm f = drawForm(styleProfile(static_cast<Style>(st)), 14.0, 110.0, rng);
            const double length = f.lengthBeats;
            moved += snapToConjunctions(f, bass, rows);
            double at = 0.0;
            for (const Section& sec : f.sections) {
                if (std::fabs(sec.beat - at) > 1e-9 || sec.length < 4 * kBeatsPerBar - 1e-9) ++bad;
                if (std::fmod(sec.beat, kBeatsPerBar) > 1e-9) ++bad;
                at = sec.beat + sec.length;
                if (sec.type == SectionType::Build && sec.index < static_cast<int>(rows.size())) {
                    ++builds;
                    const double period = sec.index == 0 ? 52.0 : sec.index == 1 ? 28.0 : 36.0;   // lcm with the bass
                    const double q = sec.length / period;
                    if (std::fabs(q - std::round(q)) < 1e-9) ++onConjunction;
                }
            }
            if (std::fabs(at - length) > 1e-9) ++bad;
        }
    }
    check(bad == 0, "the form stays whole: no gaps, bars, floors, the same length", fmt("%d problems", bad));
    check(onConjunction * 2 > builds, "most builds end on a conjunction of their row with the bass",
          fmt("%d of %d (%d moved)", onConjunction, builds, moved));
}

/** The style morph of a concert (Style.h, morphProfile): the ends exact, the middle between them. */
void testMorph()
{
    section("style morph");
    const StyleProfile& a = styleProfile(Style::Cosmic);
    const StyleProfile& b = styleProfile(Style::Modern);
    const StyleProfile m0 = morphProfile(a, b, 0.0f), m1 = morphProfile(a, b, 1.0f), mh = morphProfile(a, b, 0.5f);
    const bool ends = m0.bpmLow == a.bpmLow && m0.hallSeconds == a.hallSeconds && m0.peakRows == a.peakRows
                   && m1.bpmHigh == b.bpmHigh && m1.drumsChance == b.drumsChance && m1.tape == b.tape;
    auto between = [](float x, float lo, float hi) { return x >= std::min(lo, hi) && x <= std::max(lo, hi); };
    const bool middle = between(mh.bpmLow, a.bpmLow, b.bpmLow) && between(mh.hallSeconds, a.hallSeconds, b.hallSeconds)
                     && between(mh.drumsChance, a.drumsChance, b.drumsChance) && mh.phasesHigh >= mh.phasesLow;
    const StyleProfile flat = arcProfile(a, 0.3f, 0.0f), high = arcProfile(a, 0.5f, 1.0f), low = arcProfile(a, -0.5f, 1.0f);
    const bool arc = flat.peakRows == a.peakRows && flat.leadChance == a.leadChance
                  && high.peakRows > low.peakRows && high.leadChance > low.leadChance && high.bpmHigh > low.bpmHigh
                  && concertArc(0.0f) < 0.01f && concertArc(1.0f) < 0.01f && concertArc(0.6f) > 0.99f;
    check(arc, "the concert's arc: nothing at strength 0, denser at the height than at the ends, its peak at 60 %",
          fmt("rows %d / %d, lead %.2f / %.2f", low.peakRows, high.peakRows, static_cast<double>(low.leadChance), static_cast<double>(high.leadChance)));
    // The user's own style: Cosmic plays three rows at the peak; the custom style with six, once it is switched on.
    auto rowsStarted = [](const char* set) {
        ParamStore q;
        q.parseText(set);
        const Score sc = composePiece(q, 9, 8.0);
        bool started[kRows] = {};
        for (const RackEvent& e : sc.rack) if (e.op == RackOp::Start && e.row >= 0 && e.row < kRows) started[e.row] = true;
        int n = 0;
        for (bool b : started) n += b;
        return n;
    };
    const int plain = rowsStarted("compose.style=Cosmic custom.peak_rows=6"), own = rowsStarted("compose.style=Cosmic custom.use=1 custom.peak_rows=6");
    check(own > plain, "the custom style is used when it is switched on", fmt("rows started: %d off, %d on", plain, own));
    check(ends && middle, "the ends are the two profiles, the middle lies between them",
          fmt("bpm %.0f-%.0f / %.0f-%.0f / %.0f-%.0f", static_cast<double>(m0.bpmLow), static_cast<double>(m0.bpmHigh),
              static_cast<double>(mh.bpmLow), static_cast<double>(mh.bpmHigh), static_cast<double>(m1.bpmLow), static_cast<double>(m1.bpmHigh)));
}

/**
 * The composer (Composer.h): the same seed writes the same piece; a piece is as long as asked (to the
 * bar rounding); every note of the rows, the lead, the chords and the drone is in the scale of the root
 * sounding at its beat; the hands never exceed two; a concert moves through keys.
 */
void testComposer()
{
    section("composer");
    ParamStore p;
    p.parseText("compose.style=Melodic compose.scale=Aeolian");
    const Score a = composePiece(p, 77, 10.0), b = composePiece(p, 77, 10.0);
    bool same = a.notes.size() == b.notes.size() && a.gestures.size() == b.gestures.size();
    for (size_t i = 0; same && i < a.notes.size(); ++i) same = a.notes[i].beat == b.notes[i].beat && a.notes[i].pitch == b.notes[i].pitch;
    check(same && a.notes.size() > 1000, "same seed, same piece", fmt("%zu notes, %zu gestures", a.notes.size(), a.gestures.size()));
    const double secs = a.tempo.secondsAt(a.lengthBeats);
    check(std::fabs(secs - 600.0) < 60.0, "as long as asked", fmt("%.0f s for 600", secs));
    int outside = 0, total = 0;
    for (const NoteEvent& n : a.notes) {
        if (n.part == Part::Drums) continue;   // General MIDI instrument numbers, not pitches
        int shift = 0;
        for (const auto& e : a.rootShifts) { if (e.first > n.beat) break; shift = e.second; }
        ++total;
        if (!inScale(n.pitch, ((a.keyRoot + shift) % 12 + 12) % 12, a.scaleAt(n.beat, 0))) ++outside;
    }
    check(outside == 0, "every note in the scale of the root at its beat", fmt("%d of %d outside", outside, total));
    std::vector<std::pair<double, int>> edges;
    for (const Gesture& g : a.gestures) {
        if (g.length <= 0.0 || g.hand > 1) continue;
        edges.push_back({ g.beat, 1 });
        edges.push_back({ g.beat + g.length, -1 });
    }
    std::sort(edges.begin(), edges.end());
    int live = 0, most = 0;
    for (const auto& e : edges) { live += e.second; most = std::max(most, live); }
    // The hands' two, plus the composer's own slow moves of the wind on the atmosphere (hand 1's knob).
    check(most <= 3, "the hands and the wind: never more than three movements at once", fmt("%d", most));
    const Score c = composeConcert(p, 5, 40.0);
    int pieces = 0;
    for (const Marker& m : c.markers) pieces += m.text.find(": Atmo") != std::string::npos;
    std::vector<int> keys;
    for (const auto& e : c.rootShifts) keys.push_back(e.second);
    check(pieces >= 3 && std::fabs(c.tempo.secondsAt(c.lengthBeats) - 2400.0) < 600.0, "a concert of pieces, about as long as asked",
          fmt("%d pieces, %.0f s", pieces, c.tempo.secondsAt(c.lengthBeats)));
}

/**
 * Curation (SetFile.h): rerolling the lead draws new lead notes and leaves every other note and every
 * gesture as it was; a set file brings the same piece back.
 */
void testCuration()
{
    section("curation and set files");
    ParamStore p;
    p.parseText("compose.style=Cosmic");
    const Score a = composePiece(p, 21, 8.0);
    Curation c;
    c.reroll("lead");
    const Score b = composePiece(p, 21, 8.0, 0, &c);
    auto without = [](const Score& s, Part part) {
        std::vector<std::pair<double, int>> v;
        for (const NoteEvent& n : s.notes) if (n.part != part) v.push_back({ n.beat, n.pitch });
        return v;
    };
    auto only = [](const Score& s, Part part) {
        std::vector<std::pair<double, int>> v;
        for (const NoteEvent& n : s.notes) if (n.part == part) v.push_back({ n.beat, n.pitch });
        return v;
    };
    bool gesturesSame = a.gestures.size() == b.gestures.size();
    for (size_t i = 0; gesturesSame && i < a.gestures.size(); ++i)
        gesturesSame = a.gestures[i].beat == b.gestures[i].beat && a.gestures[i].to == b.gestures[i].to;
    const bool hasLead = !only(a, Part::Lead).empty();
    check(without(a, Part::Lead) == without(b, Part::Lead) && gesturesSame, "a reroll of the lead leaves the rest bit for bit", "");
    check(!hasLead || only(a, Part::Lead) != only(b, Part::Lead), "and draws the lead again", hasLead ? "" : "no lead in this piece");

    SetFile sf;
    sf.seed = 21;
    sf.minutes = 8.0;
    sf.curation = c;
    const std::string path = "eph_selftest.ephset";
    const bool saved = saveSet(path.c_str(), sf, p);
    ParamStore q;
    SetFile back;
    std::string err;
    const bool loaded = loadSet(path.c_str(), back, q, &err);
    std::remove(path.c_str());
    const Score d = composePiece(q, back.seed, back.minutes, 0, &back.curation);
    check(saved && loaded && without(d, Part::Count) == without(b, Part::Count), "a set file brings the same piece back", err);
}

/** The perform controls (Params.h, perform): neutral at their defaults, audible when moved. */
void testPerform()
{
    section("perform controls");
    ParamStore base;
    // The voices as the settings make them (not a drawn preset), and only the rows: the brightness is theirs.
    base.parseText("compose.style=Melodic compose.pick_sounds=0 drone.level=-60 tape.level=-60 strings.level=-60 lead.level=-60 atmos.level=-60 drums.level=-60 poly.level=-60");
    const Score score = composePiece(base, 5, 4.0);
    auto render = [&](const char* setting) {
        Engine e;
        e.params().copyValuesFrom(base);
        e.params().parseText(setting);
        e.prepare(48000.0, 256);
        e.load(score);
        e.seek(96.0);
        std::vector<float> out;
        std::vector<float> L(256), R(256);
        for (int done = 0; done < 48000 * 10; done += 256) {
            e.process(L.data(), R.data(), 256);
            out.insert(out.end(), L.begin(), L.end());
        }
        return out;
    };
    auto rms = [](const std::vector<float>& v) { double s = 0.0; for (float x : v) s += double(x) * x; return std::sqrt(s / double(v.size())); };
    // Brightness: the energy of the first difference against the energy of the signal.
    auto bright = [](const std::vector<float>& v) {
        double d = 0.0, e = 0.0;
        for (size_t i = 1; i < v.size(); ++i) { d += double(v[i] - v[i - 1]) * (v[i] - v[i - 1]); e += double(v[i]) * v[i]; }
        return d / std::max(1e-12, e);
    };
    auto diff = [](const std::vector<float>& a, const std::vector<float>& b) {
        double s = 0.0; for (size_t i = 0; i < a.size(); ++i) s += double(a[i] - b[i]) * (a[i] - b[i]); return std::sqrt(s / double(a.size()));
    };
    const auto plain = render("");
    const auto neutral = render("perform.filter=0 perform.transpose=0 perform.hold=0 perform.throw=0");
    check(diff(plain, neutral) == 0.0, "the controls at their defaults change nothing", "bit for bit");
    const auto open = render("perform.filter=2"), shut = render("perform.filter=-2");
    check(bright(open) > 1.2 * bright(plain) && bright(shut) < 0.8 * bright(plain), "the filter hand opens and closes the rows",
          fmt("brightness %.4f / %.4f / %.4f", bright(shut), bright(plain), bright(open)));
    const auto up = render("perform.transpose=7");
    check(diff(up, plain) > 0.2 * rms(plain), "the transposition key moves the notes", fmt("difference %.3f of rms %.3f", diff(up, plain), rms(plain)));
    const auto thrown = render("perform.throw=1");
    check(rms(thrown) > rms(plain) * 1.05, "the echo throw adds its repeats", fmt("rms %.4f against %.4f", rms(thrown), rms(plain)));
    const auto held = render("perform.hold=1");
    check(diff(held, plain) > 0.0, "the hold keeps the gestures where they stood", fmt("difference %.4f", diff(held, plain)));
}

/** The score cues (Cue.h): the OSC bytes as the specification lays them out, the marks of a piece, the tap. */
void testCues()
{
    section("score cues (OSC)");
    const char* d = "D";
    const std::vector<uint8_t> key = oscMessage("/eph/key", "s", nullptr, nullptr, &d);
    const uint8_t keyWant[20] = { '/', 'e', 'p', 'h', '/', 'k', 'e', 'y', 0, 0, 0, 0, ',', 's', 0, 0, 'D', 0, 0, 0 };
    const int32_t five = 5;
    const float bpm = 132.0f;
    const std::vector<uint8_t> beat = oscMessage("/eph/beat", "if", &five, &bpm, nullptr);
    const uint8_t beatWant[24] = { '/', 'e', 'p', 'h', '/', 'b', 'e', 'a', 't', 0, 0, 0, ',', 'i', 'f', 0, 0, 0, 0, 5, 0x43, 0x04, 0, 0 };
    check(key.size() == 20 && std::memcmp(key.data(), keyWant, 20) == 0 && beat.size() == 24 && std::memcmp(beat.data(), beatWant, 24) == 0,
          "OSC 1.0 byte layout (padding to four bytes, big-endian numbers)", fmt("%zu and %zu bytes", key.size(), beat.size()));

    ParamStore p;
    p.parseText("compose.style=Melodic");
    const Score score = composePiece(p, 5, 10.0);
    const std::vector<CueMark> marks = cueMarksOf(score);
    int phases = 0, keys = 0, conj = 0, named = 0;
    bool ordered = true, twoOrMore = true;
    for (size_t i = 0; i < marks.size(); ++i) {
        if (i > 0 && marks[i].beat < marks[i - 1].beat) ordered = false;
        if (marks[i].kind == CueKind::Phase) { ++phases; named += marks[i].text[0] != 0; }
        if (marks[i].kind == CueKind::Key) ++keys;
        if (marks[i].kind == CueKind::Conjunction) { ++conj; twoOrMore = twoOrMore && marks[i].a >= 2; }
    }
    check(ordered && phases == static_cast<int>(score.markers.size()) && named == phases && keys >= 1 && conj > 0 && twoOrMore,
          "the marks of a piece: every section named, the keys, conjunctions of two rows or more",
          fmt("%d sections of %zu markers, %d keys, %d conjunctions", phases, score.markers.size(), keys, conj));

    // The tap over the whole piece in blocks: every mark once, in order, and a beat cue per beat.
    CueTap tap;
    CueRing ring;
    int got = 0, beats = 0, lost = 0;
    const double step = 0.37;
    for (double b = 0.0; b < score.lengthBeats; b += step) {
        lost += tap.scan(marks, b, b + step, 110.0f, 0, 0, 1000000, ring);
        Cue c;
        while (ring.pop(c)) { if (c.kind == CueKind::Beat) ++beats; else ++got; }
    }
    check(lost == 0 && got == static_cast<int>(marks.size()) && std::abs(beats - static_cast<int>(std::ceil(score.lengthBeats))) <= 1,
          "the tap sends every mark once and a cue per beat", fmt("%d of %zu marks, %d beats, %d lost", got, marks.size(), beats, lost));

    // The sender, for real: a datagram through the loopback to a socket of the test's own.
    const std::vector<uint8_t> want = oscOf([] { Cue c; c.kind = CueKind::Key; std::strcpy(c.text, "F#"); return c; }());
    std::vector<uint8_t> heard;
#if defined(_WIN32)
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    const SOCKET rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    DWORD timeout = 2000;
    setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    const int rx = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    timeval timeout{ 2, 0 };
    setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    bind(rx, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    socklen_t len = sizeof(addr);
    getsockname(rx, reinterpret_cast<sockaddr*>(&addr), &len);
    CueSender sender;
    if (sender.start("127.0.0.1", ntohs(addr.sin_port))) {
        Cue c;
        c.kind = CueKind::Key;
        std::strcpy(c.text, "F#");
        c.dueNanos = CueSender::nowNanos();
        sender.ring().push(c);
        char buf[256];
        const auto n = recv(rx, buf, sizeof(buf), 0);
        if (n > 0) heard.assign(buf, buf + n);
        sender.stop();
    }
#if defined(_WIN32)
    closesocket(rx);
    WSACleanup();
#else
    close(rx);
#endif
    check(heard == want, "the sender's datagram arrives through the loopback, byte for byte", fmt("%zu bytes", heard.size()));
}

/** The other rooms (PLAN 5.8): the plate decays as set, the BBD repeats on time and darker the longer it is. */
void testRooms()
{
    section("plate and bucket-brigade delay");
    const int sr = 48000;
    // The plate: an impulse, 4 s of response at a set T60 of 3 s.
    Plate plate;
    plate.prepare(sr);
    plate.set(3.0f, 0.3f, 0.0f, 40.0f, 16000.0f);
    std::vector<float> inL(static_cast<size_t>(4 * sr), 0.0f), inR = inL, outL = inL, outR = inL;
    inL[0] = inR[0] = 1.0f;
    for (int i = 0; i < 4 * sr; i += 256) plate.process(inL.data() + i, inR.data() + i, outL.data() + i, outR.data() + i, 256);
    auto energy = [&](double t0, double t1) {
        double e = 0.0;
        for (int i = static_cast<int>(t0 * sr); i < static_cast<int>(t1 * sr); ++i) e += double(outL[static_cast<size_t>(i)]) * outL[static_cast<size_t>(i)] + double(outR[static_cast<size_t>(i)]) * outR[static_cast<size_t>(i)];
        return e / ((t1 - t0) * sr);
    };
    const double drop = 10.0 * std::log10(energy(0.25, 0.45) / std::max(1e-30, energy(3.25, 3.45)));
    double diff = 0.0;
    for (size_t i = 0; i < outL.size(); ++i) diff += std::fabs(outL[i] - outR[i]);
    check(drop > 45.0 && drop < 75.0 && diff > 0.1, "the plate falls about 60 dB over its T60 of 3 s, and its sides differ",
          fmt("%.1f dB in 3 s", drop));

    // The BBD: repeats at 250 ms with feedback 0.5.
    auto run = [&](double seconds, std::vector<float>& out) {
        BbdEcho b;
        b.prepare(sr, 2.5, 3);
        EchoSettings es;
        es.delaySeconds = seconds;
        es.feedback = 0.5f;
        es.toneHz = 12000.0f;
        es.wowMs = 0.0f;
        es.pingPong = false;
        b.set(es);
        std::vector<float> xL(static_cast<size_t>(2 * sr), 0.0f), xR = xL, yR = xL;
        out.assign(static_cast<size_t>(2 * sr), 0.0f);
        // A burst of noise, so the repeats carry a spectrum.
        Rng r;
        r.seed(1);
        for (int i = 0; i < 480; ++i) xL[static_cast<size_t>(i)] = xR[static_cast<size_t>(i)] = r.bipolar();
        for (int i = 0; i < 2 * sr; i += 256) b.process(xL.data() + i, xR.data() + i, out.data() + i, yR.data() + i, 256);
    };
    std::vector<float> y;
    run(0.25, y);
    auto window = [&](const std::vector<float>& v, double t0, double t1) {
        double e = 0.0, d = 0.0;
        for (int i = static_cast<int>(t0 * sr) + 1; i < static_cast<int>(t1 * sr); ++i) {
            e += double(v[static_cast<size_t>(i)]) * v[static_cast<size_t>(i)];
            d += double(v[static_cast<size_t>(i)] - v[static_cast<size_t>(i - 1)]) * (v[static_cast<size_t>(i)] - v[static_cast<size_t>(i - 1)]);
        }
        return std::make_pair(e, d / std::max(1e-30, e));
    };
    const double first = window(y, 0.24, 0.27).first, early = window(y, 0.02, 0.22).first, second = window(y, 0.49, 0.52).first;
    check(first > 100.0 * early && second < first && second > 0.01 * first, "the BBD repeats on time, each repeat weaker",
          fmt("first %.3g, before it %.3g, second %.3g", first, early, second));
    std::vector<float> shortY, longY;
    run(0.1, shortY);
    run(0.6, longY);
    const double brightShort = window(shortY, 0.09, 0.12).second, brightLong = window(longY, 0.59, 0.62).second;
    check(brightLong < 0.7 * brightShort, "a longer BBD delay is darker (its clock is slower)",
          fmt("brightness %.4f at 100 ms, %.4f at 600 ms", brightShort, brightLong));

    // The granular cloud of the atmosphere: silent when off, a cloud when on.
    auto cloud = [&](float gain) {
        Atmos a;
        a.prepare(sr, 5);
        AtmosSettings as;
        as.grainGain = gain;
        as.grainsPerSecond = 20.0f;
        a.set(as);
        std::vector<float> l(static_cast<size_t>(sr), 0.0f), r = l, bl = l, br = l;
        for (int i = 0; i < sr; i += 256) a.process(l.data() + i, r.data() + i, bl.data() + i, br.data() + i, std::min(256, sr - i));
        double e = 0.0;
        for (size_t i = 0; i < l.size(); ++i) e += double(l[i]) * l[i] + double(r[i]) * r[i];
        return std::sqrt(e / (2.0 * sr));
    };
    const double off = cloud(0.0f), on = cloud(0.5f);
    check(off == 0.0 && on > 0.01, "the granular cloud is silent when off and sounds when on", fmt("rms %.4f off, %.4f on", off, on));
}

/** The string machine after the Streichfett (StringMachine.h): registrations, their loudness, the animation, the phaser. */
void testStrings()
{
    section("string machine: registration, animation, phaser");
    const int sr = 48000;
    // A chord held for two seconds; returns the output and, per 100 ms, a brightness (first difference energy / energy).
    auto play = [&](StringSettings st, std::vector<float>& out, std::vector<double>& bright) {
        StringMachine m;
        m.prepare(sr, 7);
        m.set(st);
        for (int k : { 57, 60, 64, 69 }) m.noteOn(k, 0.8f, k);
        const int total = 2 * sr;
        out.assign(static_cast<size_t>(total), 0.0f);
        std::vector<float> r(static_cast<size_t>(total), 0.0f);
        for (int i = 0; i < total; i += 256) m.process(out.data() + i, r.data() + i, std::min(256, total - i));
        bright.clear();
        for (int w = sr / 2; w + sr / 10 <= total; w += sr / 10) {
            double e = 0.0, d = 0.0;
            for (int i = w + 1; i < w + sr / 10; ++i) {
                e += double(out[static_cast<size_t>(i)]) * out[static_cast<size_t>(i)];
                d += double(out[static_cast<size_t>(i)] - out[static_cast<size_t>(i - 1)]) * (out[static_cast<size_t>(i)] - out[static_cast<size_t>(i - 1)]);
            }
            bright.push_back(d / std::max(1e-30, e));
        }
    };
    auto rmsDb = [](const std::vector<float>& v) {
        double e = 0.0;
        for (size_t i = v.size() / 4; i < v.size(); ++i) e += double(v[i]) * v[i];
        return 10.0 * std::log10(e / double(v.size() * 3 / 4) + 1e-30);
    };
    std::vector<float> out;
    std::vector<double> bright;
    StringSettings st;
    st.animate = 0.0f;
    double lo = 1e9, hi = -1e9, violins = 0.0, basses = 0.0;
    for (int r = 0; r < StringMachine::kRegistrations; ++r) {
        st.registration = static_cast<float>(r);
        play(st, out, bright);
        const double db = rmsDb(out);
        lo = std::min(lo, db);
        hi = std::max(hi, db);
        if (r == 0) violins = bright.front();
        if (r == 3) basses = bright.front();
    }
    check(basses < 0.6 * violins, "the registrations differ: the basses are darker than the violins",
          fmt("brightness %.4f against %.4f", basses, violins));
    check(hi - lo < 1.5, "the eight registrations are about as loud as each other", fmt("%.1f .. %.1f dB", lo, hi));
    // The animation moves the mix: the brightness wanders over the two seconds.
    st.registration = 3.0f;
    st.animate = 1.0f;
    st.animateHz = 0.5f;
    play(st, out, bright);
    const double bmin = *std::min_element(bright.begin(), bright.end()), bmax = *std::max_element(bright.begin(), bright.end());
    check(bmax > 1.5 * bmin, "the animation moves the registration by itself", fmt("brightness %.4f .. %.4f", bmin, bmax));
    // The phaser and the ensemble types change the sound.
    std::vector<float> plain, phased, chorus;
    st.animate = 0.0f;
    st.registration = 0.0f;
    play(st, plain, bright);
    st.phaser = 1.0f;
    play(st, phased, bright);
    st.phaser = 0.0f;
    st.ensembleType = 1;
    play(st, chorus, bright);
    double dp = 0.0, dc = 0.0;
    for (size_t i = 0; i < plain.size(); ++i) { dp += std::fabs(plain[i] - phased[i]); dc += std::fabs(plain[i] - chorus[i]); }
    check(dp > 1.0 && dc > 1.0, "the phaser and the ensemble types change the sound", fmt("differences %.1f and %.1f", dp, dc));
}

/**
 * The harmony after the style guide (Harmony.h): the chord track's classes and degrees, the bass row that follows
 * it under the unchanged counter rows, the transposer's moves, the drone on the centre, the open fifth at the end,
 * the parallel change of mode at the peak.
 */
void testHarmony()
{
    section("harmony after the style guide");
    // The chord track: starts on the tonic, changes on bar lines, only usable degrees, the style's classes.
    int badDegree = 0, offBar = 0, statics[static_cast<int>(Style::Count)] = {};
    for (int st = 0; st < static_cast<int>(Style::Count); ++st) {
        for (int scale = 0; scale < kScales; ++scale) {
            Rng rng;
            rng.seed(static_cast<uint64_t>(100 * st + scale + 1));
            for (int k = 0; k < 60; ++k) {
                ChordClass cls;
                const auto track = drawChordTrack(static_cast<Style>(st), scale, 64.0, 64.0 + 4.0 * 96, rng, &cls);
                statics[st] += cls == ChordClass::Static ? 1 : 0;
                if (track.front().first != 64.0 || track.front().second != 0) ++badDegree;
                for (const auto& ch : track) {
                    if (!usableDegree(scale, ch.second)) ++badDegree;
                    if (std::fmod(ch.first - 64.0, 4.0) != 0.0) ++offBar;
                }
            }
        }
    }
    check(badDegree == 0 && offBar == 0, "the chord track: from the tonic, on bar lines, no diminished chord",
          fmt("%d bad degrees, %d off the bar", badDegree, offBar));
    check(statics[static_cast<int>(Style::Cosmic)] > statics[static_cast<int>(Style::Melodic)] * 3 / 2,
          "Cosmic stays on one chord far more often than Melodic", fmt("%d against %d of 480", statics[static_cast<int>(Style::Cosmic)],
          statics[static_cast<int>(Style::Melodic)]));
    // The transposer's moves leave the centre inside the moved scale (no fifth up in Lydian).
    bool lydianFifth = false;
    for (int seed = 1; seed < 40; ++seed) {
        Rng rng;
        rng.seed(static_cast<uint64_t>(seed));
        for (int r : drawProgression(Style::Cosmic, 6, 8, 1, rng)) lydianFifth = lydianFifth || r == 7;
    }
    check(!lydianFifth, "no fifth up in Lydian, where the centre would leave the scale", "");

    // The bass row under a chord (RackOp::Chord): moved diatonically, the counter row untouched.
    {
        auto play = [](bool chord) {
            ParamStore p;
            p.parseText("row1.active=1 row1.length=16 row1.division=1/16 row1.mutation=0 "
                        "row2.active=1 row2.length=12 row2.division=1/16 row2.mutation=0");
            Score s;
            s.clear(120.0);
            Rack r;
            r.setup(p, 21);
            r.generate(0, RowRole::Bass);
            r.generate(1, RowRole::Counter);
            if (chord) s.rack.push_back({ 16.0, 0, RackOp::Chord, -2 });   // VI, below the centre
            r.run(s, 48.0);
            s.sort();
            return s;
        };
        const Score plain = play(false), moved = play(true);
        int bassWrong = 0, counterWrong = 0, bassMoved = 0;
        for (size_t i = 0; i < plain.notes.size() && i < moved.notes.size(); ++i) {
            const NoteEvent &a = plain.notes[i], &b = moved.notes[i];
            if (a.part == Part::Row2) { counterWrong += a.pitch != b.pitch ? 1 : 0; continue; }
            const int d = b.pitch - a.pitch;
            if (a.beat < 16.0) bassWrong += d != 0 ? 1 : 0;
            else { bassWrong += (d != -3 && d != -4) || !inScale(b.pitch, 9, 0) ? 1 : 0; ++bassMoved; }
        }
        check(plain.notes.size() == moved.notes.size() && bassWrong == 0 && counterWrong == 0 && bassMoved > 20,
              "a chord moves the bass row down to VI in the scale and leaves the counter row",
              fmt("%d bass notes moved, %d wrong, %d counter notes changed", bassMoved, bassWrong, counterWrong));
    }

    // Pieces: the bass follows the chord, the transposer only moves by +7, +5, -3, the drone stays, the end is open.
    int badMoves = 0, droneOff = 0, shifted = 0, fifthEnds = 0, ends = 0;
    for (int seed = 1; seed <= 10; ++seed) {
        ParamStore p;
        p.parseText(seed % 2 ? "compose.style=Melodic" : "compose.style=Cosmic");
        const Score sc = composePiece(p, static_cast<uint64_t>(seed), 12.0);
        auto keyAt = [&](double beat) {
            int k = 0;
            for (const RackEvent& e : sc.rack) if (e.op == RackOp::Key && e.beat <= beat) k = e.value;
            return k;
        };
        for (const auto& r : sc.rootShifts) {
            const int m = r.second - keyAt(r.first);
            if (m != 0 && m != 7 && m != 5 && m != -3) ++badMoves;
        }
        for (const NoteEvent& n : sc.notes)
            if (n.part == Part::Drone && pitchClass(n.pitch - sc.keyRoot - keyAt(n.beat)) != 0) ++droneOff;
        for (const auto& e : sc.scaleShifts) shifted += e.second != sc.scaleShifts.front().second ? 1 : 0;
        // The last chord of the piece: the open fifth on the centre.
        double last = -1.0;
        for (const NoteEvent& n : sc.notes) if (n.part == Part::Strings || n.part == Part::TapeKeys) last = std::max(last, n.beat);
        if (last > 0.8 * sc.lengthBeats) {
            ++ends;
            bool open = true;
            for (const NoteEvent& n : sc.notes)
                if ((n.part == Part::Strings || n.part == Part::TapeKeys) && n.beat >= last - 0.1) {
                    const int pc = pitchClass(n.pitch - sc.keyRoot - keyAt(n.beat));
                    open = open && (pc == 0 || pc == 7);
                }
            fifthEnds += open ? 1 : 0;
        }
    }
    check(badMoves == 0, "the transposer moves only by a fifth up, a fourth up or a minor third down", fmt("%d other moves", badMoves));
    check(droneOff == 0, "the drone stays on the centre", fmt("%d drone notes elsewhere", droneOff));
    check(ends >= 3 && fifthEnds == ends, "the piece ends on the open fifth", fmt("%d of %d", fifthEnds, ends));
    check(shifted > 0, "a parallel change of mode at some peak", fmt("%d changes of mode in ten pieces", shifted));
}

/**
 * The sequencing after the style guide (Rack.h, 4.x): the figures, their rests and accents, the ratchets, the
 * probability gates, the doubled pulse, the thinning; in a piece, ratchets only on the plateau and at the peak.
 */
void testSequencing()
{
    section("sequencing after the style guide");
    // Figures: every archetype turns up; the first step sounds on the root or the fifth; rests and accents per eight.
    std::set<int> seen;
    int badFirst = 0, badRests = 0, badAccents = 0, rows = 0;
    for (int seed = 1; seed <= 60; ++seed) {
        ParamStore p;
        p.parseText(seed % 3 == 0 ? "compose.scale=Phrygian" : "compose.scale=Dorian");
        Rack r;
        r.setup(p, static_cast<uint64_t>(seed));
        r.generate(0, RowRole::Bass);
        r.generate(1, RowRole::Counter);
        r.generate(2, RowRole::Walk);
        for (int row = 0; row < 3; ++row) {
            const Step* st = r.steps(row);
            const Figure f = r.figure(row);
            seen.insert(static_cast<int>(f));
            ++rows;
            if (f != Figure::Canon && (!st[0].gate || (st[0].degree != 0 && st[0].degree != 4))) ++badFirst;
            for (int b = 0; b < 16; b += 8) {
                int rests = 0;
                for (int i = b; i < b + 8; ++i) rests += st[i].gate ? 0 : 1;
                if (f != Figure::Canon && (row == 0 ? rests > 2 : (rests < 1 || rests > 3))) ++badRests;
                if (!st[b].accent) ++badAccents;
            }
        }
    }
    check(seen.size() == static_cast<size_t>(Figure::Count), "every figure turns up", fmt("%zu of %d", seen.size(), static_cast<int>(Figure::Count)));
    check(badFirst == 0 && badRests == 0 && badAccents == 0, "root or fifth first, the rests and accents of each eight",
          fmt("%d first steps, %d rest counts, %d accents wrong of %d rows", badFirst, badRests, badAccents, rows));

    // A row played with rack events: ratchets, the doubled pulse, the thinning.
    auto play = [](const std::vector<RackEvent>& events, const char* extra) {
        ParamStore p;
        p.parseText(std::string("row1.active=1 row1.length=8 row1.division=1/16 row1.mutation=0 ") + extra);
        Score s;
        s.clear(120.0);
        Rack r;
        r.setup(p, 5);
        r.generate(0, RowRole::Counter);
        s.rack = events;
        r.run(s, 64.0);
        s.sort();
        return s;
    };
    auto notesIn = [](const Score& s, double from, double to) {
        int n = 0;
        for (const NoteEvent& e : s.notes) n += e.beat >= from && e.beat < to ? 1 : 0;
        return n;
    };
    const Score plain = play({}, "");
    const Score ratch = play({ { 32.0, 0, RackOp::Ratchet, 2 } }, "");
    double closest = 1.0;
    for (size_t i = 1; i < ratch.notes.size(); ++i) if (ratch.notes[i].beat >= 32.0) closest = std::min(closest, ratch.notes[i].beat - ratch.notes[i - 1].beat);
    check(notesIn(ratch, 0.0, 32.0) == notesIn(plain, 0.0, 32.0) && notesIn(ratch, 32.0, 64.0) > notesIn(plain, 32.0, 64.0) && closest < 0.2,
          "ratchets split steps into quick triggers from their event on",
          fmt("%d against %d notes after it, closest %.3f beats", notesIn(ratch, 32.0, 64.0), notesIn(plain, 32.0, 64.0), closest));
    const Score doubled = play({ { 32.0, 0, RackOp::Division, static_cast<int>(RowDivision::ThirtySecond) } }, "");
    check(notesIn(doubled, 0.0, 32.0) == notesIn(plain, 0.0, 32.0) && notesIn(doubled, 32.0, 64.0) > notesIn(plain, 32.0, 64.0) * 3 / 2,
          "a new division doubles the pulse from its event on", fmt("%d against %d notes", notesIn(doubled, 32.0, 64.0), notesIn(plain, 32.0, 64.0)));
    std::vector<RackEvent> thin;
    for (double b = 16.0; b < 48.0; b += 4.0) thin.push_back({ b, 0, RackOp::Thin, 1 });
    const Score thinned = play(thin, "");
    bool downbeats = true;
    for (const NoteEvent& e : plain.notes)
        if (std::fmod(e.beat, 2.0) == 0.0) {
            bool found = false;
            for (const NoteEvent& t : thinned.notes) found = found || t.beat == e.beat;
            downbeats = downbeats && found;
        }
    check(notesIn(thinned, 48.0, 64.0) < notesIn(plain, 48.0, 64.0) && downbeats, "the thinned row loses steps but never its first",
          fmt("%d against %d notes at the end", notesIn(thinned, 48.0, 64.0), notesIn(plain, 48.0, 64.0)));
    // Probability gates in Modern: a step that sounds on some rounds and not on others.
    {
        ParamStore p;
        p.parseText("compose.style=Modern row3.active=1 row3.length=16 row3.division=1/16 row3.mutation=0");
        int chancy = 0, varied = 0;
        for (int seed = 1; seed <= 20; ++seed) {
            Rack r;
            r.setup(p, static_cast<uint64_t>(seed));
            r.generate(2, RowRole::Counter);
            for (int i = 0; i < 16; ++i) chancy += r.steps(2)[i].chance < 1.0f && r.steps(2)[i].gate ? 1 : 0;
            Score s;
            s.clear(120.0);
            r.run(s, 64.0);
            s.sort();
            int count[16] = {};
            for (const NoteEvent& e : s.notes) if (e.part == Part::Row3) ++count[static_cast<int>(std::lround(e.beat * 4.0)) % 16];
            for (int i = 0; i < 16; ++i) varied += count[i] > 0 && count[i] < 16 ? 1 : 0;
        }
        check(chancy > 0 && varied > 0, "probability gates: steps that sound on some rounds only", fmt("%d chance steps, %d varied", chancy, varied));
    }

    // In a piece: ratchets only on the plateau (the lead's section) and at the peak.
    int outside = 0, ratchets = 0;
    for (int seed = 1; seed <= 6; ++seed) {
        ParamStore p;
        p.parseText("compose.style=Melodic");
        const Score sc = composePiece(p, static_cast<uint64_t>(seed), 12.0);
        std::vector<std::pair<double, double>> allowed;
        for (size_t m = 0; m < sc.markers.size(); ++m) {
            const std::string& t = sc.markers[m].text;
            if (t.rfind("Lead", 0) == 0 || t.rfind("Hoehepunkt", 0) == 0)
                allowed.push_back({ sc.markers[m].beat, m + 1 < sc.markers.size() ? sc.markers[m + 1].beat : sc.lengthBeats });
        }
        double last[kRows];
        for (double& l : last) l = -10.0;
        for (const NoteEvent& n : sc.notes) {
            const int r = static_cast<int>(n.part) - static_cast<int>(Part::Row1);
            if (r < 0 || r >= kRows) continue;
            if (n.beat - last[r] < 0.2 && n.beat - last[r] > 1e-6) {
                ++ratchets;
                bool in = false;
                for (const auto& a : allowed) in = in || (n.beat >= a.first && n.beat < a.second + 0.5);
                outside += in ? 0 : 1;
            }
            last[r] = n.beat;
        }
    }
    check(ratchets > 0 && outside == 0, "ratchets only on the plateau and at the peak", fmt("%d ratchet triggers, %d elsewhere", ratchets, outside));
}

/**
 * Form, space and lead after the style guide (3.8, 5.3, 7.3): phrases of four to eight bars with rests of two to
 * four, ending on the fifth or the minor third, a little behind the beat; the drums between 45 and 90 % of the
 * piece; the bass dry; the hall shorter at the peak than in the spaces.
 */
void testFormAndSpace()
{
    section("form, space and lead after the style guide");
    LeadPlan lp;
    lp.keyRoot = 9;
    lp.scale = 0;
    Score ls;
    ls.clear(120.0);
    Rng rng;
    rng.seed(31);
    writeLead(ls, lp, 0.0, 4.0 * 128, rng);
    ls.sort();
    int phrases = 0, longPhrases = 0, shortRests = 0, endings = 0, goodEndings = 0, early = 0;
    double phraseStart = -1.0, lastEnd = -100.0;
    int lastPitch = -1;
    auto closePhrase = [&]() {
        if (phraseStart < 0.0) return;
        ++phrases;
        if (lastEnd - phraseStart > 8 * 4.0 + 0.5) ++longPhrases;
        ++endings;
        const int pc = pitchClass(lastPitch - 9);
        goodEndings += pc == 7 || pc == 3 ? 1 : 0;
    };
    for (const NoteEvent& n : ls.notes) {
        if (n.beat - lastEnd > 4.0) {
            closePhrase();
            if (phraseStart >= 0.0 && n.beat - lastEnd < 2 * 4.0 - 0.5) ++shortRests;
            phraseStart = n.beat;
        }
        const double off = std::fmod(n.beat, 0.25);
        if (off < 0.014 || off > 0.05) ++early;
        lastEnd = std::max(lastEnd, n.beat + n.length);
        lastPitch = n.pitch;
    }
    closePhrase();
    check(phrases >= 6 && longPhrases == 0 && shortRests == 0, "lead phrases of four to eight bars, rests of two or more",
          fmt("%d phrases, %d too long, %d rests too short", phrases, longPhrases, shortRests));
    check(goodEndings * 10 >= endings * 9, "the phrases end on the fifth or the minor third", fmt("%d of %d", goodEndings, endings));
    check(early == 0, "the lead a little behind the beat", fmt("%d notes on or before the grid", early));

    int drumsOutside = 0, drumPieces = 0, dryBass = 0, pieces = 0, hallShrinks = 0;
    for (int seed = 1; seed <= 8; ++seed) {
        ParamStore p;
        p.parseText("compose.style=Melodic");
        const Score sc = composePiece(p, static_cast<uint64_t>(seed), 14.0);
        ++pieces;
        bool drums = false;
        for (const NoteEvent& n : sc.notes)
            if (n.part == Part::Drums) {
                drums = true;
                drumsOutside += n.beat < 0.2 * sc.lengthBeats - 1e-6 || n.beat > 0.9 * sc.lengthBeats ? 1 : 0;
            }
        drumPieces += drums ? 1 : 0;
        const int echo = p.id(Module::Row, 0, row::EchoSend);
        dryBass += p.fromNormalised(echo, p.toNormalised(echo, knobAt(p, sc, echo)) + sc.gestureOffset(echo, 1.0)) < 0.01f ? 1 : 0;
        const int hall = p.id(Module::Reverb, 0, reverb::Decay);
        double peakEnd = -1.0, atmoMid = -1.0;
        for (size_t m = 0; m < sc.markers.size(); ++m) {
            const double next = m + 1 < sc.markers.size() ? sc.markers[m + 1].beat : sc.lengthBeats;
            if (sc.markers[m].text.rfind("Hoehepunkt", 0) == 0 && peakEnd < 0.0) peakEnd = next - 1.0;
            if (sc.markers[m].text.rfind("Atmo", 0) == 0) atmoMid = next - 1.0;
        }
        if (peakEnd > 0.0 && atmoMid > 0.0 && sc.gestureOffset(hall, peakEnd) < sc.gestureOffset(hall, atmoMid)) ++hallShrinks;
    }
    check(drumPieces > 0 && drumsOutside == 0, "the pulse and the drums only between 20 and 90 % of the piece", fmt("%d pieces with drums, %d hits outside", drumPieces, drumsOutside));
    check(dryBass == pieces, "the bass without echo", fmt("%d of %d", dryBass, pieces));
    check(hallShrinks == pieces, "the hall shorter at the peak than in the atmosphere", fmt("%d of %d", hallShrinks, pieces));
}

/**
 * The style guide's further rules: shifts by a third between the phases, a mode of its own for a later phase
 * (the suite's parts), quantised random steps.
 */
void testGuideExtras()
{
    section("the style guide's further rules");
    std::set<int> moves;
    for (int seed = 1; seed <= 200; ++seed) {
        Rng rng;
        rng.seed(static_cast<uint64_t>(seed));
        const PieceForm f = drawForm(styleProfile(Style::Drift), 20.0, 100.0, rng);
        for (int k : f.phaseKey) moves.insert(k);
    }
    check(moves.count(-3) == 1 && moves.count(4) == 1, "later phases shift by a third as well (a minor third down, a major third up)",
          fmt("%zu different keys", moves.size()));
    int ownMode = 0, multi = 0;
    for (int seed = 1; seed <= 40; ++seed) {
        ParamStore p;
        p.parseText("compose.style=Doom");
        const Score sc = composePiece(p, static_cast<uint64_t>(seed), 20.0);
        for (const Marker& m : sc.markers)
            if (m.text == "Einsatz 2") {
                ++multi;
                ownMode += sc.scaleAt(m.beat + 0.01, 0) != sc.scaleAt(0.0, 0) ? 1 : 0;
            }
    }
    check(ownMode > 0, "a later phase in a mode of its own", fmt("%d of %d second phases", ownMode, multi));
    // Random steps: a new note through the quantiser each time round, always in the mode.
    {
        ParamStore p;
        p.parseText("compose.style=Drift row3.active=1 row3.length=16 row3.division=1/16 row3.mutation=0");
        int randomSteps = 0, varied = 0, outside = 0;
        for (int seed = 1; seed <= 20; ++seed) {
            Rack r;
            r.setup(p, static_cast<uint64_t>(seed));
            r.generate(2, RowRole::Counter);
            std::set<int> pitches[16];
            Score sc;
            sc.clear(120.0);
            r.run(sc, 64.0);
            for (const NoteEvent& e : sc.notes) {
                pitches[static_cast<int>(std::lround(e.beat * 4.0)) % 16].insert(e.pitch);
                outside += inScale(e.pitch, 9, 0) ? 0 : 1;
            }
            for (int i = 0; i < 16; ++i) {
                if (!r.steps(2)[i].random) continue;
                ++randomSteps;
                varied += pitches[i].size() > 1 ? 1 : 0;
            }
        }
        check(randomSteps > 0 && varied * 2 > randomSteps && outside == 0, "random steps play new notes of the mode each time round",
              fmt("%d random steps, %d varied, %d notes outside", randomSteps, varied, outside));
    }
    // The stereo field: the composer's pans, and the drone's slow wander in the engine.
    {
        ParamStore p;
        p.parseText("compose.style=Melodic");
        const Score sc = composePiece(p, 3, 10.0);
        auto panAt = [&](Module m, int inst, int index) {
            const int id = p.id(m, inst, index);
            return p.fromNormalised(id, p.toNormalised(id, knobAt(p, sc, id)) + sc.gestureOffset(id, 1.0));
        };
        const float bass = panAt(Module::Row, 0, row::Pan), third = panAt(Module::Row, 2, row::Pan);
        const float depth = panAt(Module::Drone, 0, lead::AutoPan);
        check(std::fabs(bass) < 0.01f && std::fabs(third) >= 0.29f && depth > 0.3f, "the bass in the middle, a counter row out to the side, the drone wandering",
              fmt("bass %.2f, row 3 %.2f, drone auto pan %.2f", bass, third, depth));
        Score one;
        one.clear(120.0);
        one.notes.push_back({ 0.0, 44.0, Part::Drone, 45, 0.8f, false, false });
        one.lengthBeats = 44.0;
        Engine e;
        e.params().parseText("drone.echo=0 drone.reverb=0 drone.pan=0 drone.auto_pan=1");
        e.prepare(48000.0, 256);
        e.load(one);
        std::vector<float> L(256), R(256);
        double lo = 1.0, hi = 0.0, l = 0.0, r = 0.0;
        // Above 300 Hz, where the overtones wander: under 100 Hz the mix bus keeps everything mono.
        Svf hpl, hpr;
        hpl.setQ(300.0f, 0.7071f, 48000.0f);
        hpr.copyCoefficients(hpl);
        for (int done = 0, w = 0; done < 48000 * 21; done += 256) {
            e.process(L.data(), R.data(), 256);
            for (int i = 0; i < 256; ++i) {
                float a, b, xl, xr;
                hpl.tick(L[i], a, b, xl);
                hpr.tick(R[i], a, b, xr);
                l += double(xl) * xl;
                r += double(xr) * xr;
            }
            if (++w == 187) {   // about a second
                if (done > 48000) { lo = std::min(lo, l / (l + r + 1e-30)); hi = std::max(hi, l / (l + r + 1e-30)); }
                l = r = 0.0;
                w = 0;
            }
        }
        check(hi - lo > 0.4, "the drone's auto pan moves it across the field", fmt("left share %.2f .. %.2f", lo, hi));
    }
    // The album: interludes without rows between the long pieces, the middle piece Phrygian, the last one Dorian.
    {
        ParamStore p;
        p.parseText("compose.style=Melodic compose.scale=Aeolian compose.album=1");
        const Score sc = composeConcert(p, 9, 70.0);
        std::vector<std::pair<double, double>> inter;
        std::vector<double> pieceStarts;
        for (size_t m = 0; m < sc.markers.size(); ++m) {
            const std::string& t = sc.markers[m].text;
            if (t.rfind("Zwischenspiel", 0) == 0 && t.find(':') == std::string::npos) {
                double end = sc.lengthBeats;
                for (size_t k = m + 1; k < sc.markers.size(); ++k)
                    if (sc.markers[k].text.rfind("Stueck", 0) == 0) { end = sc.markers[k].beat; break; }
                inter.push_back({ sc.markers[m].beat, end });
            }
            if (t.rfind("Stueck", 0) == 0 && t.find(": Atmo") != std::string::npos) pieceStarts.push_back(sc.markers[m].beat);
        }
        int rowNotes = 0;
        for (const NoteEvent& n : sc.notes) {
            const int r = static_cast<int>(n.part) - static_cast<int>(Part::Row1);
            if (r < 0 || r >= kRows) continue;
            for (const auto& iv : inter) rowNotes += n.beat >= iv.first && n.beat < iv.second ? 1 : 0;
        }
        bool phrygian = false;
        for (double b : pieceStarts) phrygian = phrygian || sc.scaleAt(b + 1.0, 0) == 2;
        const bool dorianEnd = !pieceStarts.empty() && sc.scaleAt(pieceStarts.back() + 1.0, 0) == 1;
        check(!inter.empty() && rowNotes == 0 && phrygian && dorianEnd,
              "an album: interludes without rows, the darkest piece in the middle, an ethereal end",
              fmt("%zu interludes, %d row notes in them, %zu pieces, Phrygian %d, Dorian at the end %d",
                  inter.size(), rowNotes, pieceStarts.size(), phrygian ? 1 : 0, dorianEnd ? 1 : 0));
    }
    // The second echo: a row sent only into it comes back after its own time (an eighth: 0.5 beats), not before.
    {
        Score one;
        one.clear(120.0);
        one.notes.push_back({ 0.0, 0.1, Part::Row2, 57, 0.9f, false, false });
        one.lengthBeats = 8.0;
        auto render = [&](const char* setting) {
            Engine e;
            e.params().parseText("row2.echo=0 row2.reverb=0 delay.time=1/8 delay.feedback=0.3 delay.return=0 master.level=0");
            e.params().parseText(setting);
            e.prepare(48000.0, 256);
            e.load(one);
            std::vector<float> out, L(256), R(256);
            for (int done = 0; done < 48000 * 2; done += 256) {
                e.process(L.data(), R.data(), 256);
                for (int i = 0; i < 256; ++i) out.push_back(L[i] + R[i]);
            }
            return out;
        };
        const std::vector<float> dry = render("row2.echo2=0"), wet = render("row2.echo2=1");
        auto energy = [&](double b0, double b1) {   // of the difference, between two beats at 120 BPM
            double e = 0.0;
            for (size_t i = static_cast<size_t>(b0 * 24000.0); i < static_cast<size_t>(b1 * 24000.0) && i < wet.size(); ++i)
                e += double(wet[i] - dry[i]) * (wet[i] - dry[i]);
            return e;
        };
        const double before = energy(0.1, 0.45), repeat = energy(0.5, 0.8);
        check(repeat > 1e-3 && repeat > 100.0 * before, "the second echo returns a row after its own time",
              fmt("difference %.2g before the eighth, %.2g after it", before, repeat));
    }
}

/**
 * The loudness meter (Loudness.h) against the standards' own test signals: a 997 Hz sine at full scale in one
 * channel reads -3.01 LUFS (BS.1770-4); a sine at a quarter of the rate, sampled 45 degrees off its crests,
 * has samples at -3 dBFS and a true peak at 0 dBTP; identical channels correlate at +1, inverted ones at -1.
 */
void testLoudness()
{
    section("loudness meter");
    const int sr = 48000;
    auto measure = [&](auto gen) {
        LoudnessMeter m;
        m.prepare(sr);
        std::vector<float> L(sr), R(sr);
        for (int s = 0; s < 10; ++s) {
            for (int i = 0; i < sr; ++i) gen(s * sr + i, L[static_cast<size_t>(i)], R[static_cast<size_t>(i)]);
            m.process(L.data(), R.data(), sr);
        }
        return m.report();
    };
    const LoudnessReport one = measure([](int i, float& l, float& r) { l = static_cast<float>(std::sin(2.0 * 3.14159265358979 * 997.0 * i / 48000.0)); r = 0.0f; });
    check(std::fabs(one.integrated + 3.01) < 0.1 && std::fabs(one.shortTermMax + 3.01) < 0.1 && one.range < 0.1,
          "a full-scale 997 Hz sine in one channel reads -3.01 LUFS", fmt("%.2f LUFS integrated, %.2f short-term, range %.2f LU", one.integrated, one.shortTermMax, one.range));
    const LoudnessReport quarter = measure([](int i, float& l, float& r) { l = r = static_cast<float>(std::sin(3.14159265358979 * 0.5 * i + 3.14159265358979 * 0.25)); });
    check(std::fabs(quarter.truePeak) < 0.3, "the true peak between the samples: 0 dBTP from samples at -3 dBFS", fmt("%.2f dBTP", quarter.truePeak));
    const LoudnessReport same = measure([](int i, float& l, float& r) { l = r = 0.3f * static_cast<float>(std::sin(0.05 * i)); });
    const LoudnessReport inverted = measure([](int i, float& l, float& r) { l = 0.3f * static_cast<float>(std::sin(0.05 * i)); r = -l; });
    check(same.correlation > 0.999 && inverted.correlation < -0.999 && same.sideUnderMid > 100.0 && inverted.sideUnderMid < -100.0,
          "the correlation and the side against the mid", fmt("%.3f and %.3f", same.correlation, inverted.correlation));
}

/**
 * The mix after the production guide (Engine.h): a strip's low cut, the bass mono under 100 Hz on the mix bus,
 * the mono switch; and in a piece the composer's frequency plan and its automation along the form.
 */
void testMixBus()
{
    section("mix bus after the production guide");
    // One low note on the bass row, hard left: A1 (55 Hz), dry.
    Score one;
    one.clear(120.0);
    one.notes.push_back({ 0.0, 6.0, Part::Row1, 33, 0.9f, false, false });
    one.lengthBeats = 8.0;
    auto render = [&](const char* setting, std::vector<float>& L, std::vector<float>& R) {
        Engine e;
        e.params().parseText("row1.echo=0 row1.reverb=0 row1.pan=-1 master.level=0");
        e.params().parseText(setting);
        e.prepare(48000.0, 256);
        e.load(one);
        L.clear(); R.clear();
        std::vector<float> l(256), r(256);
        for (int done = 0; done < 48000 * 3; done += 256) {
            e.process(l.data(), r.data(), 256);
            if (done >= 48000) { L.insert(L.end(), l.begin(), l.end()); R.insert(R.end(), r.begin(), r.end()); }
        }
    };
    auto energy = [](const std::vector<float>& v) { double e = 0.0; for (float x : v) e += double(x) * x; return e; };
    std::vector<float> L, R, L2, R2;
    render("row1.low_cut=30", L, R);
    render("row1.low_cut=300", L2, R2);
    check(energy(L2) + energy(R2) < 0.3 * (energy(L) + energy(R)), "a strip's low cut takes the bass away",
          fmt("%.1f dB less", 10.0 * std::log10((energy(L) + energy(R)) / std::max(1e-30, energy(L2) + energy(R2)))));
    // Hard left, yet under 100 Hz it comes out of both sides: the side is mono down there (measured under 70 Hz).
    double lr = 0.0, ll = 0.0, rr = 0.0;
    Svf la, lb, ra, rb;
    la.setQ(70.0f, 0.7071f, 48000.0f); lb.copyCoefficients(la); ra.copyCoefficients(la); rb.copyCoefficients(la);
    for (size_t i = 0; i < L.size(); ++i) {
        const double l = lb.lp(la.lp(L[i])), r = rb.lp(ra.lp(R[i]));
        if (i < 4800) continue;   // the filters settle
        lr += l * r; ll += l * l; rr += r * r;
    }
    check(lr / std::sqrt(ll * rr) > 0.8 && rr > 0.5 * ll, "the bass is mono under 100 Hz on the mix bus, even panned hard left",
          fmt("correlation %.2f, right %.1f dB under left", lr / std::sqrt(ll * rr), 10.0 * std::log10(ll / std::max(1e-30, rr))));
    render("row1.low_cut=30 master.mono=1", L2, R2);
    bool same = true;
    for (size_t i = 0; i < L2.size(); ++i) same = same && L2[i] == R2[i];
    check(same, "the mono switch sums both sides", "");

    // In a piece: the frequency plan and the automation along the form.
    ParamStore p;
    p.parseText("compose.style=Melodic");
    const Score sc = composePiece(p, 12, 12.0);
    auto valueAt = [&](Module m, int inst, int index, double beat) {
        const int id = p.id(m, inst, index);
        return p.fromNormalised(id, p.toNormalised(id, knobAt(p, sc, id)) + sc.gestureOffset(id, beat));
    };
    const float bassCut = valueAt(Module::Row, 0, row::LowCut, 1.0), mainCut = valueAt(Module::Row, 1, row::LowCut, 1.0);
    const float counterCut = valueAt(Module::Row, 2, row::LowCut, 1.0);
    check(std::fabs(bassCut - 30.0f) < 1.0f && std::fabs(mainCut - 90.0f) < 2.0f && std::fabs(counterCut - 200.0f) < 4.0f,
          "the frequency plan: the bass from 30 Hz, the main sequence from 90, the counter rows from 200",
          fmt("%.0f, %.0f, %.0f Hz", bassCut, mainCut, counterCut));
    double atmoEnd = -1.0, peakEnd = -1.0, leadMid = -1.0;
    for (size_t m = 0; m + 1 < sc.markers.size(); ++m) {
        const std::string& t = sc.markers[m].text;
        if (t.rfind("Atmo", 0) == 0) atmoEnd = sc.markers[m + 1].beat - 1.0;
        if (t.rfind("Hoehepunkt", 0) == 0 && peakEnd < 0.0) peakEnd = sc.markers[m + 1].beat - 1.0;
        if (t.rfind("Lead", 0) == 0 && leadMid < 0.0) leadMid = 0.5 * (sc.markers[m].beat + sc.markers[m + 1].beat);
    }
    const float atmoLevel = valueAt(Module::Master, 0, master::Level, atmoEnd), peakLevel = valueAt(Module::Master, 0, master::Level, peakEnd);
    const float atmoWidth = valueAt(Module::Master, 0, master::Width, atmoEnd), peakWidth = valueAt(Module::Master, 0, master::Width, peakEnd);
    check(atmoLevel < peakLevel - 2.0f && atmoWidth > peakWidth, "along the form: the spaces quieter and wider than the peak",
          fmt("level %.1f against %.1f dB, width %.1f against %.1f dB", atmoLevel, peakLevel, atmoWidth, peakWidth));
    if (leadMid > 0.0) {
        const float tape = valueAt(Module::Tape, 0, tape::Level, leadMid) - p.get(p.id(Module::Tape, 0, tape::Level));
        check(tape < -2.5f, "the pads step back while the lead plays", fmt("tape keys %.1f dB", tape));
    }
}

/**
 * The addon from the dark-ambient practice (Engine.h, Composer.cpp): the distance macro, the cascaded duck, the tape
 * keys' allpass spread, the sub solo, the export fades; in a piece the approach and the events.
 */
void testAddon()
{
    section("the addon: distance, cascade, spread, events");
    // Renders a score with the given settings; returns the mix and, per source, the stems' sums of squares.
    struct Out { std::vector<float> L, R; double stem[Engine::kChannels + 1] = {}; double stemLR[Engine::kChannels + 1] = {}; double stemLL[Engine::kChannels + 1] = {}, stemRR[Engine::kChannels + 1] = {}; };
    auto render = [](const Score& sc, const char* setting, double seconds) {
        Out o;
        Engine e;
        e.params().parseText("master.level=0 master.motion=0");
        e.params().parseText(setting);
        e.prepare(48000.0, 256);
        e.load(sc);
        constexpr int kStems = Engine::kChannels + 1;
        std::vector<std::vector<float>> buf(2 * kStems, std::vector<float>(256));
        std::vector<float*> sl(kStems), sr(kStems);
        for (int c = 0; c < kStems; ++c) { sl[static_cast<size_t>(c)] = buf[static_cast<size_t>(2 * c)].data(); sr[static_cast<size_t>(c)] = buf[static_cast<size_t>(2 * c + 1)].data(); }
        e.setStems(sl.data(), sr.data());
        std::vector<float> l(256), r(256);
        for (int done = 0; done < static_cast<int>(48000 * seconds); done += 256) {
            e.process(l.data(), r.data(), 256);
            if (done < 48000) continue;
            o.L.insert(o.L.end(), l.begin(), l.end());
            o.R.insert(o.R.end(), r.begin(), r.end());
            for (int c = 0; c < kStems; ++c)
                for (int i = 0; i < 256; ++i) {
                    const double a = sl[static_cast<size_t>(c)][i], b = sr[static_cast<size_t>(c)][i];
                    o.stem[c] += a * a + b * b; o.stemLR[c] += a * b; o.stemLL[c] += a * a; o.stemRR[c] += b * b;
                }
        }
        return o;
    };
    auto energy = [](const Out& o) { double e = 0.0; for (size_t i = 0; i < o.L.size(); ++i) e += double(o.L[i]) * o.L[i] + double(o.R[i]) * o.R[i]; return e; };
    // The distance macro: a row at 0.6 is about 10 dB quieter and darker than at 0.
    Score one;
    one.clear(120.0);
    for (double b = 0.0; b < 8.0; b += 0.5) one.notes.push_back({ b, 0.4, Part::Row2, 57, 0.9f, false, false });
    one.lengthBeats = 8.0;
    const Out nearRow = render(one, "row2.reverb=0 row2.echo=0 row2.distance=0", 3.0), farRow = render(one, "row2.reverb=0 row2.echo=0 row2.distance=0.6", 3.0);
    const double nearStem = nearRow.stem[1], farStem = farRow.stem[1];
    check(10.0 * std::log10(nearStem / std::max(1e-30, farStem)) > 8.0, "the distance macro: a row at 0.6 lies about 10 dB further back",
          fmt("%.1f dB quieter in its strip", 10.0 * std::log10(nearStem / std::max(1e-30, farStem))));
    // The cascaded duck: the strings step back a little in their middle band while a row plays loud.
    Score pads = one;
    pads.notes.clear();
    for (int k : { 57, 60, 64 }) pads.notes.push_back({ 0.0, 8.0, Part::Strings, k, 0.8f, false, false });
    Score both = pads;
    for (double b = 0.0; b < 8.0; b += 0.25) both.notes.push_back({ b, 0.2, Part::Row2, 45, 1.0f, false, false });
    const Out alone = render(pads, "row2.level=6", 3.0), under = render(both, "row2.level=6", 3.0);
    const double drop = 10.0 * std::log10(alone.stem[kRows + 3] / std::max(1e-30, under.stem[kRows + 3]));
    check(drop > 0.5 && drop < 3.0, "the rows duck the pads' middle band a little (the cascade)", fmt("strings %.2f dB lower", drop));
    // The tape keys' spread: correlation between 0.2 and 0.5, their mono sum unchanged in level.
    Score keys = pads;
    for (NoteEvent& nte : keys.notes) nte.part = Part::TapeKeys;
    const Out spread = render(keys, "tape.spread=0.7", 3.0), flat = render(keys, "tape.spread=0", 3.0);
    const int t = kRows + 2;   // the tape keys' channel (Engine::channelName)
    const double corr = spread.stemLR[t] / std::sqrt(spread.stemLL[t] * spread.stemRR[t]);
    double sumSpread = 0.0, sumFlat = 0.0;
    for (size_t i = 0; i < spread.L.size(); ++i) {
        sumSpread += (double(spread.L[i]) + spread.R[i]) * (double(spread.L[i]) + spread.R[i]);
        sumFlat += (double(flat.L[i]) + flat.R[i]) * (double(flat.L[i]) + flat.R[i]);
    }
    check(corr > 0.2 && corr < 0.5, "the tape keys' allpass spread: wide, but not apart", fmt("correlation %.2f; mono sum %.1f dB against the unspread",
          corr, 10.0 * std::log10(sumSpread / std::max(1e-30, sumFlat))));
    // The sub solo: little left above 200 Hz.
    const Out sub = render(one, "row2.reverb=0 row2.echo=0 master.sub_solo=1", 3.0);
    check(energy(sub) < 0.1 * energy(nearRow), "the sub solo leaves only the lows", fmt("%.1f dB under the full mix", 10.0 * std::log10(energy(nearRow) / std::max(1e-30, energy(sub)))));
    // The export fades: silent at both ends, whole in the middle.
    check(exportFade(0, 2880000, 48000.0) == 0.0f && exportFade(2880000, 2880000, 48000.0) == 0.0f && exportFade(1440000, 2880000, 48000.0) == 1.0f
          && exportFade(48000, 2880000, 48000.0) > 0.4f && exportFade(48000, 2880000, 48000.0) < 0.6f, "the export fades in over 2 s and out over 10 s", "");

    // In a piece: the bass comes in from afar, and events sound in the stages without a sequence.
    ParamStore p;
    p.parseText("compose.style=Cosmic");
    const Score sc = composePiece(p, 21, 14.0);
    double entry = -1.0, atmoEnd = -1.0;
    for (size_t m = 0; m + 1 < sc.markers.size(); ++m) {
        if (sc.markers[m].text.rfind("Einsatz", 0) == 0 && entry < 0.0) entry = sc.markers[m].beat;
        if (sc.markers[m].text.rfind("Atmo", 0) == 0) atmoEnd = sc.markers[m + 1].beat;
    }
    const int dist = p.id(Module::Row, 0, row::Distance);
    auto distAt = [&](double b) { return p.fromNormalised(dist, p.toNormalised(dist, knobAt(p, sc, dist)) + sc.gestureOffset(dist, b)); };
    const double later = entry + 45.0 * sc.tempo.bpmAt(entry) / 60.0 + 1.0;
    check(distAt(entry + 0.01) > 0.6f && distAt(later) < 0.05f, "the bass comes in from afar (the approach)",
          fmt("distance %.2f at the entry, %.2f 45 s later", distAt(entry + 0.01), distAt(later)));
    int events = 0;
    for (const NoteEvent& n : sc.notes) events += n.part == Part::Lead && n.beat < atmoEnd ? 1 : 0;
    check(events > 0, "near events in the atmosphere before the sequence", fmt("%d lead notes", events));
}

/**
 * The factory presets (Presets.h): 1024 per synth, every name unique within its synth, every value inside its knob's
 * range, the mix and the composer's amounts left alone, and the tuning kept: the foundation's detune under 3 cents,
 * elsewhere at most 12, the drift at most 7, the vibrato at most 40 cents, the tapes' wobble inside its defaults'.
 */
void testPresets()
{
    section("factory presets");
    ParamStore store;
    int total = 0, badCount = 0, dupes = 0, outside = 0, leaves = 0, detuned = 0;
    for (Module m : { Module::Voice, Module::Lead, Module::Drone, Module::Tape, Module::Strings, Module::Drums, Module::Atmos }) {
        const std::vector<SoundPreset>& list = factoryPresets(m);
        total += static_cast<int>(list.size());
        badCount += list.size() == 1024 ? 0 : 1;
        std::set<std::string> names;
        for (const SoundPreset& pr : list) {
            dupes += names.insert(pr.name).second ? 0 : 1;
            for (const auto& e : pr.values) {
                const ParamDesc& d = store.desc(store.id(m, 0, e.first));
                outside += e.second < d.minValue || e.second > d.maxValue ? 1 : 0;
                leaves += presetLeaves(m, e.first) ? 1 : 0;
                const bool voiceLike = m == Module::Voice || m == Module::Lead || m == Module::Drone;
                if (voiceLike && e.first == voice::Detune && e.second > (m == Module::Drone ? 3.0f : 12.0f)) ++detuned;
                if (voiceLike && e.first == voice::Drift && e.second > 7.0f) ++detuned;
                if ((m == Module::Lead || m == Module::Drone) && e.first == lead::Vibrato && e.second > 40.0f) ++detuned;
                if (m == Module::Tape && ((e.first == tape::Wow && e.second > 12.0f) || (e.first == tape::Flutter && e.second > 4.0f)
                                          || (e.first == tape::Sag && e.second > 2.5f))) ++detuned;
            }
        }
    }
    check(total == 7 * 1024 && badCount == 0 && dupes == 0, "1024 presets for each of the seven synths, every name its own",
          fmt("%d presets, %d duplicate names", total, dupes));
    check(outside == 0 && leaves == 0, "every value inside its knob's range; the mix and the composer's amounts untouched",
          fmt("%d outside, %d on a knob a preset leaves", outside, leaves));
    check(detuned == 0, "the tuning kept: detune, drift, vibrato and the tapes' wobble in their bounds", fmt("%d too far", detuned));
    // A preset sets the whole sound: applied twice from different starting points it gives the same knobs.
    ParamStore a, b;
    b.parseText("voice3.cutoff=9000 voice3.resonance=0.9 voice3.detune=25 row3.level=-20");
    const SoundPreset& pr = factoryPresets(Module::Voice)[300];
    applyPreset(a, Module::Voice, 2, pr);
    applyPreset(b, Module::Voice, 2, pr);
    bool same = true;
    for (int k = 0; k < ParamStore::moduleCount(Module::Voice); ++k) same = same && a.get(a.id(Module::Voice, 2, k)) == b.get(b.id(Module::Voice, 2, k));
    check(same && b.get(b.id(Module::Row, 2, row::Level)) == -20.0f, "a preset sets the whole sound and leaves the mix",
          fmt("%s: %s", pr.group.c_str(), pr.name.c_str()));
}

/**
 * The blend room and the rest of the mix bus (Engine.h): the serial feed of the blend room into the hall, the soft
 * clipper (which leaves what lies under the ceiling alone), the limiter under 80 Hz, the punch; the user presets'
 * text.
 */
void testBlendAndBus()
{
    section("blend room, clipper, sub limiter, punch, user presets");
    auto render = [](const Score& sc, const char* setting, double seconds) {
        Engine e;
        e.params().parseText("master.level=0 master.motion=0");
        e.params().parseText(setting);
        e.prepare(48000.0, 256);
        e.load(sc);
        std::vector<float> out, l(256), r(256);
        for (int done = 0; done < static_cast<int>(48000 * seconds); done += 256) {
            e.process(l.data(), r.data(), 256);
            for (int i = 0; i < 256; ++i) out.push_back(0.5f * (l[i] + r[i]));
        }
        return out;
    };
    auto energy = [](const std::vector<float>& v, double s0, double s1) {
        double e = 0.0;
        for (size_t i = static_cast<size_t>(s0 * 48000.0); i < static_cast<size_t>(s1 * 48000.0) && i < v.size(); ++i) e += double(v[i]) * v[i];
        return e;
    };
    Score one;
    one.clear(120.0);
    one.notes.push_back({ 0.0, 0.25, Part::Row2, 57, 0.9f, false, false });
    one.lengthBeats = 8.0;
    // Into the blend room only: its short tail; with the serial feed the hall's long one follows.
    const auto dry = render(one, "row2.echo=0 row2.reverb=0 row2.blend=1 blend.into_hall=0 reverb.decay=8", 5.0);
    const auto serial = render(one, "row2.echo=0 row2.reverb=0 row2.blend=1 blend.into_hall=0.5 reverb.decay=8", 5.0);
    check(energy(dry, 0.3, 1.0) > 1e-4 && energy(serial, 3.0, 4.5) > 4.0 * energy(dry, 3.0, 4.5), "the blend room, and its return going on into the hall",
          fmt("tail after 3 s: %.2g with the feed, %.2g without", energy(serial, 3.0, 4.5), energy(dry, 3.0, 4.5)));
    // The soft clipper: quiet material passes bit for bit; loud peaks come out lower than without it.
    const auto quiet0 = render(one, "row2.level=-24 master.clip=0", 1.0), quiet1 = render(one, "row2.level=-24 master.clip=1", 1.0);
    const auto loud0 = render(one, "row2.level=6 row2.echo=0 row2.reverb=0 master.clip=0 master.ceiling=-6", 1.0);
    const auto loud1 = render(one, "row2.level=6 row2.echo=0 row2.reverb=0 master.clip=1 master.ceiling=-6", 1.0);
    bool same = quiet0.size() == quiet1.size();
    for (size_t i = 0; same && i < quiet0.size(); ++i) same = quiet0[i] == quiet1[i];
    check(same && loud0 != loud1, "the soft clipper leaves the quiet alone and rounds the peaks", "");
    // The limiter under 80 Hz: a loud low note is held under its ceiling in the low band.
    Score low = one;
    low.notes[0] = { 0.0, 4.0, Part::Row1, 28, 1.0f, false, false };
    const auto free = render(low, "row1.level=6 row1.echo=0 row1.reverb=0 row1.low_cut=10 master.sub_ceiling=0", 2.0);
    const auto held = render(low, "row1.level=6 row1.echo=0 row1.reverb=0 row1.low_cut=10 master.sub_ceiling=-18", 2.0);
    auto lowBand = [](std::vector<float> v) {   // under 60 Hz (fourth order)
        Svf a1, a2;
        a1.setQ(60.0f, 0.7071f, 48000.0f);
        a2.copyCoefficients(a1);
        for (float& x : v) x = a2.lp(a1.lp(x));
        return v;
    };
    const auto freeLow = lowBand(free), heldLow = lowBand(held);
    check(energy(heldLow, 0.5, 2.0) < 0.5 * energy(freeLow, 0.5, 2.0), "the band under 80 Hz limited on its own",
          fmt("%.1f dB less under 60 Hz", 10.0 * std::log10(energy(freeLow, 0.5, 2.0) / std::max(1e-30, energy(heldLow, 0.5, 2.0)))));
    // The punch lifts the attack more than the rest.
    const auto flat = render(one, "row2.echo=0 row2.reverb=0 row2.punch=0", 1.0), punched = render(one, "row2.echo=0 row2.reverb=0 row2.punch=1", 1.0);
    const double attack = energy(punched, 0.0, 0.02) / std::max(1e-30, energy(flat, 0.0, 0.02));
    const double body = energy(punched, 0.08, 0.12) / std::max(1e-30, energy(flat, 0.08, 0.12));
    check(attack > 1.2 * body, "the punch lifts the attack", fmt("attack x%.2f, body x%.2f", attack, body));
    // A user preset's text: the knobs as they stand, read back to the same sound.
    ParamStore a, b;
    a.parseText("lead.cutoff=2345 lead.vibrato=17 lead.level=-3");
    SoundPreset mine;
    const bool read = presetFromText(Module::Lead, "Mine", presetText(a, Module::Lead, 0), mine);
    applyPreset(b, Module::Lead, 0, mine);
    check(read && std::fabs(b.get(b.id(Module::Lead, 0, lead::Cutoff)) - 2345.0f) < 1.0f && std::fabs(b.get(b.id(Module::Lead, 0, lead::Vibrato)) - 17.0f) < 0.01f
          && b.get(b.id(Module::Lead, 0, lead::Level)) != -3.0f, "a user preset keeps the sound and leaves the mix", "");
}

/**
 * The rooms A and D and the resonance suppressor (fx/Rooms.h, Engine.h): early reflections without a tail, the
 * shimmer's octave feeding its own tail, a resonant row pulled back where its peak sticks out.
 */
void testSendsAD()
{
    section("early reflections, shimmer, resonance tamer");
    auto render = [](const Score& sc, const char* setting, double seconds) {
        Engine e;
        e.params().parseText("master.level=0 master.motion=0 master.clip=0");
        e.params().parseText(setting);
        e.prepare(48000.0, 256);
        e.load(sc);
        std::vector<float> out, l(256), r(256);
        for (int done = 0; done < static_cast<int>(48000 * seconds); done += 256) {
            e.process(l.data(), r.data(), 256);
            for (int i = 0; i < 256; ++i) out.push_back(0.5f * (l[i] + r[i]));
        }
        return out;
    };
    auto energy = [](const std::vector<float>& a, const std::vector<float>* b, double s0, double s1) {
        double e = 0.0;
        for (size_t i = static_cast<size_t>(s0 * 48000.0); i < static_cast<size_t>(s1 * 48000.0) && i < a.size(); ++i) {
            const double d = b != nullptr ? double(a[i]) - (*b)[i] : double(a[i]);
            e += d * d;
        }
        return e;
    };
    Score click;
    click.clear(120.0);
    click.notes.push_back({ 0.0, 0.05, Part::Row2, 57, 1.0f, false, false });
    click.lengthBeats = 8.0;
    const char* dry = "row2.echo=0 row2.reverb=0 row2.amp_decay=5 voice2.amp_decay=5 voice2.decay=20";
    const auto plain = render(click, dry, 1.5), early = render(click, (std::string(dry) + " row2.early=1").c_str(), 1.5);
    check(energy(early, &plain, 0.0, 0.15) > 100.0 * energy(early, &plain, 0.4, 1.5) && energy(early, &plain, 0.0, 0.15) > 1e-6,
          "early reflections: the room's first 80 ms, and no tail", fmt("%.2g in the first 150 ms, %.2g after 400", energy(early, &plain, 0.0, 0.15), energy(early, &plain, 0.4, 1.5)));
    Score pad;
    pad.clear(120.0);
    for (int k : { 57, 64 }) pad.notes.push_back({ 0.0, 1.0, Part::Strings, k, 0.9f, false, false });
    pad.lengthBeats = 16.0;
    const auto still = render(pad, "strings.reverb=0 strings.echo=0 strings.shimmer=1 shimmer.amount=0", 6.0);
    const auto shim = render(pad, "strings.reverb=0 strings.echo=0 strings.shimmer=1 shimmer.amount=0.6", 6.0);
    check(energy(shim, nullptr, 3.0, 6.0) > 1.5 * energy(still, nullptr, 3.0, 6.0), "the shimmer's octave feeds its own tail",
          fmt("tail %.2g with the octave, %.2g without", energy(shim, nullptr, 3.0, 6.0), energy(still, nullptr, 3.0, 6.0)));
    // The resonance tamer: noise with a loud 1 kHz sine (a ringing peak) and plain noise.
    auto tame = [](bool peak, double& sineIn, double& sineOut, double& noiseChange) {
        ResonanceTamer t;
        t.prepare(48000.0);
        Rng rng;
        rng.seed(3);
        std::vector<float> L(48000), R(48000), dry(48000);
        for (int i = 0; i < 48000; ++i) {
            const float x = 0.05f * rng.bipolar() + (peak ? 0.3f * static_cast<float>(std::sin(2.0 * 3.14159265358979 * 1000.0 * i / 48000.0)) : 0.0f);
            L[static_cast<size_t>(i)] = R[static_cast<size_t>(i)] = dry[static_cast<size_t>(i)] = x;
        }
        for (int i = 0; i < 48000; i += 32) t.process(L.data() + i, R.data() + i, 32, 1.0f);
        // The 1 kHz component in and out (correlation with the sine over the last half second), and how much changed.
        sineIn = sineOut = noiseChange = 0.0;
        double total = 0.0;
        for (int i = 24000; i < 48000; ++i) {
            const double s = std::sin(2.0 * 3.14159265358979 * 1000.0 * i / 48000.0);
            sineIn += dry[static_cast<size_t>(i)] * s;
            sineOut += L[static_cast<size_t>(i)] * s;
            noiseChange += (double(L[static_cast<size_t>(i)]) - dry[static_cast<size_t>(i)]) * (double(L[static_cast<size_t>(i)]) - dry[static_cast<size_t>(i)]);
            total += double(dry[static_cast<size_t>(i)]) * dry[static_cast<size_t>(i)];
        }
        noiseChange /= std::max(1e-30, total);
    };
    double in1, out1, ch1, in2, out2, ch2;
    tame(true, in1, out1, ch1);
    tame(false, in2, out2, ch2);
    check(std::fabs(out1) < 0.7 * std::fabs(in1) && ch2 < 0.05, "the resonance tamer pulls a peak back and leaves a flat spectrum",
          fmt("1 kHz peak %.1f dB lower; plain noise changed by %.1f %%", 20.0 * std::log10(std::fabs(in1) / std::max(1e-30, std::fabs(out1))), 100.0 * ch2));
}

/**
 * The modulation sequencer (Rack.h, ModVoice): a lane of cutoff offsets with its own length, so the timbre's accents
 * wander against the notes; a bright step sounds brighter.
 */
void testModLane()
{
    section("modulation sequencer");
    ParamStore p;
    p.parseText("compose.style=Modern row2.active=1 row2.length=16 row2.division=1/16 row2.mutation=0");
    Rack r;
    r.setup(p, 4);
    r.generate(1, RowRole::Counter);
    Score s;
    s.clear(120.0);
    r.run(s, 32.0);
    s.sort();
    float lo = 1e9f, hi = -1e9f;
    std::vector<float> bright;
    for (const NoteEvent& n : s.notes) if (n.part == Part::Row2) { lo = std::min(lo, n.bright); hi = std::max(hi, n.bright); bright.push_back(n.bright); }
    // The lane does not follow the row's 16 steps: the brightness of a bar differs from the next one's.
    bool differs = false;
    for (size_t i = 0; i + 16 < bright.size() && !differs; ++i) differs = bright[i] != bright[i + 16];
    check(lo < 0.0f && hi > 0.5f && differs, "a lane of its own length: bright and dark steps, not in step with the row",
          fmt("%.2f .. %.2f octaves", lo, hi));
    Score one;
    one.clear(120.0);
    one.notes.push_back({ 0.0, 0.5, Part::Row2, 57, 0.9f, false, false, 0.0f });
    one.notes.push_back({ 1.0, 0.5, Part::Row2, 57, 0.9f, false, false, 1.5f });
    one.lengthBeats = 4.0;
    Engine e;
    e.params().parseText("master.level=0 master.motion=0 row2.echo=0 row2.reverb=0 voice2.cutoff=400 voice2.env_amount=0");
    e.prepare(48000.0, 256);
    e.load(one);
    std::vector<float> out, l(256), rr(256);
    for (int done = 0; done < 48000; done += 256) { e.process(l.data(), rr.data(), 256); out.insert(out.end(), l.begin(), l.end()); }
    auto brightness = [&](size_t a, size_t b) {
        double d = 0.0, en = 0.0;
        for (size_t i = a + 1; i < b; ++i) { d += double(out[i] - out[i - 1]) * (out[i] - out[i - 1]); en += double(out[i]) * out[i]; }
        return d / std::max(1e-30, en);
    };
    const double dark = brightness(2400, 9600), light = brightness(26400, 33600);   // 0.05..0.2 s after each onset
    check(light > 1.5 * dark, "a bright step opens the filter for its note", fmt("brightness %.4f against %.4f", light, dark));
}

/**
 * The night set (composeNightSet): hours of pieces in mixed styles, a rung of the ladder apart, each laid over the
 * last one's outro on the other bank of rows -- both banks sounding together at every handover -- beat-matched, the
 * drone sliding into the new key.
 */
void testNightSet()
{
    section("night set");
    ParamStore p;
    p.parseText("compose.style=Cosmic compose.concert_minutes=480 compose.night_set=1");
    const auto t0 = std::chrono::steady_clock::now();
    const Score s = composeConcert(p, 11, 480.0);
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double secs = s.tempo.secondsAt(s.lengthBeats);
    struct Start { double beat; std::string style; };
    std::vector<Start> starts;
    for (const Marker& m : s.markers) {
        const size_t a = m.text.find(" ("), b = m.text.find("): ");
        if (m.text.rfind("Stueck ", 0) != 0 || a == std::string::npos || b == std::string::npos) continue;
        if (std::atoi(m.text.c_str() + 7) == static_cast<int>(starts.size()) + 1) starts.push_back({ m.beat, m.text.substr(a + 2, b - a - 2) });
    }
    check(std::fabs(secs - 28800.0) < 900.0 && starts.size() >= 25, "a night of pieces, about as long as asked",
          fmt("%zu pieces, %.2f h, composed in %.1f s", starts.size(), secs / 3600.0, took));
    static const char* const ladder[5] = { "Drift", "Doom", "Cosmic", "Modern", "Melodic" };
    auto rung = [](const std::string& name) { for (int k = 0; k < 5; ++k) if (name == ladder[k]) return k; return -9; };
    std::vector<std::string> kinds;
    bool adjacent = true, noThird = true;
    for (size_t i = 0; i < starts.size(); ++i) {
        if (std::find(kinds.begin(), kinds.end(), starts[i].style) == kinds.end()) kinds.push_back(starts[i].style);
        if (i > 0) adjacent = adjacent && std::abs(rung(starts[i].style) - rung(starts[i - 1].style)) <= 1;
        if (i > 1) noThird = noThird && !(starts[i].style == starts[i - 1].style && starts[i].style == starts[i - 2].style);
    }
    check(kinds.size() >= 4 && adjacent && noThird, "mixed styles, a rung of the ladder apart, never three of one in a row",
          fmt("%zu styles", kinds.size()));
    // Every handover: both banks of rows in one bar, no tempo change where the next piece starts, the drone sliding.
    int mixed = 0, matched = 0, slid = 0;
    std::vector<NoteEvent> drones;
    for (const NoteEvent& n : s.notes) if (n.part == Part::Drone) drones.push_back(n);
    for (size_t i = 1; i < starts.size(); ++i) {
        const double at = starts[i].beat;
        bool both = false;
        for (double bar = at; bar < at + 32.0 * kBeatsPerBar && !both; bar += kBeatsPerBar) {
            bool lo = false, hi = false;
            auto it = std::lower_bound(s.notes.begin(), s.notes.end(), bar, [](const NoteEvent& n, double v) { return n.beat < v; });
            for (; it != s.notes.end() && it->beat < bar + kBeatsPerBar; ++it) {
                const NoteEvent& n = *it;
                const int r = static_cast<int>(n.part) - static_cast<int>(Part::Row1);
                if (r >= 0 && r < 4) lo = true;
                if (r >= 4 && r < kRows) hi = true;
            }
            both = lo && hi;
        }
        mixed += both;
        matched += std::fabs(s.tempo.bpmAt(at - 0.5) - s.tempo.bpmAt(at + 0.5)) < 1e-6;
        const NoteEvent* before = nullptr;
        bool slides = false;
        for (const NoteEvent& n : drones) {
            if (n.beat < at - 1e-6) before = &n;
            else { slides = std::fabs(n.beat - at) < 1e-6 && before != nullptr && before->slide && before->beat + before->length > at; break; }
        }
        slid += slides;
    }
    const int handovers = static_cast<int>(starts.size()) - 1;
    check(mixed == handovers && matched == handovers, "every handover: the two pieces' rows together, beat-matched",
          fmt("%d and %d of %d", mixed, matched, handovers));
    check(slid == handovers, "the drone slides into the next piece's key", fmt("%d of %d", slid, handovers));
}

/**
 * The variation planner (the style guide's 4.3 and 6.4): inside a phase no sixteen bars pass without a planned
 * variation of a row; a break before the peak silences the rows and the drums; the decay lane lengthens a note's
 * filter envelope.
 */
void testVariations()
{
    section("variations and breaks");
    size_t events = 0;
    int gaps = 0, tooLong = 0, breaks = 0, loud = 0;
    for (const char* st : { "Melodic", "Modern", "Cosmic" }) {
        for (uint64_t seed : { 3u, 4u, 5u }) {
            ParamStore p;
            p.parseText(std::string("compose.style=") + st);
            const Score s = composePiece(p, seed, 10.0);
            std::vector<double> at, entries;
            for (const RackEvent& e : s.rack) {
                if (e.op == RackOp::Gate || e.op == RackOp::OctaveStep || e.op == RackOp::Direction || e.op == RackOp::Theme
                    || e.op == RackOp::SetLength) at.push_back(e.beat);
                if (e.op == RackOp::Start && e.row == 0) entries.push_back(e.beat);
            }
            std::sort(at.begin(), at.end());
            events += at.size();
            for (size_t i = 1; i < at.size(); ++i) {
                if (at[i] - at[i - 1] <= 1e-9) continue;
                ++gaps;
                bool newPhase = false;
                for (double b : entries) newPhase = newPhase || (b > at[i - 1] && b <= at[i]);
                if (at[i] - at[i - 1] > 16.0 * kBeatsPerBar + 1e-6 && !newPhase) ++tooLong;
            }
            // A break: the same rows stop at one beat and start again two or four bars later.
            for (const RackEvent& e : s.rack) {
                if (e.op != RackOp::Stop || e.row != 0) continue;
                for (const RackEvent& f : s.rack) {
                    if (f.op != RackOp::Start || f.row != 0) continue;
                    const double len = f.beat - e.beat;
                    if (std::fabs(len - 2 * kBeatsPerBar) > 1e-6 && std::fabs(len - 4 * kBeatsPerBar) > 1e-6) continue;
                    ++breaks;
                    for (const NoteEvent& n : s.notes) {
                        const int r = static_cast<int>(n.part) - static_cast<int>(Part::Row1);
                        if (((r >= 0 && r < kRows) || n.part == Part::Drums) && n.beat >= e.beat - 1e-6 && n.beat < f.beat - 1e-6) ++loud;
                    }
                }
            }
        }
    }
    check(events > 0 && tooLong == 0, "no sixteen bars of a phase without a planned variation",
          fmt("%zu events, %d of %d gaps too long", events, tooLong, gaps));
    check(breaks > 0 && loud == 0, "breaks before the peak: the rows and the drums silent", fmt("%d breaks, %d notes in them", breaks, loud));

    // The decay lane: the same note with its decay lengthened is brighter a fifth of a second later.
    Score one;
    one.clear(120.0);
    one.notes.push_back({ 0.0, 1.0, Part::Row2, 45, 0.9f, false, false, 0.0f, -1.0f });
    one.notes.push_back({ 2.0, 1.0, Part::Row2, 45, 0.9f, false, false, 0.0f, 1.5f });
    one.lengthBeats = 4.0;
    Engine e;
    e.params().parseText("master.level=0 master.motion=0 row2.echo=0 row2.reverb=0 voice2.cutoff=250 voice2.env_amount=4 voice2.decay=100");
    e.prepare(48000.0, 256);
    e.load(one);
    std::vector<float> out, l(256), rr(256);
    for (int done = 0; done < 96000; done += 256) { e.process(l.data(), rr.data(), 256); out.insert(out.end(), l.begin(), l.end()); }
    auto brightness = [&](size_t a, size_t b) {
        double d = 0.0, en = 0.0;
        for (size_t i = a + 1; i < b; ++i) { d += double(out[i] - out[i - 1]) * (out[i] - out[i - 1]); en += double(out[i]) * out[i]; }
        return d / std::max(1e-30, en);
    };
    const double shortTail = brightness(1440, 5760), longTail = brightness(49440, 53760);   // 0.03..0.12 s after each onset
    check(longTail > 1.5 * shortTail, "the decay lane: a longer filter decay keeps the note open longer", fmt("%.5f against %.5f", longTail, shortTail));
}

/**
 * The composer's sounds (compose.pick_sounds): a preset for every synth from the groups of its part, the bass not
 * too bright and the main sequence resonant; other seeds other sounds; a reroll of the sounds leaves the notes; the
 * hands move around the preset's cutoff; off, the knobs alone; in a night set the second bank's rows get theirs.
 */
void testSounds()
{
    section("the composer's sounds");
    ParamStore p;
    p.parseText("compose.style=Melodic");
    const Score a = composePiece(p, 7, 8.0);
    auto pickOf = [](const Score& s, Module m, int inst) {
        for (const SoundPick& k : s.sounds) if (k.module == static_cast<int>(m) && k.instance == inst) return k.preset;
        return -1;
    };
    int synths = 0;
    for (Module m : { Module::Lead, Module::Drone, Module::Tape, Module::Strings, Module::Drums, Module::Atmos }) synths += pickOf(a, m, 0) >= 0;
    const int bass = pickOf(a, Module::Voice, 0), main = pickOf(a, Module::Voice, 1);
    const std::vector<SoundPreset>& voices = factoryPresets(Module::Voice);
    const std::string bassGroup = bass >= 0 ? voices[static_cast<size_t>(bass)].group : "", mainGroup = main >= 0 ? voices[static_cast<size_t>(main)].group : "";
    const bool bassFits = bassGroup == "Ladder Bass" || bassGroup == "Deep Ostinato" || bassGroup == "Dark Throb" || bassGroup == "Warm Unison";
    check(synths == 6 && bassFits && main >= 0, "a preset for every synth, the bass from a bass group",
          fmt("%d synths; bass \"%s\" (%s), main sequence \"%s\" (%s)", synths, bass >= 0 ? voices[static_cast<size_t>(bass)].name.c_str() : "-",
              bassGroup.c_str(), main >= 0 ? voices[static_cast<size_t>(main)].name.c_str() : "-", mainGroup.c_str()));
    // Where the knobs stand at the start: the bass's cutoff at most 420 Hz, the main sequence's resonance at least 0.45.
    // The settings' offset of a knob at the start (hand 1's steps; the hands' own start after them).
    auto setting = [](const Score& s, int id) { float v = 0.0f; for (const Gesture& g : s.gestures) if (g.param == id && g.hand == 1 && g.beat <= 0.0) v = g.to; return v; };
    auto at0 = [&](const Score& s, int id) {
        for (const KnobSet& k : s.knobs) if (k.param == id && k.beat <= 0.0) return k.value;   // the knob itself (26.09.2026)
        return p.fromNormalised(id, std::clamp(p.toNormalised(id, p.get(id)) + setting(s, id), 0.0f, 1.0f));
    };
    const float cut = at0(a, p.id(Module::Voice, 0, voice::Cutoff)), res = at0(a, p.id(Module::Voice, 1, voice::Resonance));
    check(cut <= 421.0f && res >= 0.449f, "nudged to the part: a dark enough bass, a resonant main sequence", fmt("%.0f Hz, resonance %.2f", cut, res));
    std::vector<int> basses;
    for (uint64_t seed : { 1u, 2u, 3u, 4u, 5u }) {
        const int b = pickOf(composePiece(p, seed, 6.0), Module::Voice, 0);
        if (std::find(basses.begin(), basses.end(), b) == basses.end()) basses.push_back(b);
    }
    check(basses.size() >= 3, "other pieces, other sounds", fmt("%zu basses in five pieces", basses.size()));
    Curation cur;
    cur.reroll("sounds");
    const Score b = composePiece(p, 7, 8.0, 0, &cur);
    bool sameNotes = a.notes.size() == b.notes.size();
    for (size_t i = 0; sameNotes && i < a.notes.size(); ++i) sameNotes = a.notes[i].beat == b.notes[i].beat && a.notes[i].pitch == b.notes[i].pitch;
    check(sameNotes && pickOf(b, Module::Voice, 0) != bass, "a reroll of the sounds draws new sounds and leaves the notes", "");
    // The hands' first movement of the bass's cutoff begins where the preset put it (within their reach of it).
    const int cid = p.id(Module::Voice, 0, voice::Cutoff);
    const float base = setting(a, cid);
    float first = 99.0f;
    for (const Gesture& g : a.gestures) if (g.param == cid && g.hand <= 1 && g.length > 0.0) { first = g.from; break; }
    check(std::fabs(first - base) <= 0.5f, "the hands move the cutoff around the preset's", fmt("preset %.2f, first move from %.2f", base, first));
    ParamStore off;
    off.parseText("compose.style=Melodic compose.pick_sounds=0");
    check(composePiece(off, 7, 8.0).sounds.empty(), "off: no presets, the knobs as they are", "");
    ParamStore night;
    night.parseText("compose.style=Cosmic compose.night_set=1");
    const Score n = composeConcert(night, 5, 40.0);
    bool high = false;
    for (const SoundPick& k : n.sounds) high = high || (k.module == static_cast<int>(Module::Voice) && k.instance >= 4);
    check(high, "a night set: the second bank's rows get their own sounds", fmt("%zu picks", n.sounds.size()));
    // The sounds are the knobs themselves (26.09.2026): no offset of a knob the piece sets, the engine puts them on the
    // knobs as it loads the piece, and a concert's next piece puts its own on as it begins -- and back on a jump back.
    std::string why;
    for (const KnobSet& k : a.knobs)
        for (const Gesture& g : a.gestures)
            if (g.param == k.param && g.hand == 1 && g.beat <= 1e-9 && g.shape == GestureShape::Step && why.empty()) why = "offset on " + p.key(k.param);
    Engine e;
    e.params().parseText("compose.style=Melodic");
    e.prepare(48000.0, 512);
    e.load(a);
    for (const KnobSet& k : a.knobs)
        if (e.params().get(k.param) != k.value && why.empty()) why = fmt("%s is %g, not %g", p.key(k.param).c_str(), e.params().get(k.param), k.value);
    check(why.empty() && !a.knobs.empty(), "the sounds are set on the knobs, not as offsets", why.empty() ? fmt("%zu knobs set", a.knobs.size()) : why);
    // The mix as well (the user, 26.09.2026): the piece's pans, sends, low cuts on the faders, no step at its start left;
    // the counter rows out to alternating sides, the bass in the middle.
    int mixKnobs = 0, steps = 0;
    for (const KnobSet& k : a.knobs) mixKnobs += k.kind == 1;
    for (int r = 0; r < kRows; ++r)
        for (int idx : { row::Pan, row::EchoSend, row::LowCut, row::Distance })
            for (const Gesture& g : a.gestures)
                steps += g.param == p.id(Module::Row, r, idx) && g.beat <= 1e-9 && g.shape == GestureShape::Step;
    const float pan0 = knobAt(p, a, p.id(Module::Row, 0, row::Pan)), pan2 = knobAt(p, a, p.id(Module::Row, 2, row::Pan));
    const float pan3 = knobAt(p, a, p.id(Module::Row, 3, row::Pan));
    check(mixKnobs > 10 && steps == 0 && pan0 == 0.0f && pan2 * pan3 < 0.0f, "the mix is set on the faders, the moves along the form from there",
          fmt("%d mix knobs, %d steps left; pans %.2f, %.2f, %.2f", mixKnobs, steps, pan0, pan2, pan3));
    // A concert's next piece puts its sounds on the knobs as it begins, on the sample's cell; a jump back into the first
    // piece puts the first's on again, a jump inside a piece leaves the knobs as the player has them.
    Score con;
    con.clear(120.0);
    con.lengthBeats = 32.0;
    const int cutId = p.id(Module::Voice, 0, voice::Cutoff), vcoId = p.id(Module::Voice, 0, voice::Vco);
    con.knobs = { { 0.0, cutId, 300.0f, static_cast<int>(Module::Voice), 0 }, { 0.0, vcoId, 1.0f, static_cast<int>(Module::Voice), 0 },
                  { 16.0, cutId, 900.0f, static_cast<int>(Module::Voice), 0 }, { 16.0, vcoId, 3.0f, static_cast<int>(Module::Voice), 0 } };
    Engine ce;
    ce.prepare(48000.0, 512);
    ce.load(con);
    std::vector<float> l(512), r(512);
    const bool atFirst = ce.params().get(cutId) == 300.0f && ce.params().get(vcoId) == 1.0f;
    ce.params().set(cutId, 450.0f);   // the player's hand in the first piece
    while (ce.beat() < 15.9) ce.process(l.data(), r.data(), 512);
    const bool atKept = ce.params().get(cutId) == 450.0f;
    while (ce.beat() < 16.1) ce.process(l.data(), r.data(), 512);
    const bool atSecond = ce.params().get(cutId) == 900.0f && ce.params().get(vcoId) == 3.0f;
    ce.seek(20.0);
    ce.params().set(cutId, 700.0f);
    ce.seek(24.0);   // inside the second piece: the hand's value stays
    const bool atInside = ce.params().get(cutId) == 700.0f;
    ce.seek(4.0);    // back into the first: its sounds
    const bool atBack = ce.params().get(cutId) == 300.0f && ce.params().get(vcoId) == 1.0f;
    check(atFirst && atKept && atSecond && atInside && atBack, "a concert's next piece brings its sounds on the knobs, a jump back the first's",
          fmt("start %d, hand kept %d, next piece %d, jump inside %d, jump back %d", atFirst, atKept, atSecond, atInside, atBack));
}

/**
 * The wavetables and the pad synth (Wavetable.h, Poly.h): every table built; a high note reads a poorer level; the
 * synth sounds, scans its table, fades after the key and does not care how the blocks are cut; 1024 presets with
 * names of their own; the composer's pads never make a third plane.
 */
void testPoly()
{
    section("wavetables and the pad synth");
    int built = 0, frames = 0;
    for (int i = 0; i < kWavetableCount; ++i) { built += !wavetable(i).empty(); frames += wavetable(i).frames; }
    const int hi = cycleLevelFor(4000.0, 48000.0, -1), lo = cycleLevelFor(100.0, 48000.0, -1);
    check(built == kWavetableCount && CycleTable::levelHarmonics(hi) * 4000 < 24000 && lo == 0,
          "every table built; a high note reads a level below Nyquist", fmt("%d tables, %d frames; level %d at 4 kHz", built, frames, hi));

    auto render = [](const PolySettings& ps, int block, int samples, int releaseAt) {
        PolySynth p;
        p.prepare(48000.0, 7);
        p.set(ps);
        p.noteOn(57, 0.8f, 1);
        p.noteOn(64, 0.8f, 2);
        p.noteOn(69, 0.8f, 3);
        std::vector<float> out;
        std::vector<float> l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
        for (int done = 0; done < samples; done += block) {
            if (done >= releaseAt && done - block < releaseAt) { p.noteOff(1); p.noteOff(2); p.noteOff(3); }
            const int n = std::min(block, samples - done);
            p.set(ps);
            p.process(l.data(), r.data(), n);
            for (int i = 0; i < n; ++i) { out.push_back(l[static_cast<size_t>(i)]); out.push_back(r[static_cast<size_t>(i)]); }
        }
        return out;
    };
    PolySettings ps;
    ps.attackS = 0.1f;
    ps.releaseS = 0.3f;
    const auto a = render(ps, 256, 48000 * 3, 48000), b = render(ps, 97, 48000 * 3, 48000);
    double loud = 0.0, tail = 0.0;
    for (size_t i = 48000; i < 96000; ++i) loud += double(a[i]) * a[i];
    for (size_t i = a.size() - 9600; i < a.size(); ++i) tail += double(a[i]) * a[i];
    check(loud > 1.0 && tail < 1e-6 * loud, "a chord sounds and fades after the keys", fmt("energy %.1f, tail %.2g", loud, tail));
    // Bit for bit whatever the blocks (the note-offs fall on different samples: compare the first second only).
    bool same = true;
    for (size_t i = 0; i < 2 * 47000 && same; ++i) same = a[i] == b[i];
    check(same, "the same sound however the blocks are cut", "");
    // The scan moves the timbre: the brightness of successive tenths of a second varies with it, hardly without.
    auto spread = [](const std::vector<float>& v) {
        double lo2 = 1e30, hi2 = 0.0;
        for (size_t w = 1; w + 1 < 10; ++w) {
            double d = 0.0, e = 0.0;
            for (size_t i = w * 9600 + 2; i < (w + 1) * 9600; i += 2) { d += double(v[i] - v[i - 2]) * (v[i] - v[i - 2]); e += double(v[i]) * v[i]; }
            lo2 = std::min(lo2, d / e); hi2 = std::max(hi2, d / e);
        }
        return hi2 / lo2;
    };
    PolySettings still = ps, moving = ps;
    still.scan = 0.0f;
    moving.scan = 1.0f; moving.scanHz = 1.0f;
    still.table = moving.table = 3;   // Formant: the peak climbs
    still.cutoffHz = moving.cutoffHz = 12000.0f;
    still.chorus = moving.chorus = 0.0f;
    still.driftCents = moving.driftCents = 0.0f;
    const double sStill = spread(render(still, 256, 48000, 96000)), sMove = spread(render(moving, 256, 48000, 96000));
    check(sMove > 1.3 * sStill, "the scan walks through the table", fmt("brightness varies %.2fx against %.2fx", sMove, sStill));

    const std::vector<SoundPreset>& list = factoryPresets(Module::Poly);
    std::vector<std::string> names;
    for (const SoundPreset& pr : list) names.push_back(pr.name);
    std::sort(names.begin(), names.end());
    check(list.size() == 1024 && std::unique(names.begin(), names.end()) == names.end(), "1024 pad presets, every name its own",
          fmt("%zu", list.size()));

    int pieces = 0, third = 0;
    for (const char* st : { "Cosmic", "Doom", "Melodic", "Modern", "Drift" }) {
        for (uint64_t seed : { 1u, 2u, 3u }) {
            ParamStore q;
            q.parseText(std::string("compose.style=") + st);
            const Score sc = composePiece(q, seed, 8.0);
            bool any = false;
            for (const NoteEvent& n : sc.notes) {
                if (n.part != Part::Pad) continue;
                any = true;
                bool tape = false, strings = false;
                for (const NoteEvent& m : sc.notes) {
                    if (m.beat >= n.beat + n.length || m.beat + m.length <= n.beat) continue;
                    tape = tape || m.part == Part::TapeKeys;
                    strings = strings || m.part == Part::Strings;
                }
                third += tape && strings;
            }
            pieces += any;
        }
    }
    check(pieces >= 5 && third == 0, "the composer's pads: in many pieces, never a third plane", fmt("%d of 15 pieces, %d notes over two planes", pieces, third));
}

/**
 * The rows' wavetables (voice.table): a row reads its table, and the note's modulation step moves its place in it --
 * the formant table's peak climbs with a bright step; the composer gives some counter rows a table.
 */
void testRowTables()
{
    section("the rows' wavetables");
    auto render = [](const char* setting, float bright0, float bright1) {
        Score one;
        one.clear(120.0);
        one.notes.push_back({ 0.0, 1.0, Part::Row2, 57, 0.9f, false, false, bright0 });
        one.notes.push_back({ 2.0, 1.0, Part::Row2, 57, 0.9f, false, false, bright1 });
        one.lengthBeats = 4.0;
        Engine e;
        e.params().parseText(std::string("master.level=0 master.motion=0 row2.echo=0 row2.reverb=0 voice2.cutoff=16000 voice2.env_amount=0 ") + setting);
        e.prepare(48000.0, 256);
        e.load(one);
        std::vector<float> out, l(256), r(256);
        for (int done = 0; done < 96000; done += 256) { e.process(l.data(), r.data(), 256); out.insert(out.end(), l.begin(), l.end()); }
        return out;
    };
    auto brightness = [](const std::vector<float>& v, size_t a, size_t b) {
        double d = 0.0, en = 0.0;
        for (size_t i = a + 1; i < b; ++i) { d += double(v[i] - v[i - 1]) * (v[i] - v[i - 1]); en += double(v[i]) * v[i]; }
        return d / std::max(1e-30, en);
    };
    const auto analog = render("", 0.0f, 0.0f), formant = render("voice2.table=Formant voice2.table_pos=0 voice2.table_mod=1", 0.0f, 1.5f);
    double diff = 0.0;
    for (size_t i = 2400; i < 12000; ++i) diff += std::fabs(double(analog[i]) - formant[i]);
    const double low = brightness(formant, 2400, 12000), high = brightness(formant, 50400, 60000);
    check(diff > 1.0 && high > 2.0 * low, "a row reads its table; a bright step moves further into it",
          fmt("brightness %.4f, then %.4f", low, high));
    int rows = 0;
    for (uint64_t seed : { 1u, 2u, 3u, 4u, 5u, 6u }) {
        ParamStore q;
        q.parseText("compose.style=Modern");
        const Score sc = composePiece(q, seed, 6.0);
        for (const KnobSet& k : sc.knobs)
            for (int r = 0; r < kRows; ++r)
                if (k.param == q.id(Module::Voice, r, voice::Table) && k.value > 0.0f) ++rows;
    }
    check(rows > 0, "the composer gives counter rows a wavetable", fmt("%d rows in six Modern pieces", rows));
}

/**
 * The timbre drift (the style guide's 4.3): every counter row that plays has a knob of its timbre wandering in long
 * glides, the pad synth its place in the table; the bass has none.
 */
void testTimbreDrift()
{
    section("timbre drift");
    int rows = 0, drifting = 0, bass = 0, pads = 0, padDrift = 0;
    for (uint64_t seed : { 1u, 2u, 3u, 4u }) {
        ParamStore q;
        q.parseText("compose.style=Melodic");
        const Score sc = composePiece(q, seed, 8.0);
        for (int r = 0; r < kRows; ++r) {
            bool plays = false;
            for (const NoteEvent& n : sc.notes) if (n.part == rowPart(r)) { plays = true; break; }
            bool long3 = false;
            for (const Gesture& g : sc.gestures)
                if (g.hand == 3 && g.length >= 100.0 && (g.param == q.id(Module::Voice, r, voice::Wave) || g.param == q.id(Module::Voice, r, voice::PulseWidth)
                                                          || g.param == q.id(Module::Voice, r, voice::TablePos))) long3 = true;
            if (r == 0) { bass += long3; continue; }
            rows += plays;
            drifting += plays && long3;
        }
        bool hasPad = false, posDrift = false;
        for (const NoteEvent& n : sc.notes) hasPad = hasPad || n.part == Part::Pad;
        for (const Gesture& g : sc.gestures) posDrift = posDrift || (g.param == q.id(Module::Poly, 0, poly::Position) && g.hand == 3 && g.length >= 100.0);
        pads += hasPad;
        padDrift += hasPad && posDrift;
    }
    check(rows > 0 && drifting == rows && bass == 0, "every counter row's timbre wanders, the bass's not", fmt("%d of %d rows", drifting, rows));
    check(pads == padDrift, "the pad synth wanders through its table", fmt("%d of %d pieces with pads", padDrift, pads));
}

/**
 * The filter models (Filters.h): in the small-signal range each follows its circuit's linear transfer function; each
 * that should oscillates at its cutoff at full resonance; none runs away at full resonance and a loud input.
 */
void testFilters()
{
    section("filter models");
    const double fs = 96000.0, fc = 1000.0;
    const float g = static_cast<float>(std::tan(3.14159265358979 * fc / fs));
    enum { MOOG, PROPHET, JUNO, SEM, XPANDER, DIODE, KORG, POLIVOKS, WASP };
    auto run = [&](int model, float res, std::function<float(int)> input, int n, float drive = 1.0f) {
        float v[4] = {}, st[4] = {};
        std::vector<float> out(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const float x = input(i) * drive;
            float y = 0.0f;
            switch (model) {
            case MOOG: y = ladderMoog<float>(v, st, x, g, FilterVoicing::feedback(FilterModel::Moog, res)); break;
            case PROPHET: y = otaCascade<float>(v, st, x, g, FilterVoicing::feedback(FilterModel::Prophet, res), FilterVoicing::otaDrive(FilterModel::Prophet), FilterVoicing::otaRes(FilterModel::Prophet)); break;
            case JUNO: y = otaCascade<float>(v, st, x, g, FilterVoicing::feedback(FilterModel::Juno, res), FilterVoicing::otaDrive(FilterModel::Juno), FilterVoicing::otaRes(FilterModel::Juno)); break;
            case SEM: y = svfNonlinear<float>(v, st, x, g, FilterVoicing::feedback(FilterModel::Sem, res), 1.5f, 0.0f, 0.0f); break;
            case XPANDER: y = otaCascade<float>(v, st, x, g, FilterVoicing::feedback(FilterModel::Xpander, res), FilterVoicing::otaDrive(FilterModel::Xpander), FilterVoicing::otaRes(FilterModel::Xpander), 2); break;
            case DIODE: y = diodeLadder<float>(v, st, x, g * 0.70710678f, FilterVoicing::feedback(FilterModel::Diode, res)); break;
            case KORG: y = korg35<float>(v, st, x, g, FilterVoicing::feedback(FilterModel::Korg35, res)); break;
            case POLIVOKS: y = svfNonlinear<float>(v, st, x, g, FilterVoicing::feedback(FilterModel::Polivoks, res), 0.35f, 0.0f, 0.0f); break;
            default: y = svfNonlinear<float>(v, st, x, g, FilterVoicing::feedback(FilterModel::Wasp, res), 0.6f, 0.35f, 0.0f); break;
            }
            out[static_cast<size_t>(i)] = y;
        }
        return out;
    };
    // The gain at f of a small sine, after the transient.
    auto gainAt = [&](int model, double f) {
        const double w = 2.0 * 3.14159265358979 * f / fs;
        const int n = 96000;
        const auto y = run(model, 0.0f, [&](int i) { return static_cast<float>(0.01 * std::sin(w * i)); }, n);
        double re = 0.0, im = 0.0;
        for (int i = n / 2; i < n; ++i) { re += y[static_cast<size_t>(i)] * std::cos(w * i); im += y[static_cast<size_t>(i)] * std::sin(w * i); }
        return 2.0 * std::sqrt(re * re + im * im) / (n / 2) / 0.01;
    };
    // The analog prototypes at the prewarped frequency (the bilinear transform is exact at fc).
    auto analog = [&](int model, double f) {
        const double wa = std::tan(3.14159265358979 * f / fs) / std::tan(3.14159265358979 * fc / fs);
        const std::complex<double> sj(0.0, wa);
        switch (model) {
        case SEM: return 1.0 / std::abs(sj * sj + 1.414 * sj + 1.0);
        case DIODE: { const std::complex<double> pz = sj * 1.41421356 + 2.0; return 1.0 / std::abs(pz * pz * pz * pz * 0.5 - 2.0 * pz * pz + 1.0); }
        case KORG: return 1.0 / std::abs(sj * sj + 3.0 * sj + 1.0);
        default: return 1.0 / std::pow(std::abs(sj + 1.0), 4.0);
        }
    };
    const char* names[] = { "Moog", "Prophet", "Juno", "SEM", "", "Diode", "Korg35" };
    double worst = 0.0;
    std::string where;
    for (int m : { MOOG, PROPHET, JUNO, SEM, DIODE, KORG }) {
        for (double f : { 250.0, 1000.0, 3000.0 }) {
            const double e = std::fabs(20.0 * std::log10(gainAt(m, f) / analog(m, f)));
            if (e > worst) { worst = e; where = std::string(names[m]) + " at " + std::to_string(static_cast<int>(f)) + " Hz"; }
        }
    }
    check(worst < 0.5, "small signals follow each circuit's linear response", fmt("worst %.2f dB (%s)", worst, where.c_str()));
    // Self-oscillation: kicked once, at full resonance, each ladder, cascade, diode ladder and Sallen-Key rings on at fc.
    int ringing = 0;
    std::string freqs;
    for (int m : { MOOG, PROPHET, DIODE, KORG }) {
        const auto y = run(m, 1.02f, [](int i) { return i == 0 ? 1.0f : 0.0f; }, 96000);
        double e = 0.0;
        int crossings = 0;
        for (size_t i = 48000; i < y.size(); ++i) { e += double(y[i]) * y[i]; crossings += (y[i - 1] < 0.0f) != (y[i] < 0.0f); }
        const double rms = std::sqrt(e / 48000.0), f = crossings / 2.0 / 0.5;
        ringing += rms > 0.01 && std::fabs(f - fc) < 0.08 * fc;
        freqs += fmt(" %s %.0f Hz", names[m], f);
    }
    check(ringing == 4, "at full resonance each oscillates at its cutoff", freqs);
    // Loud and at full resonance: bounded.
    bool bounded = true;
    std::string peaks;
    for (int m = MOOG; m <= WASP; ++m) {
        const auto y = run(m, 1.0f, [](int i) { return std::sin(0.05f * i) > 0.0f ? 1.0f : -1.0f; }, 48000, 4.0f);
        float peak = 0.0f;
        bool finite = true;
        for (float x : y) { finite = finite && std::isfinite(x); peak = std::max(peak, std::fabs(x)); }
        bounded = bounded && finite && peak < 12.0f;
        peaks += fmt(" %.1f", finite ? static_cast<double>(peak) : -1.0);
    }
    check(bounded, "full resonance and a loud square: every model stays finite and bounded", "peaks" + peaks);
}

/**
 * Every filter model through a row's voice (voice.filter): each sounds, stays finite, and is a sound of its own.
 */
void testFilterVoices()
{
    section("filter models in the voices");
    std::vector<double> bright;
    bool ok = true;
    std::string info;
    for (int m = 0; m < kFilterModels; ++m) {
        Score one;
        one.clear(120.0);
        one.notes.push_back({ 0.0, 1.5, Part::Row2, 45, 0.9f, true, false });
        one.lengthBeats = 2.0;
        Engine e;
        e.params().parseText(fmt("master.level=0 master.motion=0 row2.echo=0 row2.reverb=0 voice2.cutoff=700 voice2.resonance=0.6 voice2.filter=%d voice2.filter_mode=0.3", m));
        e.prepare(48000.0, 256);
        e.load(one);
        std::vector<float> out, l(256), r(256);
        for (int done = 0; done < 48000; done += 256) { e.process(l.data(), r.data(), 256); out.insert(out.end(), l.begin(), l.end()); }
        double en = 0.0, d = 0.0;
        for (size_t i = 4801; i < out.size(); ++i) { en += double(out[i]) * out[i]; d += double(out[i] - out[i - 1]) * (out[i] - out[i - 1]); }
        const double rms = std::sqrt(en / double(out.size() - 4801));
        ok = ok && std::isfinite(rms) && rms > 1e-3;
        bright.push_back(d / std::max(1e-30, en));
        info += fmt(" %d:%.3f", m, rms);
    }
    int distinct = 0;
    for (size_t a = 0; a < bright.size(); ++a) {
        bool own = true;
        for (size_t b = 0; b < a; ++b) own = own && std::fabs(bright[a] / bright[b] - 1.0) > 0.03;
        distinct += own;
    }
    check(ok, "every model sounds and stays finite in a voice", "rms" + info);
    check(distinct >= 8, "and each is a sound of its own", fmt("%d of %d distinct", distinct, kFilterModels));
    // The presets carry the filters of their instruments: the voices' use every model, the pad synth's several.
    auto modelsIn = [](Module m, int index) {
        std::vector<int> seen;
        for (const SoundPreset& pr : factoryPresets(m))
            for (const auto& v : pr.values)
                if (v.first == index && std::find(seen.begin(), seen.end(), static_cast<int>(v.second)) == seen.end()) seen.push_back(static_cast<int>(v.second));
        return seen.size();
    };
    const size_t voiceModels = modelsIn(Module::Voice, voice::Filter), polyModels = modelsIn(Module::Poly, poly::Filter);
    check(voiceModels == static_cast<size_t>(kFilterModels) && polyModels >= 6, "the presets carry the filters of their instruments",
          fmt("%zu models in the voices' presets, %zu in the pad synth's", voiceModels, polyModels));
}

/**
 * The voices' modulation (26.09.2026, Modulation.h): a synced LFO reads its phase off the beat, a fading one comes in
 * over its time, a square LFO on the level opens and shuts a held note, the filter's sustain holds it brighter, and all
 * of it is the same for any host block size.
 */
void testModulation()
{
    section("envelopes, LFOs and the modulation matrix");
    {
        // A one-bar saw, not retriggered, on the blend (span 1): -1 on the downbeat, 0 at beat 2, 0.5 at beat 3.
        Modulator m;
        m.prepare(48000.0, 1);
        ModSettings ms;
        ms.lfo[0].shape = static_cast<int>(LfoShape::SawUp);
        ms.lfo[0].sync = 3;
        ms.slot[0] = { static_cast<int>(ModSource::Lfo1), static_cast<int>(ModDest::Wave), 1.0f };
        m.set(ms);
        float ext[kModSources] = {}, out[kModDests] = {};
        const int wave = static_cast<int>(ModDest::Wave);
        m.evaluate(0, 4.0, ext, out);
        const float a = out[wave];
        m.evaluate(48000, 6.0, ext, out);
        const float b = out[wave];
        m.evaluate(72000, 7.0, ext, out);
        const float c = out[wave];
        check(std::fabs(a + 1.0f) < 1e-5f && std::fabs(b) < 1e-5f && std::fabs(c - 0.5f) < 1e-5f, "a synced LFO reads its phase off the beat",
              fmt("%.4f %.4f %.4f", static_cast<double>(a), static_cast<double>(b), static_cast<double>(c)));
        // A free square of 2 Hz, retriggered, fading in over a second: half its swing half a second after the note.
        ms = ModSettings{};
        ms.lfo[1] = { 2.0f, static_cast<int>(LfoShape::Square), 0, 1, 1.0f };
        ms.slot[0] = { static_cast<int>(ModSource::Lfo2), static_cast<int>(ModDest::Level), 1.0f };
        m.set(ms);
        m.evaluate(1000, 0.0, ext, out);
        m.noteOn(1000, 0.0);
        m.evaluate(1000 + 6000, 0.0, ext, out);   // 1/8 s: the first half cycle, +1, faded to 1/8
        const float f1 = out[static_cast<int>(ModDest::Level)];
        m.evaluate(1000 + 24000 + 6000, 0.0, ext, out);   // 5/8 s: the second cycle's first half, faded to 5/8
        const float f2 = out[static_cast<int>(ModDest::Level)];
        check(std::fabs(f1 - 0.125f) < 1e-3f && std::fabs(f2 - 0.625f) < 1e-3f, "a retriggered LFO starts with the note and fades in",
              fmt("%.4f %.4f", static_cast<double>(f1), static_cast<double>(f2)));
    }
    // A held note through the engine, 2 s.
    auto render = [](const char* text, int block, double seconds, bool two = false) {
        Score one;
        one.clear(120.0);
        one.notes.push_back({ 0.0, 4.0, Part::Row2, 45, 0.9f, false, false });
        if (two) for (int k = 0; k < 8; ++k) one.notes.push_back({ 0.5 * k, 0.3, Part::Row3, 57 + k, 0.7f, k == 3, false });
        one.lengthBeats = 5.0;
        Engine e;
        e.params().parseText(std::string("master.level=0 master.motion=0 row2.echo=0 row2.reverb=0 voice2.cutoff=500 voice2.env_amount=3 ") + text);
        e.prepare(48000.0, block);
        e.load(one);
        std::vector<float> out, l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
        const int total = static_cast<int>(48000.0 * seconds);
        for (int done = 0; done < total; done += block) {
            const int n = std::min(block, total - done);
            e.process(l.data(), r.data(), n);
            out.insert(out.end(), l.begin(), l.begin() + n);
        }
        return out;
    };
    auto rms = [](const std::vector<float>& v, double from, double to) {
        double e = 0.0;
        const size_t a = static_cast<size_t>(from * 48000.0), b = static_cast<size_t>(to * 48000.0);
        for (size_t i = a; i < b; ++i) e += static_cast<double>(v[i]) * v[i];
        return std::sqrt(e / static_cast<double>(b - a));
    };
    {
        // A square of 4 Hz on the level, amount -1: shut on its first half cycle, doubled on its second.
        const std::vector<float> v = render("voice2.lfo1_rate=4 voice2.lfo1_shape=4 voice2.lfo1_retrig=1 voice2.mod1_src=1 voice2.mod1_dst=9 voice2.mod1_amt=-1", 256, 1.2);
        double shut = 0.0, open = 0.0;
        for (int k = 0; k < 4; ++k) {
            shut += rms(v, 0.25 * k + 0.02, 0.25 * k + 0.105);
            open += rms(v, 0.25 * k + 0.145, 0.25 * k + 0.23);
        }
        check(open > 0.01 && shut < 0.02 * open, "a square LFO on the level opens and shuts a held note",
              fmt("rms %.5f shut, %.4f open", shut / 4.0, open / 4.0));
    }
    {
        // The filter envelope's sustain (release unlinked) holds the held note brighter than its decay to nothing.
        auto bright = [&](const char* text) {
            const std::vector<float> v = render(text, 256, 1.8);
            double en = 0.0, d = 0.0;
            for (size_t i = 48000; i < 86400; ++i) { en += double(v[i]) * v[i]; d += double(v[i] - v[i - 1]) * (v[i] - v[i - 1]); }
            return d / std::max(1e-30, en);
        };
        const double off = bright("voice2.filt_link=0"), on = bright("voice2.filt_link=0 voice2.filt_sustain=0.7");
        check(on > 2.0 * off, "the filter envelope's sustain holds the note open", fmt("brightness %.5f without, %.5f with", off, on));
    }
    {
        // Every source and destination at once on two voices, one of them on a wavetable: blocks of 37 equal blocks of 512.
        const char* busy =
            "voice2.lfo1_rate=3.3 voice2.lfo1_shape=5 voice2.mod1_src=1 voice2.mod1_dst=5 voice2.mod1_amt=0.4 "
            "voice2.lfo2_sync=6 voice2.lfo2_shape=6 voice2.mod2_src=2 voice2.mod2_dst=1 voice2.mod2_amt=0.3 "
            "voice2.mod3_src=5 voice2.mod3_dst=6 voice2.mod3_amt=0.5 voice2.mod_attack=80 voice2.mod_sustain=0.4 "
            "voice2.lfo3_rate=0.7 voice2.lfo3_retrig=1 voice2.lfo3_fade=0.5 voice2.lfo3_shape=1 voice2.mod4_src=3 voice2.mod4_dst=10 voice2.mod4_amt=0.7 "
            "voice2.mod5_src=4 voice2.mod5_dst=2 voice2.mod5_amt=0.5 voice2.lfo4_shape=2 voice2.lfo4_rate=6 voice2.mod6_src=4 voice2.mod6_dst=3 voice2.mod6_amt=0.6 "
            "voice2.filter=3 voice2.mod7_src=6 voice2.mod7_dst=7 voice2.mod7_amt=0.8 voice2.mod8_src=7 voice2.mod8_dst=8 voice2.mod8_amt=0.5 "
            "voice2.filt_link=0 voice2.filt_sustain=0.3 voice2.filt_release=90 voice2.amp_sustain=0.6 voice2.amp_decay2=200 voice2.env_velocity=0.5 "
            "voice3.table=3 voice3.lfo1_rate=1.3 voice3.lfo1_shape=1 voice3.mod1_src=1 voice3.mod1_dst=4 voice3.mod1_amt=0.8 "
            "voice3.lfo2_sync=5 voice3.lfo2_retrig=1 voice3.mod2_src=2 voice3.mod2_dst=9 voice3.mod2_amt=-0.5";
        const std::vector<float> a = render(busy, 512, 2.5, true), b = render(busy, 37, 2.5, true);
        size_t first = 0;
        while (first < a.size() && first < b.size() && std::memcmp(&a[first], &b[first], sizeof(float)) == 0) ++first;
        check(a.size() == b.size() && first == a.size() && rms(a, 0.1, 2.0) > 1e-3, "modulated voices are the same for any block size",
              first < a.size() ? fmt("first difference at sample %zu", first) : fmt("rms %.4f", rms(a, 0.1, 2.0)));
    }
}

/**
 * The other synths' envelopes and modulation (26.09.2026): the pad synth's keys each with their own matrix (a square LFO
 * on the pan swings a chord from side to side), the tape keys' and the strings' sustain levels, and the same output for
 * any host block size with every destination moving.
 */
void testSynthModulation()
{
    section("envelopes and modulation of the pad synth, the tape keys, the strings");
    auto render = [](Part part, const char* text, int block, double seconds) {
        Score one;
        one.clear(120.0);
        for (int p : { 57, 64, 69 }) one.notes.push_back({ 0.0, 5.0, part, p, 0.8f, false, false });
        one.lengthBeats = 6.0;
        Engine e;
        e.params().parseText(std::string("master.level=0 master.motion=0 poly.echo=0 poly.reverb=0 poly.chorus=0 poly.attack=0.01 "
                                         "poly.blend=0 poly.early=0 tape.echo=0 tape.reverb=0 tape.blend=0 tape.early=0 "
                                         "strings.echo=0 strings.reverb=0 strings.blend=0 strings.early=0 strings.attack=0.01 ") + text);
        e.prepare(48000.0, block);
        e.load(one);
        std::vector<float> L, R, l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
        const int total = static_cast<int>(48000.0 * seconds);
        for (int done = 0; done < total; done += block) {
            const int n = std::min(block, total - done);
            e.process(l.data(), r.data(), n);
            L.insert(L.end(), l.begin(), l.begin() + n);
            R.insert(R.end(), r.begin(), r.begin() + n);
        }
        L.insert(L.end(), R.begin(), R.end());
        return L;
    };
    auto rms = [](const std::vector<float>& v, size_t offset, double from, double to) {
        double e = 0.0;
        const size_t a = offset + static_cast<size_t>(from * 48000.0), b = offset + static_cast<size_t>(to * 48000.0);
        for (size_t i = a; i < b; ++i) e += static_cast<double>(v[i]) * v[i];
        return std::sqrt(e / static_cast<double>(b - a));
    };
    {
        // A square of 2 Hz on the pan, restarted by the keys: the chord right in the first quarter second, left in the next.
        const double seconds = 1.2;
        const std::vector<float> v = render(Part::Pad, "poly.lfo1_rate=2 poly.lfo1_shape=4 poly.lfo1_retrig=1 poly.mod1_src=1 poly.mod1_dst=10 poly.mod1_amt=1", 256, seconds);
        const size_t half = v.size() / 2;
        const double rFirst = rms(v, half, 0.05, 0.22), lFirst = rms(v, 0, 0.05, 0.22), rSecond = rms(v, half, 0.3, 0.47), lSecond = rms(v, 0, 0.3, 0.47);
        check(rFirst > 3.0 * lFirst && lSecond > 3.0 * rSecond, "a square LFO on the pad's pan swings its keys from side to side",
              fmt("L/R %.4f/%.4f, then %.4f/%.4f", lFirst, rFirst, lSecond, rSecond));
    }
    {
        // The sustain levels: a held chord settles a good way below the same chord without one.
        auto settle = [&](Part part, const char* text) {
            const std::vector<float> v = render(part, text, 256, 2.0);
            return rms(v, 0, 1.5, 1.9);
        };
        const double tape = settle(Part::TapeKeys, "tape.amp_sustain=0.2 tape.amp_decay=150"), tapeFull = settle(Part::TapeKeys, "");
        const double str = settle(Part::Strings, "strings.amp_sustain=0.2 strings.amp_decay=0.2"), strFull = settle(Part::Strings, "");
        const double pad = settle(Part::Pad, "poly.amp_sustain=0.2 poly.amp_decay=0.2"), padFull = settle(Part::Pad, "");
        check(tape < 0.45 * tapeFull && str < 0.45 * strFull && pad < 0.45 * padFull, "the tape keys, the strings and the pad settle on their sustain levels",
              fmt("held rms with sustain 0.2 against without: tape %.4f/%.4f, strings %.4f/%.4f, pad %.4f/%.4f", tape, tapeFull, str, strFull, pad, padFull));
    }
    {
        // Every destination moving on all three: blocks of 37 equal blocks of 512.
        const char* busy[3] = {
            "poly.lfo1_rate=1.7 poly.lfo1_shape=6 poly.mod1_src=1 poly.mod1_dst=4 poly.mod1_amt=0.6 poly.lfo2_sync=7 poly.mod2_src=2 poly.mod2_dst=1 poly.mod2_amt=0.2 "
            "poly.mod3_src=5 poly.mod3_dst=5 poly.mod3_amt=0.5 poly.mod4_src=6 poly.mod4_dst=6 poly.mod4_amt=0.4 poly.mod5_src=7 poly.mod5_dst=7 poly.mod5_amt=0.5 "
            "poly.lfo3_retrig=1 poly.lfo3_fade=0.4 poly.lfo3_rate=3 poly.mod6_src=3 poly.mod6_dst=9 poly.mod6_amt=-0.5 poly.mod7_src=4 poly.mod7_dst=10 poly.mod7_amt=0.8 "
            "poly.filt_link=0 poly.env_velocity=0.5 poly.amp_sustain=0.6",
            "tape.lfo1_rate=4.5 tape.mod1_src=1 tape.mod1_dst=1 tape.mod1_amt=0.1 tape.lfo2_shape=5 tape.lfo2_rate=2 tape.mod2_src=2 tape.mod2_dst=2 tape.mod2_amt=0.5 "
            "tape.mod3_src=1 tape.mod3_dst=3 tape.mod3_amt=0.4 tape.mod4_src=2 tape.mod4_dst=4 tape.mod4_amt=0.7 tape.amp_attack=300 tape.amp_sustain=0.5 tape.amp_release=200",
            "strings.lfo1_rate=5.5 strings.lfo1_fade=0.5 strings.lfo1_retrig=1 strings.mod1_src=1 strings.mod1_dst=1 strings.mod1_amt=0.1 strings.lfo2_sync=6 "
            "strings.mod2_src=2 strings.mod2_dst=2 strings.mod2_amt=0.5 strings.mod3_src=1 strings.mod3_dst=3 strings.mod3_amt=0.3 strings.mod4_src=2 strings.mod4_dst=4 "
            "strings.mod4_amt=0.6 strings.amp_sustain=0.5",
        };
        const Part parts[3] = { Part::Pad, Part::TapeKeys, Part::Strings };
        std::string info;
        bool same = true;
        for (int i = 0; i < 3; ++i) {
            const std::vector<float> a = render(parts[i], busy[i], 512, 2.2), b = render(parts[i], busy[i], 37, 2.2);
            same = same && a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0 && rms(a, 0, 0.2, 2.0) > 1e-3;
            info += fmt(" %.4f", rms(a, 0, 0.2, 2.0));
        }
        check(same, "the three synths' modulation is the same for any block size", "rms" + info);
    }
    {
        // The presets use the modulation generously (the user, 26.09.2026: "ordentlich Gebrauch"): nearly all carry some,
        // many two or three, on many targets -- and the foundation (the bass groups, the drone) never moves its pitch.
        struct Use { double share = 0.0, slots = 0.0; int targets = 0; bool pitchOnGround = false; };
        auto use = [](Module m, int firstSlot, int slots) {
            Use u;
            std::vector<int> targets;
            int with = 0, total = 0;
            ParamStore store;
            for (const SoundPreset& pr : factoryPresets(m)) {
                int n = 0;
                for (int k = 0; k < slots; ++k) {
                    float src = 0.0f, dst = 0.0f;
                    for (const auto& v : pr.values) {
                        if (v.first == firstSlot + 3 * k) src = v.second;
                        if (v.first == firstSlot + 3 * k + 1) dst = v.second;
                    }
                    if (src == 0.0f || dst == 0.0f) continue;
                    ++n;
                    if (std::find(targets.begin(), targets.end(), static_cast<int>(dst)) == targets.end()) targets.push_back(static_cast<int>(dst));
                    const bool ground = m == Module::Drone || pr.group == "Ladder Bass" || pr.group == "Deep Ostinato" || pr.group == "Dark Throb" || pr.group == "Warm Unison";
                    if (ground && m != Module::Tape && m != Module::Strings && static_cast<int>(dst) == 1) u.pitchOnGround = true;
                }
                with += n > 0;
                total += n;
            }
            const double count = static_cast<double>(factoryPresets(m).size());
            u.share = with / count;
            u.slots = total / count;
            u.targets = static_cast<int>(targets.size());
            return u;
        };
        const Use v = use(Module::Voice, voice::Mod1Src, 8), l = use(Module::Lead, lead::Mod1Src, 8), d = use(Module::Drone, lead::Mod1Src, 8);
        const Use pd = use(Module::Poly, poly::Mod1Src, 8), t = use(Module::Tape, tape::Mod1Src, 4), st = use(Module::Strings, strings::Mod1Src, 4);
        bool ok = v.targets >= 9 && pd.targets >= 6 && !v.pitchOnGround && !d.pitchOnGround && v.slots > 1.3;
        for (const Use* u : { &v, &l, &d, &pd }) ok = ok && u->share > 0.75;
        for (const Use* u : { &t, &st }) ok = ok && u->share > 0.6;
        check(ok, "the presets use the modulation generously, on many targets, and leave the foundation's pitch",
              fmt("in %.0f%% of the voices' presets (%.1f slots, %d targets), %.0f%% lead, %.0f%% drone, %.0f%% pad (%d targets), %.0f%% tape, %.0f%% strings",
                  100 * v.share, v.slots, v.targets, 100 * l.share, 100 * d.share, 100 * pd.share, pd.targets, 100 * t.share, 100 * st.share));
    }
}

/**
 * The classic VCOs (26.09.2026, Vco.h): the ideal model is the voices' own PolyBLEP saw; a hard-synced oscillator keeps
 * its aliasing far below a naive restart's; and voices on the models, synced and cross-modulated, are the same for any
 * block size.
 */
void testVcos()
{
    section("classic VCOs, hard sync and cross mod");
    {
        // The ideal model against the lanes' saw, on a step whose phases are exact in float and double.
        VcoOsc o;
        float ph = 0.0f, maxDiff = 0.0f;
        const float dt = 3.0f / 256.0f;
        for (int n = 0; n < 4000; ++n) {
            const float a = laneVco<float>(ph, dt, 1.0f / dt, 0.0f, 0.5f, false);
            const float b = o.step(dt, vcoProfile(0), 0.0f, 0.5f, -1.0, nullptr);
            if (n > 0) maxDiff = std::max(maxDiff, std::fabs(a - b));   // the lanes take phase 0 as just after a wrap
        }
        check(maxDiff < 1e-5f, "the ideal model is the voices' own saw", fmt("max difference %.2g", static_cast<double>(maxDiff)));
    }
    {
        // Hard sync, 1234.5 Hz at 96 kHz, VCO 2 at 2.37 times it: the energy between the master's harmonics under 20 kHz
        // (the aliases the decimator leaves), band-limited against the naive restart.
        constexpr int N = 1 << 16;
        const double fs = 96000.0, f0 = 1234.5, dt1 = f0 / fs, dt2 = 2.37 * dt1;
        std::vector<float> a(N), b(N);
        VcoOsc m, sl;
        double pm = 0.0, ps = 0.0;
        for (int n = 0; n < N; ++n) {
            double wrap = -1.0;
            m.step(dt1, vcoProfile(0), 0.0f, 0.5f, -1.0, &wrap);
            a[static_cast<size_t>(n)] = sl.step(dt2, vcoProfile(0), 0.0f, 0.5f, wrap, nullptr);
            b[static_cast<size_t>(n)] = static_cast<float>(2.0 * ps - 1.0);
            pm += dt1;
            ps += dt2;
            if (pm >= 1.0) { pm -= 1.0; ps = pm * dt2 / dt1; }
            if (ps >= 1.0) ps -= 1.0;
        }
        auto aliasDb = [&](const std::vector<float>& x) {
            std::vector<float> re(N), im(N, 0.0f);
            for (int n = 0; n < N; ++n) {
                const double w = 2.0 * 3.14159265358979 * n / N;
                const double win = 0.35875 - 0.48829 * std::cos(w) + 0.14128 * std::cos(2 * w) - 0.01168 * std::cos(3 * w);
                re[static_cast<size_t>(n)] = static_cast<float>(x[static_cast<size_t>(n)] * win);
            }
            Fft fft(N);
            fft.transform(re.data(), im.data(), false);
            double harm = 0.0, alias = 0.0;
            for (int k = 20; k < static_cast<int>(20000.0 / fs * N); ++k) {   // the audible band: the decimator takes the rest
                const double h = k * fs / N / f0;
                const double off = std::fabs(h - std::round(h)) * f0 * N / fs;   // bins from the nearest harmonic
                const double e = static_cast<double>(re[static_cast<size_t>(k)]) * re[static_cast<size_t>(k)] + static_cast<double>(im[static_cast<size_t>(k)]) * im[static_cast<size_t>(k)];
                (off <= 6.0 ? harm : alias) += e;
            }
            return 10.0 * std::log10(alias / harm);
        };
        const double band = aliasDb(a), naive = aliasDb(b);
        check(band < -35.0 && band < naive - 12.0, "hard sync keeps its aliases far down", fmt("%.1f dB between the harmonics, %.1f dB naive", band, naive));
    }
    {
        // Voices on the models, one synced with its filter envelope sweeping VCO 2 (the Prophet's Poly-Mod), one
        // cross-modulated with the pulse in: blocks of 37 equal blocks of 512.
        auto render = [](int block) {
            Score one;
            one.clear(120.0);
            one.notes.push_back({ 0.0, 3.0, Part::Row2, 45, 0.9f, false, false });
            for (int k = 0; k < 8; ++k) one.notes.push_back({ 0.5 * k, 0.3, Part::Row3, 57 + k, 0.7f, k == 3, false });
            one.lengthBeats = 5.0;
            Engine e;
            e.params().parseText("master.level=0 master.motion=0 row2.echo=0 row2.reverb=0 row3.echo=0 row3.reverb=0 "
                                 "voice2.vco=3 voice2.sync=1 voice2.osc2_pitch=7 voice2.mod1_src=6 voice2.mod1_dst=11 voice2.mod1_amt=0.4 "
                                 "voice3.vco=1 voice3.cross_mod=0.3 voice3.wave=0.5 voice3.pw=0.3 voice3.lfo1_rate=3 voice3.mod1_src=1 "
                                 "voice3.mod1_dst=2 voice3.mod1_amt=0.3");
            e.prepare(48000.0, block);
            e.load(one);
            std::vector<float> out, l(static_cast<size_t>(block)), r(static_cast<size_t>(block));
            for (int done = 0; done < 48000 * 2; done += block) {
                const int n = std::min(block, 48000 * 2 - done);
                e.process(l.data(), r.data(), n);
                out.insert(out.end(), l.begin(), l.begin() + n);
            }
            return out;
        };
        const std::vector<float> x = render(512), y = render(37);
        double e = 0.0;
        for (float v : x) e += static_cast<double>(v) * v;
        check(x.size() == y.size() && std::memcmp(x.data(), y.data(), x.size() * sizeof(float)) == 0 && e > 1.0,
              "voices on the VCO models are the same for any block size", fmt("energy %.1f", e));
    }
    {
        // The presets carry the models of their instruments: the voices' every model, sync sweeps on the leads, the
        // pad synth's analog groups on the models' waves.
        std::vector<int> models;
        for (const SoundPreset& pr : factoryPresets(Module::Voice))
            for (const auto& v : pr.values)
                if (v.first == voice::Vco && std::find(models.begin(), models.end(), static_cast<int>(v.second)) == models.end()) models.push_back(static_cast<int>(v.second));
        int synced = 0, waves = 0;
        for (const SoundPreset& pr : factoryPresets(Module::Lead)) for (const auto& v : pr.values) synced += v.first == lead::Sync && v.second > 0.5f;
        for (const SoundPreset& pr : factoryPresets(Module::Poly))
            for (const auto& v : pr.values) waves += v.first == poly::Table && v.second >= static_cast<float>(kFormulaTableCount + kSampledTableCount);
        check(models.size() == static_cast<size_t>(kVcoModels) && synced > 20 && waves > 20, "the presets carry the VCOs of their instruments",
              fmt("%zu models in the voices' presets, %d synced leads, %d pads on the models' waves", models.size(), synced, waves));
    }
}

/**
 * The loudness per piece (26.09.2026, Leveler.h): a jump finds the notes sounding on across it again; the leveler
 * brings a piece's loudest part to its style's target, within a dB, the same every time, and the engine plays the
 * correction from the piece's start.
 */
void testLeveler()
{
    section("every piece as loud as its style means");
    {
        // A drone held from the start: a jump into its middle hears it at once.
        Score held;
        held.clear(120.0);
        held.notes.push_back({ 0.0, 64.0, Part::Drone, 45, 0.9f, false, false });
        held.lengthBeats = 64.0;
        Engine e;
        e.params().parseText("master.motion=0");
        e.prepare(48000.0, 512);
        e.load(held);
        e.seek(32.0);
        std::vector<float> l(512), r(512);
        double en = 0.0;
        for (int b = 0; b < 94; ++b) {
            e.process(l.data(), r.data(), 512);
            if (b >= 20) for (float x : l) en += static_cast<double>(x) * x;
        }
        check(en > 1e-3, "a jump finds the notes that sound on across it", fmt("energy %.4f after the jump", en));
    }
    ParamStore p;
    p.parseText("compose.style=Melodic");
    Score a = composePiece(p, 21, 4.0), b = a;
    const std::vector<LevelReading> ra = levelScore(a, p), rb = levelScore(b, p);
    bool ok = ra.size() == 1 && rb.size() == 1, same = ok;
    std::string info;
    for (size_t i = 0; ok && i < ra.size(); ++i) {
        ok = ok && std::fabs(ra[i].after - ra[i].target) < 1.0f && std::fabs(ra[i].trim) <= 4.0f;
        same = same && ra[i].trim == rb[i].trim;
        info += fmt("loudest part %.1f LUFS, target %.1f, correction %+.1f dB, then %.1f", static_cast<double>(ra[i].measured),
                    static_cast<double>(ra[i].target), static_cast<double>(ra[i].trim), static_cast<double>(ra[i].after));
    }
    check(ok && same && a.levels[0].trimDb == ra[0].trim, "the loudest part comes to its style's target, the same every time", info);
    // The engine plays the correction: the same piece with and without, the same place, apart by the correction. And
    // one that comes late (the plugin measures while the piece plays) glides in: at first the level as it was.
    auto loud = [](const Score& sc, const ParamStore& q, const std::vector<float>* late, double* first) {
        Engine e;
        e.params().copyValuesFrom(q);
        e.prepare(48000.0, 512);
        e.load(sc);
        e.seek(sc.levels[0].peakBeat);
        if (late != nullptr) e.setLevelTrims(*late);
        std::vector<float> l(512), r(512);
        LoudnessMeter m;
        m.prepare(48000.0);
        double en = 0.0;
        for (int k = 0; k < 900; ++k) {
            e.process(l.data(), r.data(), 512);
            if (k < 10) for (float x : l) en += static_cast<double>(x) * x;
            if (k >= 300) m.process(l.data(), r.data(), 512);
        }
        *first = 10.0 * std::log10(en + 1e-30);
        return m.report().integrated;
    };
    Score plain = a;
    plain.levels[0].trimDb = 0.0f;
    const std::vector<float> found = { a.levels[0].trimDb };
    double firstWith = 0.0, firstWithout = 0.0, firstLate = 0.0;
    const double with = loud(a, p, nullptr, &firstWith), without = loud(plain, p, nullptr, &firstWithout),
                 late = loud(plain, p, &found, &firstLate);
    check(std::fabs((with - without) - a.levels[0].trimDb) < 1.0, "the engine plays the correction from the piece's start",
          fmt("%.1f LUFS with it, %.1f without, the correction %+.1f dB", with, without, static_cast<double>(a.levels[0].trimDb)));
    check(std::fabs(firstLate - firstWithout) < 0.5 && std::fabs(late - with) < 0.5,
          "a correction that comes late glides in from the level that played",
          fmt("first 0.1 s %+.2f dB from the level before (with it at once %+.2f), then %.1f LUFS against %.1f",
              firstLate - firstWithout, firstWith - firstWithout, late, with));
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

/** @brief A section of the tests, as main() can run it alone. */
struct TestSection {
    const char* name;   ///< its name, as the command line names it
    std::function<void()> fn;   ///< the section
};

/** The keyboard (01.10.2026, Engine::queueLive): with the composer off nothing generated sounds; a played key sounds on
 *  the voice the keyboard plays, from its sample; without live play the keys and the switch do nothing. */
void testKeyboard()
{
    section("keyboard (live)");
    ParamStore p;
    const Score score = composePiece(p, 21, 8.0);
    auto run = [&](bool live, const char* knobs, bool key) {
        auto e = std::make_unique<Engine>();
        e->params().parseText(knobs);
        e->setLive(live);
        e->prepare(48000.0, 256);
        e->load(score);
        e->seek(96.0);
        std::vector<float> L(256), R(256);
        double sum = 0.0;
        for (int b = 0; b < 48000 * 4 / 256; ++b) {
            if (key && b == 100) e->queueLive(17, 60, 110, 0, true);   // middle C on the lead, 17 samples into the block
            if (key && b == 300) e->queueLive(3, 60, 0, 0, false);
            e->process(L.data(), R.data(), 256);
            if (b >= 100)
                for (int i = 0; i < 256; ++i) sum += static_cast<double>(L[static_cast<size_t>(i)]) * L[static_cast<size_t>(i)]
                                                   + static_cast<double>(R[static_cast<size_t>(i)]) * R[static_cast<size_t>(i)];
        }
        return sum;
    };
    const double composed = run(true, "", false);
    const double silent = run(true, "perform.composer=0", false);
    const double played = run(true, "perform.composer=0 perform.keyboard_part=1", true);
    const double offline = run(false, "perform.composer=0 perform.keyboard_part=1", true);
    const double plainOffline = run(false, "", false);
    check(silent < 1e-3 * composed, "the composer off: nothing generated sounds (the rooms' tails die away)",
          fmt("%.3g of %.3g", silent, composed));
    check(played > 10.0 * std::max(silent, 1e-9), "a played key sounds on the lead", fmt("energy %.3g, silence %.3g", played, silent));
    check(offline == plainOffline, "without live play the keyboard and the composer switch do nothing (renders, exports)",
          fmt("%.6g against %.6g", offline, plainOffline));
}

/** MIDI out (02.10.2026, NoteTap.h): in live play the engine writes the composer notes it plays into the tap, each on its
 *  sample inside the block and transposed as heard; every off goes out with its on's pitch; with the composer off nothing
 *  is written. */
void testNoteTap()
{
    section("MIDI out (note tap)");
    ParamStore p;
    const Score score = composePiece(p, 21, 8.0);
    struct Count { int ons = 0, offs = 0, unpaired = 0; int first[kNumParts]; bool inBlock = true; };
    auto run = [&](const char* knobs) {
        auto e = std::make_unique<Engine>();
        e->params().parseText(knobs);
        e->setLive(true);
        e->prepare(48000.0, 256);
        e->load(score);
        e->seek(96.0);
        auto tap = std::make_unique<NoteTap>();
        e->setNoteTap(tap.get());
        std::vector<float> L(256), R(256);
        Count c;
        std::fill(std::begin(c.first), std::end(c.first), -1);
        int sounding[kNumParts][128] = {};
        for (int b = 0; b < 48000 * 8 / 256; ++b) {
            tap->clear();
            const int64_t s0 = e->samplePosition();
            e->process(L.data(), R.data(), 256);
            for (int i = 0; i < tap->count; ++i) {
                const NoteTap::Note& nt = tap->notes[i];
                if (nt.sample < s0 - 48000 || nt.sample >= s0 + 256) c.inBlock = false;   // a seek's chased notes: the past
                if (nt.velocity == 0) {
                    ++c.offs;
                    if (sounding[nt.part][nt.pitch] > 0) --sounding[nt.part][nt.pitch]; else ++c.unpaired;
                    continue;
                }
                ++c.ons;
                ++sounding[nt.part][nt.pitch];
                if (c.first[nt.part] < 0) c.first[nt.part] = nt.pitch;
            }
        }
        return c;
    };
    const Count plain = run("");
    const Count up = run("perform.transpose=5");
    const Count off = run("perform.composer=0");
    check(plain.ons > 8 && plain.offs > 0, "the composer's notes are tapped, ons and offs", fmt("%d ons, %d offs", plain.ons, plain.offs));
    check(plain.inBlock, "every tapped note lies in the block it was played in (or before it, chased by the seek)");
    check(plain.unpaired == 0 && up.unpaired == 0, "every off has its on's pitch",
          fmt("%d and %d unpaired", plain.unpaired, up.unpaired));
    int parts = 0, shifted = 0;
    for (int k = 0; k < kNumParts; ++k) {
        if (k == static_cast<int>(Part::Drums) || plain.first[k] < 0 || up.first[k] < 0) continue;
        ++parts;
        if (up.first[k] == plain.first[k] + 5) ++shifted;
    }
    check(parts > 0 && shifted == parts, "the notes go out transposed as they are heard (the drums are not)",
          fmt("%d of %d parts five semitones up", shifted, parts));
    check(off.ons == 0, "the composer off: nothing tapped", fmt("%d notes", off.ons));
}

/** The keyboard's split (02.10.2026): a key that names its target plays that voice, whatever Keyboard Plays says --
 *  with Keyboard Plays off a plain key goes nowhere, a key naming the lead sounds. */
void testKeySplit()
{
    section("keyboard split (a key names its voice)");
    ParamStore p;
    const Score score = composePiece(p, 21, 8.0);
    auto run = [&](int target) {
        auto e = std::make_unique<Engine>();
        e->params().parseText("perform.composer=0");
        e->setLive(true);
        e->prepare(48000.0, 256);
        e->load(score);
        e->seek(96.0);
        std::vector<float> L(256), R(256);
        double held = 0.0;
        for (int b = 0; b < 48000 * 3 / 256; ++b) {
            if (b == 50) e->queueLive(5, 60, 110, 0, true, target);
            if (b == 200) e->queueLive(5, 60, 0, 0, false, target);
            e->process(L.data(), R.data(), 256);
            if (b >= 60 && b < 200)
                for (int i = 0; i < 256; ++i) held += static_cast<double>(L[static_cast<size_t>(i)]) * L[static_cast<size_t>(i)];
        }
        return held;
    };
    const double plain = run(-1);
    const double named = run(perform::keys::Lead);
    // The atmosphere is no note: it goes on with the composer off, so "silent" is "nothing above it".
    check(named > 10.0 * std::max(plain, 1e-9), "a key naming its voice plays it; with Keyboard Plays off a plain key goes nowhere",
          fmt("%.3g against %.3g", named, plain));
}

/** @brief Every section, in the order they run. */
const TestSection kSections[] = {
    { "testTempoMap", testTempoMap },
    { "testGestures", testGestures },
    { "testKeyboard", testKeyboard },
    { "testNoteTap", testNoteTap },
    { "testKeySplit", testKeySplit },
    { "testParams", testParams },
    { "testMidiTempo", testMidiTempo },
    { "testRack", testRack },
    { "testTransposer", testTransposer },
    { "testHands", testHands },
    { "testLead", testLead },
    { "testTapeKeys", testTapeKeys },
    { "testChords", testChords },
    { "testForm", testForm },
    { "testConjunctions", testConjunctions },
    { "testMorph", testMorph },
    { "testComposer", testComposer },
    { "testCuration", testCuration },
    { "testPerform", testPerform },
    { "testCues", testCues },
    { "testRooms", testRooms },
    { "testStrings", testStrings },
    { "testHarmony", testHarmony },
    { "testSequencing", testSequencing },
    { "testFormAndSpace", testFormAndSpace },
    { "testGuideExtras", testGuideExtras },
    { "testLoudness", testLoudness },
    { "testMixBus", testMixBus },
    { "testAddon", testAddon },
    { "testPresets", testPresets },
    { "testBlendAndBus", testBlendAndBus },
    { "testSendsAD", testSendsAD },
    { "testModLane", testModLane },
    { "testNightSet", testNightSet },
    { "testVariations", testVariations },
    { "testSounds", testSounds },
    { "testPoly", testPoly },
    { "testRowTables", testRowTables },
    { "testTimbreDrift", testTimbreDrift },
    { "testFilters", testFilters },
    { "testFilterVoices", testFilterVoices },
    { "testModulation", testModulation },
    { "testSynthModulation", testSynthModulation },
    { "testVcos", testVcos },
    { "testLeveler", testLeveler },
    { "testBlockSizes", testBlockSizes },
    { "testEcho", testEcho },
    { "testDrift", testDrift },
};

} // namespace

/** @brief Runs every section, or those the command line names; the exit code is the number of failures. */
int main(int argc, char** argv)
{
    std::string only;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--list") == 0) {
            for (const TestSection& s : kSections) std::printf("%s\n", s.name);
            return 0;
        }
        if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) only = std::string(",") + argv[++i] + ",";
    }
    for (const TestSection& s : kSections)
        if (only.empty() || only.find(std::string(",") + s.name + ",") != std::string::npos) s.fn();
    return finish();
}
