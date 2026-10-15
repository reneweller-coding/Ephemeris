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
 *              [--stems DIR] [--cues FILE] [--list] [--dump-params FILE] [--version]
 * @author Rene Weller
 */
#include "eph/Loudness.h"
#include "eph/Engine.h"
#include "eph/Leveler.h"
#include "eph/compose/Composer.h"
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

/** @brief Prints the command line. */
void usage()
{
    std::printf("eph_render %s\n"
                "  --bars N         length in bars (default: compose.piece_minutes)\n"
                "  --minutes M      length in minutes\n"
                "  --bpm B          tempo (sets compose.bpm)\n"
                "  --seed S         seed of everything drawn (default 1)\n"
                "  --tail S         seconds rendered after the end, for the rooms (default 20; the last 10 fade out)\n"
                "  --archive        an archive master: no limiter, 24 bit, scaled to -3 dBTP (renders twice)\n"
                "  --concert M      a concert of pieces, M minutes long (default: one piece)\n"
                "  --reroll UNIT    draw a unit again (form, tempo, rows, rack, layers, lead, pads, hands;\n"
                "                   in a concert pieceN.UNIT or concert); may be repeated\n"
                "  --set-file FILE  play a saved .ephset (seed, lengths, parameters, rerolls)\n"
                "  --levels         print every piece's loudness correction (Leveler.h)\n"
                "  --levels-only    ... and stop there, without rendering\n"
                "  --no-level       leave the pieces as composed, without the correction\n"
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
                "  --cues FILE      write the score's cue marks (sections, keys, conjunctions; Cue.h) as text\n"
                "  --stems DIR      write a 32-bit float WAV per channel strip and one for the rooms into DIR;\n"
                "                   their sum is the mix before the master (level, compressor, limiter)\n"
                "  --list           print every parameter and exit\n"
                "  --dump-params F  write every parameter's description as JSON (for the manual) and exit\n"
                "  --quality Q      desktop (default) or quest: the headset's level (3 singers per choir key)\n"
                "  --version        print the version and exit\n", EPH_VERSION);
}

} // namespace

/**
 * @brief Parses the arguments (see usage()), composes or loads, renders, writes the files, reports.
 * @param[in]     argc  the number of arguments
 * @param[in,out] argv  the arguments: sections to run
 * @return the number of failures
 */
