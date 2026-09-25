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
#include "eph/Cue.h"
#include "eph/fx/Plate.h"
#include "eph/synth/Atmos.h"
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
#include "eph/synth/ModVoice.h"
#include "eph/compose/Pads.h"
#include "eph/Params.h"
#include "eph/Rack.h"
#include "eph/Score.h"
#include "eph/SetFile.h"
#include "eph/Study.h"
#include "eph/fx/TapeEcho.h"
#include "eph/synth/TapeKeys.h"
#include "TestSupport.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <set>
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
    base.parseText("compose.style=Melodic");
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
    check(statics[static_cast<int>(Style::Drift)] > statics[static_cast<int>(Style::Melodic)] * 2,
          "Drift stays on one chord far more often than Melodic", fmt("%d against %d of 480", statics[static_cast<int>(Style::Drift)],
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
        p.parseText("compose.style=Modern row1.active=1 row1.length=16 row1.division=1/16 row1.mutation=0");
        int chancy = 0, varied = 0;
        for (int seed = 1; seed <= 20; ++seed) {
            Rack r;
            r.setup(p, static_cast<uint64_t>(seed));
            r.generate(0, RowRole::Counter);
            for (int i = 0; i < 16; ++i) chancy += r.steps(0)[i].chance < 1.0f && r.steps(0)[i].gate ? 1 : 0;
            Score s;
            s.clear(120.0);
            r.run(s, 64.0);
            s.sort();
            int count[16] = {};
            for (const NoteEvent& e : s.notes) ++count[static_cast<int>(std::lround(e.beat * 4.0)) % 16];
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
                drumsOutside += n.beat < 0.45 * sc.lengthBeats - 1e-6 || n.beat > 0.9 * sc.lengthBeats ? 1 : 0;
            }
        drumPieces += drums ? 1 : 0;
        const int echo = p.id(Module::Row, 0, row::EchoSend);
        dryBass += p.fromNormalised(echo, p.toNormalised(echo, p.get(echo)) + sc.gestureOffset(echo, 1.0)) < 0.01f ? 1 : 0;
        const int hall = p.id(Module::Reverb, 0, reverb::Decay);
        double peakEnd = -1.0, atmoMid = -1.0;
        for (size_t m = 0; m < sc.markers.size(); ++m) {
            const double next = m + 1 < sc.markers.size() ? sc.markers[m + 1].beat : sc.lengthBeats;
            if (sc.markers[m].text.rfind("Hoehepunkt", 0) == 0 && peakEnd < 0.0) peakEnd = next - 1.0;
            if (sc.markers[m].text.rfind("Atmo", 0) == 0) atmoMid = next - 1.0;
        }
        if (peakEnd > 0.0 && atmoMid > 0.0 && sc.gestureOffset(hall, peakEnd) < sc.gestureOffset(hall, atmoMid)) ++hallShrinks;
    }
    check(drumPieces > 0 && drumsOutside == 0, "the drums only between 45 and 90 % of the piece", fmt("%d pieces with drums, %d hits outside", drumPieces, drumsOutside));
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
        p.parseText("compose.style=Drift row1.active=1 row1.length=16 row1.division=1/16 row1.mutation=0");
        int randomSteps = 0, varied = 0, outside = 0;
        for (int seed = 1; seed <= 20; ++seed) {
            Rack r;
            r.setup(p, static_cast<uint64_t>(seed));
            r.generate(0, RowRole::Counter);
            std::set<int> pitches[16];
            Score sc;
            sc.clear(120.0);
            r.run(sc, 64.0);
            for (const NoteEvent& e : sc.notes) {
                pitches[static_cast<int>(std::lround(e.beat * 4.0)) % 16].insert(e.pitch);
                outside += inScale(e.pitch, 9, 0) ? 0 : 1;
            }
            for (int i = 0; i < 16; ++i) {
                if (!r.steps(0)[i].random) continue;
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
            return p.fromNormalised(id, p.toNormalised(id, p.get(id)) + sc.gestureOffset(id, 1.0));
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
        for (int done = 0, w = 0; done < 48000 * 21; done += 256) {
            e.process(L.data(), R.data(), 256);
            for (int i = 0; i < 256; ++i) { l += double(L[i]) * L[i]; r += double(R[i]) * R[i]; }
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

struct TestSection {
    const char* name;
    std::function<void()> fn;
};

const TestSection kSections[] = {
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
            for (const TestSection& s : kSections) std::printf("%s\n", s.name);
            return 0;
        }
        if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) only = std::string(",") + argv[++i] + ",";
    }
    for (const TestSection& s : kSections)
        if (only.empty() || only.find(std::string(",") + s.name + ",") != std::string::npos) s.fn();
    return finish();
}
