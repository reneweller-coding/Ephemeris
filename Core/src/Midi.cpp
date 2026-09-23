/**
 * @file Midi.cpp
 * @brief Standard MIDI File writer.
 */
#include "eph/Midi.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace eph {

namespace {

/** @brief One event of a track before it is serialised: absolute tick, order for equal ticks, bytes. */
struct Ev {
    int64_t tick;
    int order;   ///< at equal ticks: meta first, note-offs before note-ons
    std::vector<uint8_t> bytes;
};

int64_t toTick(double beat) { return static_cast<int64_t>(std::llround(beat * kMidiPpq)); }

void putVarLen(std::vector<uint8_t>& out, uint32_t v)
{
    uint8_t buf[5];
    int n = 0;
    buf[n++] = static_cast<uint8_t>(v & 0x7F);
    while ((v >>= 7) != 0) buf[n++] = static_cast<uint8_t>(0x80 | (v & 0x7F));
    while (n > 0) out.push_back(buf[--n]);
}

void put32(std::vector<uint8_t>& out, uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<uint8_t>((v >> s) & 0xFF));
}

Ev meta(int64_t tick, uint8_t type, const std::vector<uint8_t>& data)
{
    Ev e{ tick, 0, { 0xFF, type } };
    putVarLen(e.bytes, static_cast<uint32_t>(data.size()));
    e.bytes.insert(e.bytes.end(), data.begin(), data.end());
    return e;
}

Ev metaText(int64_t tick, uint8_t type, const std::string& s)
{
    return meta(tick, type, std::vector<uint8_t>(s.begin(), s.end()));
}

Ev tempoEvent(int64_t tick, double bpm)
{
    const uint32_t us = static_cast<uint32_t>(std::llround(60.0e6 / bpm));
    return meta(tick, 0x51, { static_cast<uint8_t>(us >> 16), static_cast<uint8_t>(us >> 8), static_cast<uint8_t>(us) });
}

/** @brief Sharps (+) or flats (-) of the minor key on @p root (A minor = 0). */
int minorKeySharps(int root)
{
    // Relative major is three semitones up; the circle of fifths from C major.
    static const int kMajorSharps[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };
    return kMajorSharps[((root % 12) + 12 + 3) % 12];
}

void appendTrack(std::vector<uint8_t>& file, std::vector<Ev>& evs)
{
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
    });
    std::vector<uint8_t> body;
    int64_t last = 0;
    for (const Ev& e : evs) {
        putVarLen(body, static_cast<uint32_t>(e.tick - last));
        body.insert(body.end(), e.bytes.begin(), e.bytes.end());
        last = e.tick;
    }
    body.insert(body.end(), { 0x00, 0xFF, 0x2F, 0x00 });
    file.insert(file.end(), { 'M', 'T', 'r', 'k' });
    put32(file, static_cast<uint32_t>(body.size()));
    file.insert(file.end(), body.begin(), body.end());
}

/** @brief Where a knob's gestures go in the file: a part (or none: the controls track) and a CC. */
bool ccFor(const ParamStore& p, int id, int& part, int& cc)
{
    const std::string& key = p.key(id);
    const size_t dot = key.find('.');
    const std::string mod = key.substr(0, dot), name = key.substr(dot + 1);
    auto voiceCc = [&]() { return name == "cutoff" ? 74 : name == "resonance" ? 71 : name == "decay" ? 75 : -1; };
    if (mod.rfind("voice", 0) == 0) { part = static_cast<int>(Part::Row1) + std::atoi(mod.c_str() + 5) - 1; cc = voiceCc(); return cc >= 0; }
    if (mod == "lead") { part = static_cast<int>(Part::Lead); cc = voiceCc(); return cc >= 0; }
    if (mod == "drone") { part = static_cast<int>(Part::Drone); cc = voiceCc(); return cc >= 0; }
    static const struct { const char* key; int cc; } kControls[] = {
        { "echo.feedback", 12 }, { "echo.time", 13 }, { "reverb.decay", 14 }, { "tape.set", 15 },
        { "atmos.wind", 20 }, { "atmos.wind_tone", 21 }, { "atmos.sweeps", 22 }, { "atmos.bleeps", 23 },
        { "spring.return", 24 }, { "strings.ensemble", 25 }, { "master.level", 7 },
    };
    for (const auto& c : kControls) if (key == c.key) { part = -1; cc = c.cc; return true; }
    return false;
}

} // namespace

int midiChannelOf(Part part)
{
    if (part == Part::Drums) return 9;
    const int c = static_cast<int>(part) % 15;
    return c >= 9 ? c + 1 : c;   // every part but the drums keeps off channel 10
}

