/**
 * @file main.cpp
 * @brief eph_render: offline render of a score to WAV, with MIDI export.
 *
 * The offline render is the determinism oracle (PLAN 1): same arguments, same samples.
 * Since Phase 4 it plays a piece written by the composer (Composer.h), or a concert with `--concert`;
 * `--sketch` plays the sketch of Phases 2 and 3 (Sketch.h), `--study` the study of Phase 1 (Study.h),
 * and `--frame` renders the bare frame of Phase 0 -- silence on the tempo map.
 *
 * Usage:
 *   eph_render [--minutes M | --bars N] [--bpm B] [--seed S] [--set "k=v ..."] [--tail S]
 *              [--rate 48000] [--block 512] [--out file.wav] [--midi file.mid] [--study]
 *              [--frame [--ramp-to B]]
 *              [--list] [--version]
 */
#include "eph/Engine.h"
#include "eph/Composer.h"
#include "eph/Midi.h"
#include "eph/SetFile.h"
#include "eph/Sketch.h"
#include "eph/Study.h"
#include "eph/WavWriter.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace eph;

namespace {

void usage()
{
    std::printf("eph_render %s\n"
                "  --bars N         length in bars (default: compose.piece_minutes)\n"
                "  --minutes M      length in minutes\n"
                "  --bpm B          tempo (sets compose.bpm)\n"
                "  --seed S         seed of everything drawn (default 1)\n"
                "  --tail S         seconds rendered after the end, for the echo (default 6)\n"
                "  --concert M      a concert of pieces, M minutes long (default: one piece)\n"
                "  --reroll UNIT    draw a unit again (form, tempo, rows, rack, layers, lead, pads, hands;\n"
                "                   in a concert pieceN.UNIT or concert); may be repeated\n"
                "  --set-file FILE  play a saved .ephset (seed, lengths, parameters, rerolls)\n"
                "  --save-set FILE  save what is played as an .ephset\n"
                "  --sketch         the sketch of Phases 2 and 3 instead of a composed piece\n"
                "  --study          the study of Phase 1 instead of a composed piece\n"
                "  --frame          the bare frame of Phase 0: silence on the tempo map\n"
                "  --ramp-to B      with --frame: ramp the tempo linearly over the whole length to B\n"
                "  --set \"k=v ...\"  parameter assignments\n"
                "  --rate R         sample rate (default 48000)\n"
                "  --block N        block size (default 512)\n"
                "  --out FILE       write a 24-bit WAV\n"
                "  --midi FILE      write a Standard MIDI File\n"
                "  --list           print every parameter and exit\n"
                "  --version        print the version and exit\n", EPH_VERSION);
}

} // namespace

