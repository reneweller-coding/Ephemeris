/**
 * @file Wavetable.h
 * @brief Wavetables (25.09.2026): stacks of single cycles, read band-limited -- the PPG's and Waldorf's way of
 *        making a sound move, and the oscillator of the pad synth (Poly.h).
 *
 * **The reader** is the AmbientSynth's (Noctuary's CycleTable.h), ported: a table is up to 64 single cycles, read
 * with a phase accumulator and a four-point (Catmull-Rom) interpolation, one frame blended into the next by the
 * position. What keeps a high note from aliasing is the classic remedy: every frame is stored at eight
 * resolutions, each an octave poorer than the one before (128 harmonics in 1024 samples, 64 in 512, down to a
 * single sine in 8), and a note reads the richest copy whose highest harmonic still lies below Nyquist. Every copy
 * holds eight samples per cycle of its highest harmonic, so the interpolation's own images stay far down. The
 * copies are made from each frame's Fourier coefficients, so a level is the same waveform with its upper octave of
 * harmonics taken away, phases kept. Noctuary keeps 512 harmonics; 128 are enough here -- pads and sequences whose
 * filters close far below -- and they keep the forty-odd tables at about ten megabytes, which the Quest can spare.
 *
 * **The tables.** Eight are written from formulas: Classic (sine, triangle, saw, square, a pulse), PWM (a pulse
 * from half its cycle down to a twentieth: the position is the pulse width, and a slow scan of it is the pulse
 * width modulation of an analog pad), Sync (a saw hard-synced to 1 .. 5 times its master), Formant (a saw through a
 * resonant peak that climbs from the first harmonic to the fortieth: the PPG's resonant sweeps), and Noctuary's
 * Vocal, Organ, Glass and Metal. The rest are sampled (WavetableData.cpp, Tools/wavetables/make_tables.py): the
 * AmbientSynth library's own tables -- bowed, tube, the four voices, consonant, overtone, morph, sampled -- the PPG
 * Wave's tables as the WaveEdit users rebuilt them, and a few of the Adventure Kid waveforms (AKWF and WaveEdit
 * Online are CC0). Last, the waves of the classic VCO models (Vco.h, 26.09.2026): each model's ramp morphing into its
 * square and narrowing into a pulse, the analog tables of the pad synth.
 *
 * All tables are built at once on first use (prepareWavetables(), which the engine's prepare calls), never on
 * the audio thread.
 */
#pragma once
#include <complex>
#include <cstdint>
#include <vector>

namespace eph {

/** @brief A complex radix-2 FFT of a fixed power-of-two size, in place (Noctuary's). The inverse divides by n. */
class Fft {
public:
    explicit Fft(int n);                                     ///< builds the tables for size @p n (a power of two)
    void transform(float* re, float* im, bool inverse) const;   ///< in place; the inverse scaled by 1 / n
    int size() const { return n_; }                          ///< the transform length
private:
    int n_;
    std::vector<float> cos_, sin_;
    std::vector<int> rev_;
};

/** @brief A wavetable of single cycles at eight band-limited resolutions, with the reader's arithmetic. */
struct CycleTable {
    static constexpr int kStoreLen  = 1024;   ///< samples per stored cycle at the finest level
    static constexpr int kMaxFrames = 64;     ///< the most frames a table keeps
    static constexpr int kLevels    = 8;      ///< 1024 .. 8 samples, 128 .. 1 harmonics
    static constexpr int kGuard     = 3;      ///< one sample stored before each cycle, two after it
    static constexpr float kTargetRms = 0.35355339f;   ///< the RMS the loudest frame of a table is scaled to

    int frames = 0;                ///< how many frames the table holds; 0 for an empty table
    int offset[kLevels] = {};      ///< where each level's cycles begin in `data`
    std::vector<float> data;       ///< every frame at every level, guards included, level-major