std::vector<uint8_t> encodeMidi(const Score& score, const char* title, const ParamStore* params)
{
    // The gestures as controller events, per part (index kNumParts: the controls track).
    std::vector<std::vector<Ev>> cc(kNumParts + 1);
    if (params != nullptr) {
        // Per knob, in time order, the value written last, so an unchanged value is not repeated.
        std::vector<int> last(static_cast<size_t>(params->count()), -1);
        for (const Gesture& g : score.gestures) {
            int part = -1, number = -1;
            if (g.param < 0 || g.param >= params->count() || !ccFor(*params, g.param, part, number)) continue;
            const size_t track = part < 0 ? static_cast<size_t>(kNumParts) : static_cast<size_t>(part);
            const uint8_t ch = static_cast<uint8_t>(part < 0 ? 15 : midiChannelOf(static_cast<Part>(part)));
            const float knob = params->toNormalised(g.param, params->get(g.param));
            const double step = 0.125;   // a thirty-second note
            const double end = g.beat + std::max(0.0, g.length);
            for (double b = g.beat; ; b += step) {
                const double at = std::min(b, end);
                const float v = std::clamp(knob + gestureValue(g, at + (g.length <= 0.0 ? 1.0 : 0.0)), 0.0f, 1.0f);
                const int value = static_cast<int>(std::lround(v * 127.0f));
                if (value != last[static_cast<size_t>(g.param)]) {
                    cc[track].push_back(Ev{ toTick(at), 1, { static_cast<uint8_t>(0xB0 | ch), static_cast<uint8_t>(number), static_cast<uint8_t>(value) } });
                    last[static_cast<size_t>(g.param)] = value;
                }
                if (at >= end) break;
            }
        }
    }
    // Which parts have notes or controllers: one track each, in part order.
    bool used[kNumParts] = {};
    for (const NoteEvent& n : score.notes) used[static_cast<int>(n.part)] = true;
    for (int i = 0; i < kNumParts; ++i) used[i] = used[i] || !cc[static_cast<size_t>(i)].empty();
    const bool controls = !cc[kNumParts].empty();
    int tracks = 1 + (controls ? 1 : 0);
    for (bool u : used) tracks += u ? 1 : 0;

    std::vector<uint8_t> file = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1 };
    file.push_back(static_cast<uint8_t>(tracks >> 8));
    file.push_back(static_cast<uint8_t>(tracks & 0xFF));
    file.push_back(static_cast<uint8_t>(kMidiPpq >> 8));
    file.push_back(static_cast<uint8_t>(kMidiPpq & 0xFF));

    // Conductor track.
    std::vector<Ev> cond;
    cond.push_back(metaText(0, 0x03, title != nullptr ? title : "Ephemeris"));
    cond.push_back(meta(0, 0x58, { 4, 2, 24, 8 }));
    cond.push_back(meta(0, 0x59, { static_cast<uint8_t>(static_cast<int8_t>(minorKeySharps(score.keyRoot))), 1 }));
    const std::vector<TempoPoint>& pts = score.tempo.points();
    const double end = std::max(score.lengthBeats, pts.back().beat);
    for (size_t i = 0; i < pts.size(); ++i) {
        const TempoPoint& p = pts[i];
        if (p.rampToNext && i + 1 < pts.size()) {
            // One event per beat of the ramp, each exact for the beat it starts.
            const double b1 = pts[i + 1].beat;
            for (double b = p.beat; b < b1; b += 1.0) {
                const double bn = std::min(b + 1.0, b1);
                const double sec = score.tempo.secondsAt(bn) - score.tempo.secondsAt(b);
                cond.push_back(tempoEvent(toTick(b), 60.0 * (bn - b) / sec));
            }
        } else {
            cond.push_back(tempoEvent(toTick(p.beat), p.bpm));
        }
    }
    for (const Marker& m : score.markers) cond.push_back(metaText(toTick(m.beat), 0x06, m.text));
    Ev eot = meta(toTick(end), 0x01, {});   // an empty text event holds the conductor track to the end
    cond.push_back(eot);
    appendTrack(file, cond);

    // One track per part with notes.
    for (int pi = 0; pi < kNumParts; ++pi) {
        if (!used[pi]) continue;
        const Part part = static_cast<Part>(pi);
        const uint8_t ch = static_cast<uint8_t>(midiChannelOf(part));
        std::vector<Ev> evs;
        evs.push_back(metaText(0, 0x03, kPartNames[pi]));
        evs.insert(evs.end(), cc[static_cast<size_t>(pi)].begin(), cc[static_cast<size_t>(pi)].end());
        for (const NoteEvent& n : score.notes) {
            if (n.part != part) continue;
            const int pitch = std::clamp(n.pitch, 0, 127);
            const int vel = std::clamp(static_cast<int>(std::lround(n.velocity * 127.0f)), 1, 127);
            const int64_t on = toTick(n.beat);
            const int64_t off = std::max(on + 1, toTick(n.beat + n.length));
            evs.push_back(Ev{ on, 2, { static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(pitch), static_cast<uint8_t>(vel) } });
            evs.push_back(Ev{ off, 1, { static_cast<uint8_t>(0x80 | ch), static_cast<uint8_t>(pitch), 0 } });
        }
        appendTrack(file, evs);
    }
    if (controls) {
        std::vector<Ev> evs = cc[kNumParts];
        evs.push_back(metaText(0, 0x03, "controls"));
        appendTrack(file, evs);
    }
    return file;
}

bool writeMidiFile(const Score& score, const char* path, const char* title, const ParamStore* params)
{
    const std::vector<uint8_t> bytes = encodeMidi(score, title, params);
    FILE* f = std::fopen(path, "wb");
    if (f == nullptr) return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    return std::fclose(f) == 0 && ok;
}

} // namespace eph
