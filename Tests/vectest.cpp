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
 * @author Rene Weller
 *
 * @par Compile Flags:
 *   @arg @c EPH_EXPECT_PATH - the vector path the build expects; the test fails when Vec.h chose another
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

constexpr int W = VecWidth;   ///< the lanes of the vector type compiled here

/**
 * @brief Deterministic test values covering signs, tiny and large magnitudes.
 * @param[in] i  the index
 * @return the float
 */
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

/**
 * @brief Whether @p a and @p b are the same float, bit for bit.
 * @param[in] a  a float
 * @param[in] b  another float
 * @return true when they are the same bit for bit
 */
bool sameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

/** @brief The vector type's operations against the scalar ones, lane by lane, bit for bit. */
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

/** @brief The ladder filters on vectors against the scalar ones, bit for bit. */
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

/** @brief The half-band decimator on vectors against the scalar one, bit for bit. */
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

/** @brief The voice kernel on vectors against the scalar one, bit for bit. */
void testVoiceKernel()
{
    section("modular voice kernel lanes against scalar");
    // Every lane its own voice: pitch, cutoff sweep, resonance, drive and wave; the scalar reference
    // runs the same kernel with float, one lane per call, on a copy of the same state.
    VoiceLanes vec{}, sca{};
    const HalfbandDesign d = designHalfband(96.0, 0.1);
    vec.clearFilters();
    for (int l = 0; l < BankLanes; ++l) {
        vec.m_ph1[l] = 0.05f * static_cast<float>(l);
        vec.m_ph2[l] = 0.5f;
        vec.m_drive[l] = 1.0f + 0.5f * static_cast<float>(l);
        vec.m_norm[l] = 1.0f / std::sqrt(vec.m_drive[l]);
        vec.m_dcR[l] = 0.999f;
        vec.m_tbl[l] = l % 2 == 0 ? 1.0f : 0.0f;   // every other lane on its wavetable oscillators (25.09.2026)
        // The filter models (26.09.2026): every model but the comb on some lane, with its modes and filter FM.
        vec.m_fmodel[l] = static_cast<float>(l % 9);
        const float mixes[5] = { 0.0f, 2.0f, -2.0f, 0.0f, 0.0f };   // the Xpander's lanes: a band pass
        for (int j = 0; j < 5; ++j) vec.m_pm[j][l] = mixes[j];
    }
    sca = vec;
    alignas(32) float outV[BankSpan * BankLanes], outS[BankSpan * BankLanes];
    constexpr int regs = BankLanes / W;
    int bad = 0;
    float maxAbs = 0.0f;
    for (uint32_t span = 0; span < 600; ++span) {
        for (int i = 0; i < BankSpan; ++i) {
            for (int l = 0; l < BankLanes; ++l) {
                const int j = i * BankLanes + l;
                const float t = static_cast<float>(span * BankSpan + static_cast<uint32_t>(i));
                const float hz = 40.0f * static_cast<float>(l + 1) * (1.0f + 0.1f * std::sin(0.0003f * t));
                vec.m_dt1[j] = hz / 96000.0f;
                vec.m_dt2[j] = hz * 1.004f / 96000.0f;
                vec.m_inv1[j] = 1.0f / vec.m_dt1[j];
                vec.m_inv2[j] = 1.0f / vec.m_dt2[j];
                vec.m_g[j] = 0.02f + 0.7f * (0.5f + 0.5f * std::sin(0.0007f * t * static_cast<float>(l + 1)));
                vec.m_gain[j] = 0.5f + 0.5f * std::sin(0.001f * t);
                // The knobs the modulation matrix moves, per sample (26.09.2026): slow sweeps of their own per lane.
                const float sw = 0.5f + 0.5f * std::sin(0.0005f * t + static_cast<float>(l));
                vec.m_wave[j] = l % 3 == 0 ? 0.0f : sw;
                vec.m_pw[j] = 0.1f + 0.8f * sw;
                const FilterModelE fmodel = static_cast<FilterModelE>(l % 9);
                vec.m_k[j] = FilterVoicing::feedback(fmodel, 0.3f + 0.04f * static_cast<float>(l) * sw);
                vec.m_fmk[j] = FilterVoicing::makeup(fmodel, vec.m_k[j]);
                vec.m_fmode[j] = sw;
                vec.m_ffm[j] = l % 3 == 0 ? 1.5f * sw : 0.0f;
            }
        }
        auto same = [](const float* a, float* b) { std::copy(a, a + BankSpan * BankLanes, b); };
        same(vec.m_wave, sca.m_wave); same(vec.m_pw, sca.m_pw); same(vec.m_k, sca.m_k);
        same(vec.m_fmk, sca.m_fmk); same(vec.m_fmode, sca.m_fmode); same(vec.m_ffm, sca.m_ffm);
        std::copy(std::begin(vec.m_dt1), std::end(vec.m_dt1), std::begin(sca.m_dt1));
        std::copy(std::begin(vec.m_dt2), std::end(vec.m_dt2), std::begin(sca.m_dt2));
        std::copy(std::begin(vec.m_inv1), std::end(vec.m_inv1), std::begin(sca.m_inv1));
        std::copy(std::begin(vec.m_inv2), std::end(vec.m_inv2), std::begin(sca.m_inv2));
        std::copy(std::begin(vec.m_g), std::end(vec.m_g), std::begin(sca.m_g));
        std::copy(std::begin(vec.m_gain), std::end(vec.m_gain), std::begin(sca.m_gain));
        // The wavetable oscillators (every other span): anything smooth will do for the comparison.
        for (int w = 0; w < 2 * BankSpan * BankLanes; ++w) {
            const float t = static_cast<float>(span * 2 * BankSpan * BankLanes + static_cast<uint32_t>(w));
            vec.m_wt1[w] = sca.m_wt1[w] = 0.8f * std::sin(0.0011f * t);
            vec.m_wt2[w] = sca.m_wt2[w] = 0.7f * std::sin(0.0013f * t + 1.0f);
        }
        const bool table = span % 2 == 1;
        // Every register its own models (26.09.2026), the scalar reference its lane's.
        unsigned regModels[BankLanes] = {}, laneModel[BankLanes] = {};
        for (int l = 0; l < BankLanes; ++l) {
            laneModel[l] = 1u << static_cast<int>(vec.m_fmodel[l]);
            regModels[l / W] |= laneModel[l];
        }
        voiceKernel<VecF, regs>(vec, d, 0, BankSpan, true, outV, table, regModels, true);
        for (int l = 0; l < BankLanes; ++l) voiceKernel<float, 1>(sca, d, l, BankSpan, true, outS, table, &laneModel[l], true);
        for (int j = 0; j < BankSpan * BankLanes; ++j) {
            if (!sameBits(outV[j], outS[j])) ++bad;
            maxAbs = std::max(maxAbs, std::fabs(outS[j]));
        }
    }
    check(bad == 0, "voice kernel output identical to scalar", fmt("%d differing samples", bad));
    check(std::isfinite(maxAbs) && maxAbs > 0.01f && maxAbs < 20.0f, "voice kernel sounds and stays bounded", fmt("max |y| = %.3f", static_cast<double>(maxAbs)));
}

} // namespace

/**
 * @brief Runs the tests; the exit code is the number of failures.
 * @return the number of failures
 */
int main()
{
    std::printf("eph_vectest: path %s, %d lanes\n", VecPathName, W);
#if defined(EPH_EXPECT_PATH)
    check(std::strcmp(VecPathName, EPH_EXPECT_PATH) == 0, "built for the expected path", fmt("expected %s, got %s", EPH_EXPECT_PATH, VecPathName));
#endif
    testOps();
    testLadder();
    testHalfband();
    testVoiceKernel();
    return finish();
}
