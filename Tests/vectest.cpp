/**
 * @file vectest.cpp
 * @brief Lane paths against the scalar reference, bit for bit.
 *
 * Built once per vector path (Tests/CMakeLists.txt): AVX2, NEON through the x86 shim, and scalar.
 * Every lane of every vector operation, and of the ladder, the half-band filters and the modular
 * voices' kernel run as lane templates, must equal the float instantiation exactly -- not within a
 * tolerance. If this ever
 * needs a tolerance, an operation has crept in that is not a single IEEE operation per lane.
 * @note Copied from Phosphene `Tests/vectest.cpp` at 9a2f615 (24.09.2026): the operations, the ladder and
 *       the half-band; the sections of the psytrance voices are left out.
 */
#include "eph/Halfband.h"
#include "eph/synth/Ladder.h"
#include "eph/synth/VoiceKernel.h"
#include "eph/Vec.h"
#include "TestSupport.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

using namespace eph;
using namespace ephtest;

namespace {

constexpr int W = kVecWidth;

/** @brief Deterministic test values covering signs, tiny and large magnitudes. */
float testValue(uint32_t i)
{
    uint32_t h = i * 2654435761u ^ 0x9E3779B9u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    const float u = static_cast<float>(h) / 4294967296.0f;
    switch (i % 5) {
    case 0:  return u * 2.0f - 1.0f;
    case 1:  return (u * 2.0f - 1.0f) * 1.0e-6f;
    case 2:  return (u * 2.0f - 1.0f) * 1000.0f;
    case 3:  return u + 0.5f;
    default: return -(u + 0.25f);
    }
}

bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

void testOps()
{
    section("vector operations, lane by lane");
    int bad = 0, total = 0;
    float a[8], b[8], c[8];
    for (uint32_t round = 0; round < 2000; ++round) {
        for (int i = 0; i < 8; ++i) { a[i] = testValue(round * 8 + i); b[i] = testValue(round * 8 + i + 11); c[i] = testValue(round * 8 + i + 23); }
        const VecF va = loadLanes<VecF>(a), vb = loadLanes<VecF>(b), vc = loadLanes<VecF>(c);
        const VecF r[] = {
            va + vb, va - vb, va * vb, va / vb, vfmadd(va, vb, vc), vfnmadd(va, vb, vc),
            vmin(va, vb), vmax(va, vb), vsqrt(vabs(va)), vabs(va), vfloor(va), vselect(vlt(va, vb), vc, va),
        };
        for (int l = 0; l < W; ++l) {
            const float x = a[l], y = b[l], z = c[l];
            const float s[] = {
                x + y, x - y, x * y, x / y, vfmadd(x, y, z), vfnmadd(x, y, z),
                vmin(x, y), vmax(x, y), vsqrt(vabs(x)), vabs(x), vfloor(x), vselect(vlt(x, y), z, x),
            };
            for (size_t k = 0; k < sizeof(s) / sizeof(s[0]); ++k) {
                ++total;
                if (!sameBits(laneOf(r[k], l), s[k])) ++bad;
            }
        }
    }
    check(bad == 0, "12 operations identical to scalar", fmt("%d of %d lanes differ", bad, total));
}

void testLadder()
{
    section("ladder lanes against the scalar ladder");
    LadderT<VecF> vl;
    LadderT<float> sl[8];
    vl.reset();
    for (auto& s : sl) s.reset();
    int bad = 0;
    float maxAbs = 0.0f;
    float x[8], g[8], k[8], comp[8];
    for (uint32_t n = 0; n < 20000; ++n) {
        for (int l = 0; l < 8; ++l) {
            // A different saw, cutoff sweep and resonance per lane; loud enough to saturate.
            const float ph = static_cast<float>((n * (l + 3)) % 97) / 97.0f;
            x[l] = (2.0f * ph - 1.0f) * (0.5f + static_cast<float>(l));
            g[l] = 0.01f + 0.6f * (0.5f + 0.5f * std::sin(0.001f * static_cast<float>(n) * static_cast<float>(l + 1)));
            k[l] = static_cast<float>(l) * 0.55f;
            comp[l] = 0.5f;
        }
        const VecF y = vl.tick(loadLanes<VecF>(x), loadLanes<VecF>(g), loadLanes<VecF>(k), loadLanes<VecF>(comp));
        for (int l = 0; l < W; ++l) {
            const float ys = sl[l].tick(x[l], g[l], k[l], comp[l]);
            if (!sameBits(laneOf(y, l), ys)) ++bad;
            maxAbs = std::max(maxAbs, std::fabs(ys));
        }
    }
    check(bad == 0, "ladder output identical to scalar", fmt("%d differing samples", bad));
    check(std::isfinite(maxAbs) && maxAbs < 50.0f, "ladder bounded under drive and resonance", fmt("max |y| = %.3f", static_cast<double>(maxAbs)));
}

void testHalfband()
{
    section("half-band lanes against scalar");
    const HalfbandDesign d = designHalfband(96.0, 0.1);
    HalfbandDown<VecF> vd;
    HalfbandUp<VecF> vu;
    HalfbandDown<float> sd[8];
    HalfbandUp<float> su[8];
    vd.setup(d); vu.setup(d);
    for (auto& s : sd) s.setup(d);
    for (auto& s : su) s.setup(d);
    int bad = 0;
    float a[8], b[8];
    for (uint32_t n = 0; n < 5000; ++n) {
        for (int l = 0; l < 8; ++l) { a[l] = testValue(n * 8 + l); b[l] = testValue(n * 8 + l + 5); }
        const VecF yd = vd.process(loadLanes<VecF>(a), loadLanes<VecF>(b));
        VecF u0, u1;
        vu.process(loadLanes<VecF>(a), u0, u1);
        for (int l = 0; l < W; ++l) {
            float s0, s1;
            su[l].process(a[l], s0, s1);
            if (!sameBits(laneOf(yd, l), sd[l].process(a[l], b[l]))) ++bad;
            if (!sameBits(laneOf(u0, l), s0) || !sameBits(laneOf(u1, l), s1)) ++bad;
        }
    }
    check(bad == 0, "decimator and interpolator identical to scalar", fmt("%d differing samples", bad));
}

void testVoiceKernel()
{
    section("modular voice kernel lanes against scalar");
    // Every lane its own voice: pitch, cutoff sweep, resonance, drive and wave; the scalar reference
    // runs the same kernel with float, one lane per call, on a copy of the same state.
    VoiceLanes vec{}, sca{};
    const HalfbandDesign d = designHalfband(96.0, 0.1);
    vec.clearFilters();
    for (int l = 0; l < kBankLanes; ++l) {
        vec.ph1[l] = 0.05f * static_cast<float>(l);
        vec.ph2[l] = 0.5f;
        vec.drive[l] = 1.0f + 0.5f * static_cast<float>(l);
        vec.norm[l] = 1.0f / std::sqrt(vec.drive[l]);
        vec.dcR[l] = 0.999f;
        vec.tbl[l] = l % 2 == 0 ? 1.0f : 0.0f;   // every other lane on its wavetable oscillators (25.09.2026)
        // The filter models (26.09.2026): every model but the comb on some lane, with its modes and filter FM.
        vec.fmodel[l] = static_cast<float>(l % 9);
        const float mixes[5] = { 0.0f, 2.0f, -2.0f, 0.0f, 0.0f };   // the Xpander's lanes: a band pass
        for (int j = 0; j < 5; ++j) vec.pm[j][l] = mixes[j];
    }
    sca = vec;
    alignas(32) float outV[kBankSpan * kBankLanes], outS[kBankSpan * kBankLanes];
    constexpr int regs = kBankLanes / W;
    int bad = 0;
    float maxAbs = 0.0f;
    for (uint32_t span = 0; span < 600; ++span) {
        for (int i = 0; i < kBankSpan; ++i) {
            for (int l = 0; l < kBankLanes; ++l) {
                const int j = i * kBankLanes + l;
                const float t = static_cast<float>(span * kBankSpan + static_cast<uint32_t>(i));
                const float hz = 40.0f * static_cast<float>(l + 1) * (1.0f + 0.1f * std::sin(0.0003f * t));
                vec.dt1[j] = hz / 96000.0f;
                vec.dt2[j] = hz * 1.004f / 96000.0f;
                vec.inv1[j] = 1.0f / vec.dt1[j];
                vec.inv2[j] = 1.0f / vec.dt2[j];
                vec.g[j] = 0.02f + 0.7f * (0.5f + 0.5f * std::sin(0.0007f * t * static_cast<float>(l + 1)));
                vec.gain[j] = 0.5f + 0.5f * std::sin(0.001f * t);
                // The knobs the modulation matrix moves, per sample (26.09.2026): slow sweeps of their own per lane.
                const float sw = 0.5f + 0.5f * std::sin(0.0005f * t + static_cast<float>(l));
                vec.wave[j] = l % 3 == 0 ? 0.0f : sw;
                vec.pw[j] = 0.1f + 0.8f * sw;
                const FilterModel fmodel = static_cast<FilterModel>(l % 9);
                vec.k[j] = FilterVoicing::feedback(fmodel, 0.3f + 0.04f * static_cast<float>(l) * sw);
                vec.fmk[j] = FilterVoicing::makeup(fmodel, vec.k[j]);
                vec.fmode[j] = sw;
                vec.ffm[j] = l % 3 == 0 ? 1.5f * sw : 0.0f;
            }
        }
        auto same = [](const float* a, float* b) { std::copy(a, a + kBankSpan * kBankLanes, b); };
        same(vec.wave, sca.wave); same(vec.pw, sca.pw); same(vec.k, sca.k);
        same(vec.fmk, sca.fmk); same(vec.fmode, sca.fmode); same(vec.ffm, sca.ffm);
        std::copy(std::begin(vec.dt1), std::end(vec.dt1), std::begin(sca.dt1));
        std::copy(std::begin(vec.dt2), std::end(vec.dt2), std::begin(sca.dt2));
        std::copy(std::begin(vec.inv1), std::end(vec.inv1), std::begin(sca.inv1));
        std::copy(std::begin(vec.inv2), std::end(vec.inv2), std::begin(sca.inv2));
        std::copy(std::begin(vec.g), std::end(vec.g), std::begin(sca.g));
        std::copy(std::begin(vec.gain), std::end(vec.gain), std::begin(sca.gain));
        // The wavetable oscillators (every other span): anything smooth will do for the comparison.
        for (int w = 0; w < 2 * kBankSpan * kBankLanes; ++w) {
            const float t = static_cast<float>(span * 2 * kBankSpan * kBankLanes + static_cast<uint32_t>(w));
            vec.wt1[w] = sca.wt1[w] = 0.8f * std::sin(0.0011f * t);
            vec.wt2[w] = sca.wt2[w] = 0.7f * std::sin(0.0013f * t + 1.0f);
        }
        const bool table = span % 2 == 1;
        // Every register its own models (26.09.2026), the scalar reference its lane's.
        unsigned regModels[kBankLanes] = {}, laneModel[kBankLanes] = {};
        for (int l = 0; l < kBankLanes; ++l) {
            laneModel[l] = 1u << static_cast<int>(vec.fmodel[l]);
            regModels[l / W] |= laneModel[l];
        }
        voiceKernel<VecF, regs>(vec, d, 0, kBankSpan, true, outV, table, regModels, true);
        for (int l = 0; l < kBankLanes; ++l) voiceKernel<float, 1>(sca, d, l, kBankSpan, true, outS, table, &laneModel[l], true);
        for (int j = 0; j < kBankSpan * kBankLanes; ++j) {
            if (!sameBits(outV[j], outS[j])) ++bad;
            maxAbs = std::max(maxAbs, std::fabs(outS[j]));
        }
    }
    check(bad == 0, "voice kernel output identical to scalar", fmt("%d differing samples", bad));
    check(std::isfinite(maxAbs) && maxAbs > 0.01f && maxAbs < 20.0f, "voice kernel sounds and stays bounded", fmt("max |y| = %.3f", static_cast<double>(maxAbs)));
}

} // namespace

int main()
{
    std::printf("eph_vectest: path %s, %d lanes\n", kVecPathName, W);
#if defined(EPH_EXPECT_PATH)
    check(std::strcmp(kVecPathName, EPH_EXPECT_PATH) == 0, "built for the expected path", fmt("expected %s, got %s", EPH_EXPECT_PATH, kVecPathName));
#endif
    testOps();
    testLadder();
    testHalfband();
    testVoiceKernel();
    return finish();
}
