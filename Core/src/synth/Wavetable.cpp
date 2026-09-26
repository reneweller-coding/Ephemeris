/**
 * @file Wavetable.cpp
 * @brief Building the cycle stacks (ported from the AmbientSynth's CycleTable.cpp), the formula tables, and the
 *        registry of every table.
 */
#include "eph/synth/Wavetable.h"
#include "eph/synth/Vco.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>

namespace eph {

namespace {
using Coeffs = std::vector<std::complex<double>>;
constexpr double kPiD = 3.14159265358979323846;
}

// ---------------------------------------------------------------- Fft

Fft::Fft(int n) : n_(n)
{
    cos_.resize(static_cast<size_t>(n / 2));
    sin_.resize(static_cast<size_t>(n / 2));
    for (int i = 0; i < n / 2; ++i) {
        cos_[static_cast<size_t>(i)] = static_cast<float>(std::cos(2.0 * kPiD * i / n));
        sin_[static_cast<size_t>(i)] = static_cast<float>(std::sin(2.0 * kPiD * i / n));
    }
    rev_.resize(static_cast<size_t>(n));
    int bits = 0;
    while ((1 << bits) < n) ++bits;
    for (int i = 0; i < n; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        rev_[static_cast<size_t>(i)] = r;
    }
}

void Fft::transform(float* re, float* im, bool inverse) const
{
    for (int i = 0; i < n_; ++i) {
        const int j = rev_[static_cast<size_t>(i)];
        if (j > i) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n_; len <<= 1) {
        const int half = len / 2, stride = n_ / len;
        for (int start = 0; start < n_; start += len) {
            for (int k = 0; k < half; ++k) {
                const float c = cos_[static_cast<size_t>(k * stride)];
                const float s = inverse ? sin_[static_cast<size_t>(k * stride)] : -sin_[static_cast<size_t>(k * stride)];
                const int a = start + k, b = a + half;
                const float tr = re[b] * c - im[b] * s;
                const float ti = re[b] * s + im[b] * c;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr;        im[a] += ti;
            }
        }
    }
    if (inverse) {
        const float inv = 1.0f / static_cast<float>(n_);
        for (int i = 0; i < n_; ++i) { re[i] *= inv; im[i] *= inv; }
    }
}

// ---------------------------------------------------------------- CycleTable

namespace {

/** @brief The harmonics of one cycle of @p len samples (a power of two), as complex amplitudes of cosines. */
Coeffs analyseCycle(const float* x, int len, const Fft& fft, std::vector<float>& re, std::vector<float>& im)
{
    const int top = std::min(CycleTable::levelHarmonics(0), (len - 1) / 2);
    Coeffs c(static_cast<size_t>(std::max(top, 0)));
    if (top <= 0) return c;
    const double scale = 2.0 / static_cast<double>(len);
    std::memcpy(re.data(), x, sizeof(float) * static_cast<size_t>(len));
    std::fill(im.begin(), im.begin() + len, 0.0f);
    fft.transform(re.data(), im.data(), false);
    for (int h = 1; h <= top; ++h)
        c[static_cast<size_t>(h - 1)] = std::complex<double>(re[static_cast<size_t>(h)], im[static_cast<size_t>(h)]) * scale;
    return c;
}

/** @brief One stored cycle at a level from the harmonics it keeps, and its guards. */
void synthesise(const Coeffs& c, double gain, int level, const Fft& fft, std::vector<float>& re, std::vector<float>& im, float* out)
{
    const int len = CycleTable::levelLength(level);
    const int top = std::min(CycleTable::levelHarmonics(level), static_cast<int>(c.size()));
    std::fill(re.begin(), re.begin() + len, 0.0f);
    std::fill(im.begin(), im.begin() + len, 0.0f);
    const double half = 0.5 * static_cast<double>(len) * gain;
    for (int h = 1; h <= top; ++h) {
        const std::complex<double> v = c[static_cast<size_t>(h - 1)] * half;
        re[static_cast<size_t>(h)] = static_cast<float>(v.real());
        im[static_cast<size_t>(h)] = static_cast<float>(v.imag());
        re[static_cast<size_t>(len - h)] = static_cast<float>(v.real());
        im[static_cast<size_t>(len - h)] = static_cast<float>(-v.imag());
    }
    fft.transform(re.data(), im.data(), true);
    for (int n = 0; n < len; ++n) out[n] = re[static_cast<size_t>(n)];
    out[-1] = out[len - 1];
    out[len] = out[0];
    out[len + 1] = out[1];
}

} // namespace

bool CycleTable::buildFromHarmonics(const std::vector<Coeffs>& in)
{
    clear();
    const int count = std::min(static_cast<int>(in.size()), kMaxFrames);
    if (count <= 0) return false;
    double loudest = 0.0;
    for (int k = 0; k < count; ++k) {
        double e = 0.0;
        const int top = std::min(static_cast<int>(in[static_cast<size_t>(k)].size()), levelHarmonics(0));
        for (int h = 0; h < top; ++h) e += std::norm(in[static_cast<size_t>(k)][static_cast<size_t>(h)]);
        loudest = std::max(loudest, std::sqrt(0.5 * e));
    }
    if (!(loudest > 1.0e-9)) return false;
    const double gain = static_cast<double>(kTargetRms) / loudest;
    size_t total = 0;
    for (int l = 0; l < kLevels; ++l) {
        offset[l] = static_cast<int>(total);
        total += static_cast<size_t>(count) * static_cast<size_t>(levelLength(l) + kGuard);
    }
    data.assign(total, 0.0f);
    std::vector<float> re(static_cast<size_t>(kStoreLen)), im(static_cast<size_t>(kStoreLen));
    std::unique_ptr<Fft> fft;
    for (int l = 0; l < kLevels; ++l) {
        const int len = levelLength(l);
        if (fft == nullptr || fft->size() != len) fft = std::make_unique<Fft>(len);
        for (int k = 0; k < count; ++k) {
            float* out = data.data() + static_cast<size_t>(offset[l]) + static_cast<size_t>(k) * static_cast<size_t>(len + kGuard) + 1;
            synthesise(in[static_cast<size_t>(k)], gain, l, *fft, re, im, out);
        }
    }
    frames = count;
    return true;
}

bool CycleTable::build(const float* mono, int n, int cycleLen)
{
    clear();
    if (mono == nullptr || cycleLen < 8 || n < cycleLen || (cycleLen & (cycleLen - 1)) != 0) return false;
    const int total = n / cycleLen;
    const int keep = std::min(total, kMaxFrames);
    Fft fft(cycleLen);
    std::vector<float> re(static_cast<size_t>(cycleLen)), im(static_cast<size_t>(cycleLen));
    std::vector<Coeffs> frameCoeffs(static_cast<size_t>(keep));
    for (int k = 0; k < keep; ++k) {
        const int src = keep == total ? k : static_cast<int>(static_cast<long long>(k) * (total - 1) / std::max(keep - 1, 1));
        frameCoeffs[static_cast<size_t>(k)] = analyseCycle(mono + static_cast<size_t>(src) * static_cast<size_t>(cycleLen), cycleLen, fft, re, im);
    }
    return buildFromHarmonics(frameCoeffs);
}

int cycleLevelFor(double hz, double sampleRate, int current)
{
    const double nyquist = 0.5 * sampleRate;
    int floorLevel = 0;
    while (floorLevel < CycleTable::kLevels - 1 && CycleTable::levelHarmonics(floorLevel) * hz >= nyquist) ++floorLevel;
    if (current > floorLevel && current < CycleTable::kLevels) {
        int l = floorLevel;
        while (l < current && CycleTable::levelHarmonics(l) * hz > 0.9 * nyquist) ++l;
        return l;
    }
    return floorLevel;
}

// ---------------------------------------------------------------- the tables

namespace {

/** @brief Every frame brought to the same RMS, so a morph does not fade as it goes (Noctuary's rule). */
Coeffs unit(Coeffs c)
{
    double e = 0.0;
    for (const auto& v : c) e += std::norm(v);
    const double s = e > 0.0 ? 1.0 / std::sqrt(0.5 * e) : 0.0;
    for (auto& v : c) v *= s;
    return c;
}

/** @brief The formula tables and the sampled ones, built once. */
struct Tables {
    CycleTable t[kWavetableCount];
    Tables()
    {
        const int H = CycleTable::levelHarmonics(0);
        const std::complex<double> sine(0.0, -1.0);   // sin x = cos(x - pi/2)
        // Classic: sine, triangle, saw, square, a pulse of a fifth -- the real waveforms, with their phases.
        {
            std::vector<Coeffs> f(5, Coeffs(static_cast<size_t>(H)));
            f[0][0] = sine;
            for (int h = 1; h <= H; ++h) {
                const double sign = (h % 2 == 0) ? -1.0 : 1.0;
                if (h % 2 == 1) f[1][static_cast<size_t>(h - 1)] = sine * ((((h - 1) / 2) % 2 == 0 ? 1.0 : -1.0) * 8.0 / (kPiD * kPiD * h * h));
                f[2][static_cast<size_t>(h - 1)] = sine * (sign * 2.0 / (kPiD * h));
                if (h % 2 == 1) f[3][static_cast<size_t>(h - 1)] = sine * (4.0 / (kPiD * h));
                f[4][static_cast<size_t>(h - 1)] = std::complex<double>(2.0 / (kPiD * h) * std::sin(kPiD * h * 0.2), 0.0);
            }
            for (auto& c : f) c = unit(std::move(c));
            t[0].buildFromHarmonics(f);
        }
        // PWM: a pulse from half its cycle to a twentieth, centred on the cycle's start (its harmonics real).
        {
            std::vector<Coeffs> f(32, Coeffs(static_cast<size_t>(H)));
            for (int k = 0; k < 32; ++k) {
                const double w = 0.5 - 0.45 * k / 31.0;
                for (int h = 1; h <= H; ++h) f[static_cast<size_t>(k)][static_cast<size_t>(h - 1)] = 2.0 / (kPiD * h) * std::sin(kPiD * h * w);
                f[static_cast<size_t>(k)] = unit(std::move(f[static_cast<size_t>(k)]));
            }
            t[1].buildFromHarmonics(f);
        }
        // Sync: a saw hard-synced to 1 .. 5 times its master's frequency, drawn as samples and analysed.
        {
            constexpr int L = 2048;
            std::vector<float> x(static_cast<size_t>(32 * L));
            for (int k = 0; k < 32; ++k) {
                const double ratio = 1.0 + 4.0 * k / 31.0;
                for (int n = 0; n < L; ++n) {
                    const double ph = ratio * n / L;
                    x[static_cast<size_t>(k * L + n)] = static_cast<float>(2.0 * (ph - std::floor(ph)) - 1.0);
                }
            }
            t[2].build(x.data(), static_cast<int>(x.size()), L);
        }
        // Formant: a saw through a resonant peak climbing from the first harmonic to the fortieth.
        {
            std::vector<Coeffs> f(32, Coeffs(static_cast<size_t>(H)));
            for (int k = 0; k < 32; ++k) {
                const double peak = std::pow(40.0, k / 31.0);
                for (int h = 1; h <= H; ++h) {
                    const double d = (h - peak) / (0.2 * peak + 1.0);
                    f[static_cast<size_t>(k)][static_cast<size_t>(h - 1)] = sine * ((0.2 + 3.0 / (1.0 + d * d)) / h);
                }
                f[static_cast<size_t>(k)] = unit(std::move(f[static_cast<size_t>(k)]));
            }
            t[3].buildFromHarmonics(f);
        }
        // Vocal, Organ, Glass, Metal: Noctuary's spectra of 32 partials, with sine phases.
        {
            auto table = [&](int index, const std::vector<std::vector<double>>& frames) {
                std::vector<Coeffs> f;
                for (const auto& a : frames) {
                    Coeffs c(static_cast<size_t>(H));
                    for (size_t h = 0; h < a.size() && h < c.size(); ++h) c[h] = sine * a[h];
                    f.push_back(unit(std::move(c)));
                }
                t[index].buildFromHarmonics(f);
            };
            std::vector<std::vector<double>> vocal(5, std::vector<double>(32));
            const double F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 270, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
            const double B[3] = { 90.0, 110.0, 160.0 }, G[3] = { 1.0, 0.5, 0.25 };
            for (int v = 0; v < 5; ++v)
                for (int h = 1; h <= 32; ++h) {
                    double a = 0.02;
                    for (int k = 0; k < 3; ++k) { const double x = (130.0 * h - F[v][k]) / B[k]; a += G[k] * std::exp(-x * x); }
                    vocal[static_cast<size_t>(v)][static_cast<size_t>(h - 1)] = a / std::sqrt(static_cast<double>(h));
                }
            table(4, vocal);
            std::vector<std::vector<double>> organ(4, std::vector<double>(32));
            organ[0][0] = 1.0; organ[0][1] = 0.8;
            organ[1][0] = 1.0; organ[1][1] = 0.7; organ[1][3] = 0.5; organ[1][7] = 0.3;
            { const int hs[] = { 1, 2, 3, 4, 5, 6, 8 }; const double g[] = { 1.0, 0.8, 0.6, 0.5, 0.4, 0.35, 0.3 }; for (int i = 0; i < 7; ++i) organ[2][static_cast<size_t>(hs[i] - 1)] = g[i]; }
            { const int hs[] = { 1, 2, 3, 4, 6, 8, 10, 12, 16, 24 }; for (int i = 0; i < 10; ++i) organ[3][static_cast<size_t>(hs[i] - 1)] = 1.0 / std::sqrt(i + 1.0); }
            table(5, organ);
            std::vector<std::vector<double>> glass(4, std::vector<double>(32));
            glass[0][0] = 1.0;
            glass[1][0] = 1.0; glass[1][2] = 0.6; glass[1][7] = 0.3;
            glass[2][0] = 1.0; glass[2][3] = 0.5; glass[2][8] = 0.35; glass[2][15] = 0.25;
            glass[3][0] = 1.0; glass[3][2] = 0.6; glass[3][6] = 0.45; glass[3][11] = 0.35; glass[3][18] = 0.3; glass[3][26] = 0.25;
            table(6, glass);
            std::vector<std::vector<double>> metal(4, std::vector<double>(32));
            for (int h = 1; h <= 32; ++h) {
                const double r = std::sqrt(static_cast<double>(h));
                if (h % 2 == 1) metal[0][static_cast<size_t>(h - 1)] = 1.0 / r;
                metal[1][static_cast<size_t>(h - 1)] = std::fabs(std::cos(0.7 * h)) / r;
                metal[2][static_cast<size_t>(h - 1)] = ((h % 3) == 0 ? 1.0 : 0.3) / r;
                metal[3][static_cast<size_t>(h - 1)] = (0.5 + 0.5 * std::sin(2.3 * h + 1.0)) / std::pow(static_cast<double>(h), 0.35);
            }
            table(7, metal);
        }
        // The sampled tables: frames of 256 samples.
        std::vector<float> x;
        for (int i = 0; i < kSampledTableCount; ++i) {
            const SampledTable& s = kSampledTables[i];
            x.resize(static_cast<size_t>(s.frames) * 256);
            for (size_t n = 0; n < x.size(); ++n) x[n] = static_cast<float>(s.data[n]) / 32768.0f;
            t[kFormulaTableCount + i].build(x.data(), static_cast<int>(x.size()), 256);
        }
        // The VCO models' waves (Vco.h): sixteen frames, the model's ramp blended into its square over the first six,
        // the pulse then narrowing to a twelfth -- a model's PWM pad, drawn as samples and analysed.
        for (int m = 1; m <= kVcoTableCount; ++m) {
            constexpr int L = 2048;
            std::vector<float> y(static_cast<size_t>(16 * L));
            for (int k = 0; k < 16; ++k) {
                const float wave = std::min(1.0f, static_cast<float>(k) / 5.0f);
                const float pw = k <= 5 ? 0.5f : 0.5f - 0.42f * static_cast<float>(k - 5) / 10.0f;
                for (int n = 0; n < L; ++n) y[static_cast<size_t>(k * L + n)] = VcoOsc::value(vcoProfile(m), wave, pw, static_cast<double>(n) / L);
            }
            t[kFormulaTableCount + kSampledTableCount + m - 1].build(y.data(), static_cast<int>(y.size()), L);
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

} // namespace

void prepareWavetables() { (void)tables(); }

const CycleTable& wavetable(int index)
{
    return tables().t[std::clamp(index, 0, kWavetableCount - 1)];
}

} // namespace eph
