/**
 * @file bench.cpp
 * @brief eph_bench (26.09.2026): the cost of the voice bank's filters and the pad synth's, per second of audio.
 *
 * Every case renders two seconds, five times; the fastest run counts, which keeps a busy machine's noise out of the
 * figure. Printed as the share of one core at 48 kHz. Not a test: ctest does not run it.
 */
#include "eph/Dsp.h"
#include "eph/synth/ModVoice.h"
#include "eph/synth/Poly.h"
#include "eph/Vec.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace eph;

namespace {

constexpr double kRate = 48000.0;
constexpr int kSeconds = 2, kRuns = 5, kBlock = 32;

/** @brief The fastest of kRuns runs of @p render (kSeconds of audio), as the share of a core. */
template <class F>
double share(F render)
{
    double best = 1e30;
    for (int run = 0; run < kRuns; ++run) {
        const auto t0 = std::chrono::steady_clock::now();
        render();
        best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    return 100.0 * best / kSeconds;
}

/** @brief Ten voices playing a sequence, each with filter model @p models[v]. */
double bank(const int* models)
{
    return share([models]() {
        uint64_t seeds[kBankVoices];
        for (int v = 0; v < kBankVoices; ++v) seeds[v] = 100u + static_cast<uint64_t>(v);
        static ModVoiceBank b;
        b.prepare(kRate, seeds);
        bool run[kBankVoices];
        for (int v = 0; v < kBankVoices; ++v) {
            VoiceSettings s;
            s.filter = models[v];
            s.resonance = 0.5f;
            s.wave = 0.3f;
            b.set(v, s);
            run[v] = true;
        }
        const int total = static_cast<int>(kRate) * kSeconds;
        for (int done = 0, step = 0; done < total; done += kBlock, ++step) {
            if (step % 375 == 0)   // a note every quarter second on every voice
                for (int v = 0; v < kBankVoices; ++v) b.noteOn(v, 40 + (step / 375 + v) % 24, 0.8f, false, false, step);
            b.process(run, kBlock);
        }
    });
}

/** @brief The pad synth with @p keys keys held on filter model @p model. */
double pad(int model, int keys)
{
    return share([model, keys]() {
        static PolySynth p;
        p.prepare(kRate, 7);
        PolySettings s;
        s.filter = model;
        s.attackS = 0.05f;
        s.resonance = 0.4f;
        p.set(s);
        for (int k = 0; k < keys; ++k) p.noteOn(48 + 3 * k, 0.8f, k + 1);
        std::vector<float> l(kBlock), r(kBlock);
        const int total = static_cast<int>(kRate) * kSeconds;
        for (int done = 0; done < total; done += kBlock) p.process(l.data(), r.data(), kBlock);
    });
}

} // namespace

int main()
{
    prepareWavetables();
    std::printf("eph_bench: path %s, the fastest of %d runs of %d s, %% of a core at 48 kHz\n", kVecPathName, kRuns, kSeconds);
    const int one[kBankVoices] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    const int mix[kBankVoices] = { 0, 0, 1, 3, 5, 3, 1, 0, 1, 3 };       // a piece's: four models
    const int all[kBankVoices] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 1 };       // nine models
    std::printf("  voice bank, one model (Moog)       %6.2f %%\n", bank(one));
    std::printf("  voice bank, four models            %6.2f %%\n", bank(mix));
    std::printf("  voice bank, nine models            %6.2f %%\n", bank(all));
    std::printf("  pad synth, 8 keys, SEM             %6.2f %%\n", pad(3, 8));
    std::printf("  pad synth, 4 keys, SEM             %6.2f %%\n", pad(3, 4));
    std::printf("  pad synth, 8 keys, Moog            %6.2f %%\n", pad(0, 8));
    return 0;
}