    /** @brief Samples per stored cycle at a level: 1024 halved per level. */
    static int levelLength(int level) { const int n = kStoreLen >> level; return n < 8 ? 8 : n; }
    /** @brief Highest harmonic a level keeps: an eighth of its length. */
    static int levelHarmonics(int level) { const int h = (kStoreLen / 8) >> level; return h < 1 ? 1 : h; }
    bool empty() const { return frames <= 0; }   ///< whether there is nothing to play
    /** @brief The stored cycle of frame @p f at a level, readable from index -1 to levelLength(level) + 1. */
    const float* cycle(int level, int f) const
    {
        return data.data() + static_cast<size_t>(offset[level]) + static_cast<size_t>(f) * static_cast<size_t>(levelLength(level) + kGuard) + 1;
    }
    /** @brief One sample of frame @p f at a level, at @p phase in [0, 1): Catmull-Rom through four samples. */
    float sample(int level, int f, double phase) const
    {
        const int len = levelLength(level);
        const double x = phase * static_cast<double>(len);
        int i = static_cast<int>(x);
        if (i >= len) i = len - 1;
        if (i < 0) i = 0;
        const float t = static_cast<float>(x - static_cast<double>(i));
        const float* c = cycle(level, f);
        const float y0 = c[i - 1], y1 = c[i], y2 = c[i + 1], y3 = c[i + 2];
        const float a = 0.5f * (y2 - y0);
        const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float d = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((d * t + b) * t + a) * t + y1;
    }
    /**
     * @brief One sample at @p position (0..1 across the frames): the two neighbouring frames blended.
     * @param level     the level cycleLevelFor() chose
     * @param position  0 .. 1 across the frames (clamped)
     * @param phase     the phase in cycles, [0, 1)
     */
    float at(int level, float position, double phase) const
    {
        if (frames <= 1) return sample(level, 0, phase);
        const float x = (position < 0.0f ? 0.0f : (position > 1.0f ? 1.0f : position)) * static_cast<float>(frames - 1);
        const int f = static_cast<int>(x);
        const float w = x - static_cast<float>(f);
        const float s0 = sample(level, f, phase);
        return f + 1 < frames && w > 0.0f ? s0 + w * (sample(level, f + 1, phase) - s0) : s0;
    }
    /** @brief Builds the table from a mono file of @p cycleLen-sample cycles laid end to end (not the audio thread). */
    bool build(const float* mono, int n, int cycleLen);
    /**
     * @brief Builds the table from spectra, one vector per frame: element h-1 is harmonic h as the complex amplitude
     *        of a cosine. The loudest frame is brought to kTargetRms. Not the audio thread.
     */
    bool buildFromHarmonics(const std::vector<std::vector<std::complex<double>>>& frameCoefficients);
    void clear() { frames = 0; data.clear(); }   ///< empties the table
};

/** @brief The level a cycle at @p hz reads: the richest below Nyquist, with a little hysteresis (@p current < 0: none). */
int cycleLevelFor(double hz, double sampleRate, int current);

/** @brief A sampled table as the program keeps it (WavetableData.cpp): frames of 256 samples, 16 bit. */
struct SampledTable {
    const char* name;      ///< its name in the table menu
    int frames;            ///< frames of 256 samples
    const int16_t* data;   ///< frames * 256 samples
};
constexpr int kFormulaTableCount = 8;    ///< Classic, PWM, Sync, Formant, Vocal, Organ, Glass, Metal
constexpr int kSampledTableCount = 38;   ///< WavetableData.cpp
constexpr int kVcoTableCount = 5;       ///< 26.09.2026: the VCO models' waves (Vco.h), after the sampled ones
constexpr int kWavetableCount = kFormulaTableCount + kSampledTableCount + kVcoTableCount;   ///< every table, the formulas first
extern const SampledTable kSampledTables[kSampledTableCount];
/** @brief The names of all tables, in menu order (poly.table and voice.table read them). */
extern const char* const kWavetableNames[kWavetableCount];
/** @brief "Analog", then every table: the row voices' choice (voice.table, 0 = the analog oscillators). */
extern const char* const kVoiceTableNames[kWavetableCount + 1];

/** @brief Builds every table once (thread-safe); the engine's prepare calls it, so the audio thread never builds. */
void prepareWavetables();
/** @brief Table @p index (clamped), built on first use. */
const CycleTable& wavetable(int index);

} // namespace eph