int main(int argc, char** argv)
{
    double bars = 0.0, minutes = 0.0, rampTo = 0.0, rate = 48000.0;
    int block = 512;
    std::string out, midi, set;
    bool list = false, frame = false, study = false, sketch = false;
    std::string dump, stemsDir, cuesOut;
    int singers = 6;
    double concert = 0.0;
    std::string setIn, setOut;
    bool noLevel = false, showLevels = false, levelsOnly = false;   // --no-level, --levels, --levels-only
    Curation curation;
    double bpmArg = 0.0, tail = 20.0;
    bool archive = false;
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
        else if (a == "--dump-params") dump = next("--dump-params");
        else if (a == "--stems") stemsDir = next("--stems");
        else if (a == "--cues") cuesOut = next("--cues");
        else if (a == "--quality") singers = std::string(next("--quality")) == "quest" ? 3 : 6;
        else if (a == "--frame") frame = true;
        else if (a == "--study") study = true;
        else if (a == "--sketch") sketch = true;
        else if (a == "--concert") concert = std::atof(next("--concert"));
        else if (a == "--reroll") curation.reroll(next("--reroll"));
        else if (a == "--set-file") setIn = next("--set-file");
        else if (a == "--no-level") noLevel = true;
        else if (a == "--levels") showLevels = true;
        else if (a == "--levels-only") showLevels = levelsOnly = true;
        else if (a == "--save-set") setOut = next("--save-set");
        else if (a == "--seed") seed = std::strtoull(next("--seed"), nullptr, 10);
        else if (a == "--tail") tail = std::atof(next("--tail"));
        else if (a == "--archive") archive = true;
        else if (a == "--version") { std::printf("%s\n", EPH_VERSION); return 0; }
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown argument %s\n", a.c_str()); usage(); return 2; }
    }

    Engine engine;
    engine.setTapeSingers(singers);
    bool soundsKept = false;   // a set whose parameters hold the composer's sounds
    double setMinutes = 0.0;   // the piece's length, for --save-set
    ParamStore& p = engine.params();
    if (!setIn.empty()) {
        // A saved set: its seed, lengths, parameters and rerolls; the command line's rerolls come on top.
        SetFile sf;
        std::string err;
        if (!loadSet(setIn.c_str(), sf, p, &err)) { std::fprintf(stderr, "--set-file: %s\n", err.c_str()); return 2; }
        seed = sf.m_seed;
        soundsKept = sf.m_soundsInParams;
        if (minutes <= 0.0) minutes = sf.m_minutes;
        if (concert <= 0.0) concert = sf.m_concert;
        for (const auto& r : curation.m_rerolls) sf.m_curation.m_rerolls[r.first] += r.second;
        curation = sf.m_curation;
    }
    if (!set.empty()) {
        std::string err;
        if (!p.parseText(set, &err)) { std::fprintf(stderr, "--set: %s\n", err.c_str()); return 2; }
    }
    const int bpmId = p.id(ModuleE::COMPOSE, 0, compose::BPM);
    if (bpmArg > 0.0) p.set(bpmId, static_cast<float>(bpmArg));
    if (!dump.empty()) {
        // One object per parameter: the key, the descriptor, the default of this instance (Tools/manual).
        FILE* f = std::fopen(dump.c_str(), "wb");
        if (f == nullptr) { std::fprintf(stderr, "cannot write %s\n", dump.c_str()); return 1; }
        auto quoted = [](const char* s) {
            std::string o = "\"";
            for (const char* c = s != nullptr ? s : ""; *c != 0; ++c) { if (*c == '"' || *c == '\\') o += '\\'; o += *c; }
            return o + "\"";
        };
        static const char* const curves[] = { "linear", "log", "int", "choice", "toggle" };
        std::fprintf(f, "[\n");
        for (int id = 0; id < p.count(); ++id) {
            const ParamDesc& d = p.desc(id);
            std::string choices = "[]";
            if (d.m_curve == CurveE::CHOICE && d.m_choices != nullptr) {
                choices = "[";
                for (int c = 0; c <= static_cast<int>(d.m_maxValue); ++c) choices += (c ? ", " : "") + quoted(d.m_choices[c]);
                choices += "]";
            }
            std::fprintf(f, "  {\"key\": %s, \"name\": %s, \"unit\": %s, \"min\": %g, \"max\": %g, \"default\": %g, \"curve\": \"%s\", \"choices\": %s}%s\n",
                         quoted(p.key(id).c_str()).c_str(), quoted(d.m_name).c_str(), quoted(d.m_unit).c_str(),
                         static_cast<double>(d.m_minValue), static_cast<double>(d.m_maxValue), static_cast<double>(p.defaultValue(id)),
                         curves[static_cast<int>(d.m_curve)], choices.c_str(), id + 1 < p.count() ? "," : "");
        }
        std::fprintf(f, "]\n");
        std::fclose(f);
        return 0;
    }
    if (list) {
        for (int id = 0; id < p.count(); ++id) std::printf("%-24s %s\n", p.key(id).c_str(), p.format(id).c_str());
        return 0;
    }

    const double bpm = p.get(bpmId);
    Score score;
    if (!frame) {
        const double mins = minutes > 0.0 ? minutes : (bars > 0.0 ? bars * BeatsPerBar / bpm
                                                                  : p.get(p.id(ModuleE::COMPOSE, 0, compose::PIECE_MINUTES)));
        if (concert <= 0.0) concert = p.get(p.id(ModuleE::COMPOSE, 0, compose::CONCERT_MINUTES));
        if (study) score = buildStudy(p, seed, mins);
        else if (sketch) score = buildSketch(p, seed, mins);
        else if (concert > 0.0) score = composeConcert(p, seed, concert, &curation);
        else score = composePiece(p, seed, mins, 0, &curation);
        setMinutes = mins;
        // Every piece as loud as its style means (Leveler.h), as the plugin does with a new piece.
        if (!noLevel) {
            for (const LevelReading& r : levelScore(score, p))
                if (showLevels)
                    std::printf("level: piece at beat %.0f: loudest part %.1f LUFS, target %.1f, correction %+.1f dB, then %.1f LUFS\n",
                                r.m_beat, static_cast<double>(r.m_measured), static_cast<double>(r.m_target), static_cast<double>(r.m_trim),
                                static_cast<double>(r.m_after));
            if (levelsOnly) return 0;
        }
        bars = score.m_lengthBeats / BeatsPerBar;
    } else {
        // The frame of Phase 0: a tempo map, a length and two markers.
        score.clear(bpm);
        score.m_keyRoot = p.getInt(p.id(ModuleE::COMPOSE, 0, compose::KEY));
        if (bars <= 0.0) {
            const double mins = minutes > 0.0 ? minutes : p.get(p.id(ModuleE::COMPOSE, 0, compose::PIECE_MINUTES));
            // Bars from minutes at the mean tempo, so a ramp does not change the length much.
            const double meanBpm = rampTo > 0.0 ? 0.5 * (bpm + rampTo) : bpm;
            bars = std::max(1.0, std::floor(mins * meanBpm / BeatsPerBar + 0.5));
        }
        score.m_lengthBeats = bars * BeatsPerBar;
        if (rampTo > 0.0) {
            score.m_tempo.add(0.0, bpm, true);
            score.m_tempo.add(score.m_lengthBeats, rampTo, false);
        }
        score.m_markers.push_back({ 0.0, "Start" });
        score.m_markers.push_back({ score.m_lengthBeats, "End" });
    }
    engine.prepare(rate, block);
    // The composer's sounds onto the knobs, as the plugin does with a new piece -- unless a saved set's parameters
    // already hold them (SetFile::soundsInParams).
    engine.load(score, !soundsKept);
    if (!setOut.empty() && !frame) {
        SetFile sf;
        sf.m_seed = seed;
        sf.m_minutes = setMinutes;
        sf.m_concert = concert;
        sf.m_curation = curation;
        if (!saveSet(setOut.c_str(), sf, p)) { std::fprintf(stderr, "cannot write %s\n", setOut.c_str()); return 1; }
    }
    if (!cuesOut.empty()) {
        FILE* f = std::fopen(cuesOut.c_str(), "w");
        if (f == nullptr) { std::fprintf(stderr, "cannot write %s\n", cuesOut.c_str()); return 1; }
        static const char* const kinds[] = { "beat", "phase", "key", "conjunction" };
        for (const CueMark& m : engine.cueMarks())
            std::fprintf(f, "%10.3f  %8.2f s  %-11s %-10s %d %.2f\n", m.m_beat, score.m_tempo.secondsAt(m.m_beat), kinds[static_cast<int>(m.m_kind)], m.m_text, m.m_a, static_cast<double>(m.m_b));
        std::fclose(f);
    }

    WavWriter wav;
    if (!out.empty() && !wav.open(out.c_str(), static_cast<int>(rate), 2, WavFormatE::PCM24)) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    // The stems: a float WAV per channel strip and one for the rooms (Engine::setStems).
    constexpr int Stems = Engine::Channels + 1;
    std::vector<std::vector<float>> stemBuf(2 * Stems, std::vector<float>(static_cast<size_t>(block)));
    std::vector<float*> stemL(Stems), stemR(Stems);
    std::vector<WavWriter> stems(stemsDir.empty() ? 0 : Stems);
    if (!stemsDir.empty()) {
        for (int c = 0; c < Stems; ++c) {
            stemL[static_cast<size_t>(c)] = stemBuf[static_cast<size_t>(2 * c)].data();
            stemR[static_cast<size_t>(c)] = stemBuf[static_cast<size_t>(2 * c + 1)].data();
            std::string name = c < Engine::Channels ? Engine::channelName(c) : "Rooms";
            for (char& ch : name) ch = ch == ' ' ? '_' : ch;
            char file[64];
            std::snprintf(file, sizeof(file), "/%02d_%s.wav", c + 1, name.c_str());
            const std::string path = stemsDir + file;
            if (!stems[static_cast<size_t>(c)].open(path.c_str(), static_cast<int>(rate), 2, WavFormatE::FLOAT32)) {
                std::fprintf(stderr, "cannot write %s (does the folder exist?)\n", path.c_str());
                return 1;
            }
        }
        engine.setStems(stemL.data(), stemR.data());
    }
    const auto t0 = std::chrono::steady_clock::now();
    const int64_t total = static_cast<int64_t>(std::llround((engine.lengthSeconds() + tail) * rate));
    std::vector<float> L(static_cast<size_t>(block)), R(static_cast<size_t>(block));
    // The archive master (the addon's 9): no limiter, the whole scaled so its true peak sits at -3 dBTP -- which takes a
    // first pass that only measures.
    float archiveGain = 1.0f;
    if (archive) {
        Engine probe;
        probe.params().copyValuesFrom(engine.params());
        probe.prepare(rate, block);
        probe.load(score);
        probe.setLimiter(false);
        LoudnessMeter pm;
        pm.prepare(rate);
        for (int64_t done = 0; done < total;) {
            const int n = static_cast<int>(std::min<int64_t>(block, total - done));
            probe.process(L.data(), R.data(), n);
            for (int i = 0; i < n; ++i) { const float g = exportFade(done + i, total, rate); L[static_cast<size_t>(i)] *= g; R[static_cast<size_t>(i)] *= g; }
            pm.process(L.data(), R.data(), n);
            done += n;
        }
        archiveGain = static_cast<float>(std::pow(10.0, (-3.0 - pm.report().m_truePeak) / 20.0));
        engine.setLimiter(false);
    }
    // The stems' correlation, each read on its own (the addon's 6: a target per layer, not one for the sum).
    std::vector<double> stemLR(stems.size(), 0.0), stemLL(stems.size(), 0.0), stemRR(stems.size(), 0.0);
    double peak = 0.0, sumSq = 0.0;
    LoudnessMeter meter;   // the production guide's figures (Loudness.h)
    meter.prepare(rate);
    for (int64_t done = 0; done < total;) {
        const int n = static_cast<int>(std::min<int64_t>(block, total - done));
        engine.process(L.data(), R.data(), n);
        for (int i = 0; i < n; ++i) {
            const float g = exportFade(done + i, total, rate) * archiveGain;
            L[static_cast<size_t>(i)] *= g;
            R[static_cast<size_t>(i)] *= g;
        }
        meter.process(L.data(), R.data(), n);
        for (size_t c = 0; c < stems.size(); ++c)
            for (int i = 0; i < n; ++i) {
                const double l = stemL[c][i], r = stemR[c][i];
                stemLR[c] += l * r; stemLL[c] += l * l; stemRR[c] += r * r;
            }
        for (int i = 0; i < n; ++i) {
            peak = std::max(peak, static_cast<double>(std::max(std::fabs(L[static_cast<size_t>(i)]), std::fabs(R[static_cast<size_t>(i)]))));
            sumSq += 0.5 * (static_cast<double>(L[static_cast<size_t>(i)]) * L[static_cast<size_t>(i)] + static_cast<double>(R[static_cast<size_t>(i)]) * R[static_cast<size_t>(i)]);
        }
        if (!out.empty()) wav.write(L.data(), R.data(), n);
        for (size_t c = 0; c < stems.size(); ++c) stems[c].write(stemL[c], stemR[c], n);
        done += n;
    }
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!out.empty() && !wav.close()) { std::fprintf(stderr, "error writing %s\n", out.c_str()); return 1; }
    for (WavWriter& w : stems) if (!w.close()) { std::fprintf(stderr, "error writing a stem in %s\n", stemsDir.c_str()); return 1; }
    if (!midi.empty() && !writeMidiFile(score, midi.c_str(), "Ephemeris", &p)) { std::fprintf(stderr, "cannot write %s\n", midi.c_str()); return 1; }

    const double secs = static_cast<double>(total) / rate;
    std::printf("Ephemeris %s: %.0f bars, %.2f s + %.1f s tail at %.0f Hz, tempo %.1f BPM, key %s, seed %llu\n",
                EPH_VERSION, bars, engine.lengthSeconds(), tail, rate, score.m_tempo.bpmAt(0.0), KeyNames[score.m_keyRoot % 12],
                static_cast<unsigned long long>(seed));
    std::printf("  %zu notes, %zu gestures, %zu rack events; peak %.1f dBFS, rms %.1f dBFS\n",
                score.m_notes.size(), score.m_gestures.size(), score.m_rack.size(),
                20.0 * std::log10(std::max(peak, 1e-12)), 10.0 * std::log10(std::max(sumSq / static_cast<double>(std::max<int64_t>(total, 1)), 1e-24)));
    const LoudnessReport lr = meter.report();
    std::printf("  loudness %.1f LUFS integrated, %.1f LUFS short-term max, range %.1f LU; true peak %.1f dBTP, PSR %.1f dB, PLR %.1f dB\n",
                lr.m_integrated, lr.m_shortTermMax, lr.m_range, lr.m_truePeak, lr.m_psr, lr.m_plr);
    std::printf("  stereo: correlation %.2f (lowest second %.2f at %.0f s), side %.1f dB under mid; crest factor %.1f dB%s\n", lr.m_correlation,
                lr.m_correlationLow, lr.m_correlationLowAt, lr.m_sideUnderMid, lr.m_crest, archive ? " (archive: no limiter, -3 dBTP)" : "");
    for (size_t c = 0; c < stems.size(); ++c)
        if (stemLL[c] > 1e-6 && stemRR[c] > 1e-6)
            std::printf("  stem %-12s correlation %.2f\n", static_cast<int>(c) < Engine::Channels ? Engine::channelName(static_cast<int>(c)) : "Rooms",
                        stemLR[c] / std::sqrt(stemLL[c] * stemRR[c]));
    std::printf("  %.3f s to render: %.0fx real time, %.2f %% of a core\n", took, secs / std::max(took, 1e-9), 100.0 * took / secs);
    for (const TempoPoint& tp : score.m_tempo.points())
        std::printf("  tempo point: beat %.1f, %.2f BPM%s, at %.3f s\n", tp.m_beat, tp.m_bpm, tp.m_rampToNext ? ", ramps" : "",
                    score.m_tempo.secondsAt(tp.m_beat));
    return 0;
}