int main(int argc, char** argv)
{
    double bars = 0.0, minutes = 0.0, rampTo = 0.0, rate = 48000.0;
    int block = 512;
    std::string out, midi, set;
    bool list = false, frame = false, study = false, sketch = false;
    double concert = 0.0;
    std::string setIn, setOut;
    Curation curation;
    double bpmArg = 0.0, tail = 6.0;
    uint64_t seed = 1;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", what); std::exit(2); }
            return argv[++i];
        };
        if (a == "--bars") bars = std::atof(next("--bars"));
        else if (a == "--minutes") minutes = std::atof(next("--minutes"));
        else if (a == "--bpm") bpmArg = std::atof(next("--bpm"));
        else if (a == "--ramp-to") rampTo = std::atof(next("--ramp-to"));
        else if (a == "--set") set += std::string(next("--set")) + "\n";
        else if (a == "--rate") rate = std::atof(next("--rate"));
        else if (a == "--block") block = std::atoi(next("--block"));
        else if (a == "--out") out = next("--out");
        else if (a == "--midi") midi = next("--midi");
        else if (a == "--list") list = true;
        else if (a == "--frame") frame = true;
        else if (a == "--study") study = true;
        else if (a == "--sketch") sketch = true;
        else if (a == "--concert") concert = std::atof(next("--concert"));
        else if (a == "--reroll") curation.reroll(next("--reroll"));
        else if (a == "--set-file") setIn = next("--set-file");
        else if (a == "--save-set") setOut = next("--save-set");
        else if (a == "--seed") seed = std::strtoull(next("--seed"), nullptr, 10);
        else if (a == "--tail") tail = std::atof(next("--tail"));
        else if (a == "--version") { std::printf("%s\n", EPH_VERSION); return 0; }
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); usage(); return 2; }
    }

    Engine engine;
    ParamStore& p = engine.params();
    if (!setIn.empty()) {
        // A saved set: its seed, lengths, parameters and rerolls; the command line's rerolls come on top.
        SetFile sf;
        std::string err;
        if (!loadSet(setIn.c_str(), sf, p, &err)) { std::fprintf(stderr, "--set-file: %s\n", err.c_str()); return 2; }
        seed = sf.seed;
        if (minutes <= 0.0) minutes = sf.minutes;
        if (concert <= 0.0) concert = sf.concert;
        for (const auto& r : curation.rerolls) sf.curation.rerolls[r.first] += r.second;
        curation = sf.curation;
    }
    if (!set.empty()) {
        std::string err;
        if (!p.parseText(set, &err)) { std::fprintf(stderr, "--set: %s\n", err.c_str()); return 2; }
    }
    const int bpmId = p.id(Module::Compose, 0, compose::Bpm);
    if (bpmArg > 0.0) p.set(bpmId, static_cast<float>(bpmArg));
    if (list) {
        for (int id = 0; id < p.count(); ++id) std::printf("%-24s %s\n", p.key(id).c_str(), p.format(id).c_str());
        return 0;
    }

    const double bpm = p.get(bpmId);
    Score score;
    if (!frame) {
        const double mins = minutes > 0.0 ? minutes : (bars > 0.0 ? bars * kBeatsPerBar / bpm
                                                                  : p.get(p.id(Module::Compose, 0, compose::PieceMinutes)));
        if (concert <= 0.0) concert = p.get(p.id(Module::Compose, 0, compose::ConcertMinutes));
        if (study) score = buildStudy(p, seed, mins);
        else if (sketch) score = buildSketch(p, seed, mins);
        else if (concert > 0.0) score = composeConcert(p, seed, concert, &curation);
        else score = composePiece(p, seed, mins, 0, &curation);
        if (!setOut.empty()) {
            SetFile sf;
            sf.seed = seed;
            sf.minutes = mins;
            sf.concert = concert;
            sf.curation = curation;
            if (!saveSet(setOut.c_str(), sf, p)) { std::fprintf(stderr, "cannot write %s\n", setOut.c_str()); return 1; }
        }
        bars = score.lengthBeats / kBeatsPerBar;
    } else {
        // The frame of Phase 0: a tempo map, a length and two markers.
        score.clear(bpm);
        score.keyRoot = p.getInt(p.id(Module::Compose, 0, compose::Key));
        if (bars <= 0.0) {
            const double mins = minutes > 0.0 ? minutes : p.get(p.id(Module::Compose, 0, compose::PieceMinutes));
            // Bars from minutes at the mean tempo, so a ramp does not change the length much.
            const double meanBpm = rampTo > 0.0 ? 0.5 * (bpm + rampTo) : bpm;
            bars = std::max(1.0, std::floor(mins * meanBpm / kBeatsPerBar + 0.5));
        }
        score.lengthBeats = bars * kBeatsPerBar;
        if (rampTo > 0.0) {
            score.tempo.add(0.0, bpm, true);
            score.tempo.add(score.lengthBeats, rampTo, false);
        }
        score.markers.push_back({ 0.0, "Start" });
        score.markers.push_back({ score.lengthBeats, "End" });
    }
    engine.prepare(rate, block);
    engine.load(score);

    WavWriter wav;
    if (!out.empty() && !wav.open(out.c_str(), static_cast<int>(rate), 2, WavFormat::Pcm24)) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    const auto t0 = std::chrono::steady_clock::now();
    const int64_t total = static_cast<int64_t>(std::llround((engine.lengthSeconds() + tail) * rate));
    std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
    double peak = 0.0, sumSq = 0.0;
    for (int64_t done = 0; done < total;) {
        const int n = static_cast<int>(std::min<int64_t>(block, total - done));
        engine.process(L.data(), R.data(), n);
        for (int i = 0; i < n; ++i) {
            peak = std::max(peak, static_cast<double>(std::max(std::fabs(L[static_cast<size_t>(i)]), std::fabs(R[static_cast<size_t>(i)]))));
            sumSq += 0.5 * (static_cast<double>(L[static_cast<size_t>(i)]) * L[static_cast<size_t>(i)] + static_cast<double>(R[static_cast<size_t>(i)]) * R[static_cast<size_t>(i)]);
        }
        if (!out.empty()) wav.write(L.data(), R.data(), n);
        done += n;
    }
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!out.empty() && !wav.close()) { std::fprintf(stderr, "error writing %s\n", out.c_str()); return 1; }
    if (!midi.empty() && !writeMidiFile(score, midi.c_str(), "Ephemeris", &p)) { std::fprintf(stderr, "cannot write %s\n", midi.c_str()); return 1; }

    const double secs = static_cast<double>(total) / rate;
    std::printf("Ephemeris %s: %.0f bars, %.2f s + %.1f s tail at %.0f Hz, tempo %.1f BPM, key %s, seed %llu\n",
                EPH_VERSION, bars, engine.lengthSeconds(), tail, rate, score.tempo.bpmAt(0.0), kKeyNames[score.keyRoot % 12],
                static_cast<unsigned long long>(seed));
    std::printf("  %zu notes, %zu gestures, %zu rack events; peak %.1f dBFS, rms %.1f dBFS\n",
                score.notes.size(), score.gestures.size(), score.rack.size(),
                20.0 * std::log10(std::max(peak, 1e-12)), 10.0 * std::log10(std::max(sumSq / static_cast<double>(std::max<int64_t>(total, 1)), 1e-24)));
    std::printf("  %.3f s to render: %.0fx real time, %.2f %% of a core\n", took, secs / std::max(took, 1e-9), 100.0 * took / secs);
    for (const TempoPoint& tp : score.tempo.points())
        std::printf("  tempo point: beat %.1f, %.2f BPM%s, at %.3f s\n", tp.beat, tp.bpm, tp.rampToNext ? ", ramps" : "",
                    score.tempo.secondsAt(tp.beat));
    return 0;
}
