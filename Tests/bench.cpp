/**
 * @file bench.cpp
 * @brief eph_bench (26.09.2026): the cost of the voice bank's filters and the pad synth's, per second of audio.
 *
 * Every case renders two seconds, five times; the fastest run counts, which keeps a busy machine's noise out of the
 * figure. Printed as the share of one core at 48 kHz. Not a test: ctest does not run it.
 * @author Rene Weller
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

constexpr double Rate = 48000.0;   ///< the sample rate, Hz
constexpr int Seconds = 2;   ///< audio a run renders, s
constexpr int Runs = 5;   ///< runs; the fastest counts
constexpr int Block = 32;   ///< the block, samples

/**
 * @brief The fastest of Runs runs of @p render (Seconds of audio), as the share of a core.
 * @param[in] render  renders the audio
 * @return the share of a core
 */
template <class FT>
double share(FT render)
{
    double best = 1e30;
    for (int run = 0; run < Runs; ++run) {
        const auto t0 = std::chrono::steady_clock::now();
        render();
        best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    }
    return 100.0 * best / Seconds;
}

/**
 * @brief Ten voices playing a sequence, each with filter model @p models[v].
 * @param[in] models  the filter model per voice
 * @return the share of a core
 */
double bank(const int* models)
{
    return share([models]() {
        uint64_t seeds[BankVoices];
        for (int v = 0; v < BankVoices; ++v) seeds[v] = 100u + static_cast<uint64_t>(v);
        static ModVoiceBank b;
        b.prepare(Rate, seeds);
        bool run[BankVoices];
        for (int v = 0; v < BankVoices; ++v) {
            VoiceSettings s;
            s.m_filter = models[v];
            s.m_resonance = 0.5f;
            s.m_wave = 0.3f;
            b.set(v, s);
            run[v] = true;
        }
        const int total = static_cast<int>(Rate) * Seconds;
        for (int done = 0, step = 0; done < total; done += Block, ++step) {
            if (step % 375 == 0)   // a note every quarter second on every voice
                for (int v = 0; v < BankVoices; ++v) b.noteOn(v, 40 + (step / 375 + v) % 24, 0.8f, false, false, step);
            b.process(run, Block);
        }
    });
}

/**
 * @brief The pad synth with @p keys keys held on filter model @p model.
 * @param[in] model  the filter model
 * @param[in] keys   the keys held
 * @return the share of a core
 */
double pad(int model, int keys)
{
    return share([model, keys]() {
        static PolySynth p;
        p.prepare(Rate, 7);
        PolySettings s;
        s.m_filter = model;
        s.m_attackS = 0.05f;
        s.m_resonance = 0.4f;
        p.set(s);
        for (int k = 0; k < keys; ++k) p.noteOn(48 + 3 * k, 0.8f, k + 1);
        std::vector<float> l(Block), r(Block);
        const int total = static_cast<int>(Rate) * Seconds;
        for (int done = 0; done < total; done += Block) p.process(l.data(), r.data(), Block);
    });
}

} // namespace

/**
 * @brief Times the engine's parts and the whole; prints each as the share of a core.
 * @return the number of failures
 */
int main()
{
    prepareWavetables();
    std::printf("eph_bench: path %s, the fastest of %d runs of %d s, %% of a core at 48 kHz\n", VecPathName, Runs, Seconds);
    const int one[BankVoices] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    const int mix[BankVoices] = { 0, 0, 1, 3, 5, 3, 1, 0, 1, 3 };       // a piece's: four models
    const int all[BankVoices] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 1 };       // nine models
    std::printf("  voice bank, one model (Moog)       %6.2f %%\n", bank(one));
    std::printf("  voice bank, four models            %6.2f %%\n", bank(mix));
    std::printf("  voice bank, nine models            %6.2f %%\n", bank(all));
    std::printf("  pad synth, 8 keys, SEM             %6.2f %%\n", pad(3, 8));
    std::printf("  pad synth, 4 keys, SEM             %6.2f %%\n", pad(3, 4));
    std::printf("  pad synth, 8 keys, Moog            %6.2f %%\n", pad(0, 8));
    return 0;
}
