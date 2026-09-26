/**
 * @file VoiceKernel.h
 * @brief The audio path of the modular voices as a lane kernel: eight voices per AVX2 register,
 *        four per NEON register, one on the scalar path.
 *
 * **What runs in lanes.** Everything that runs at audio rate: the two VCOs, the mixer's drive into the
 * saturator, the ladder at twice the sample rate, the half-band decimator, the DC blocker and the VCA.
 * What the lanes do not do -- glide, vibrato, the envelopes, the drift, and every four samples the
 * oscillators' frequencies and the ladder's cutoff with their exponentials and the tangent -- is
 * computed per voice on the scalar side (ModVoiceBank in ModVoice.h) and handed in per sample.
 *
 * **Bit-exactness.** The kernel uses only the single IEEE operations of Vec.h, so a lane of the vector
 * path equals the kernel instantiated with float, bit for bit (Tests/vectest.cpp). This is why:
 *  - The PolyBLEP residual multiplies by 1/dt (handed in with dt) and selects its two cases with masks,
 *    as in Phosphene's slot kernel (Välimäki and Huovilainen, "Antialiasing oscillators in subtractive
 *    synthesis", IEEE Signal Processing Magazine 2007).
 *  - The mixer's saturation is the algebraic sigmoid x / sqrt(1 + x^2), the curve the ladder already
 *    uses, instead of tanh, which has no single IEEE operation. Its antiderivative is sqrt(1 + x^2), so
 *    first-order ADAA (Parker, Zavalishin, Le Bihan, DAFx 2016) is the difference quotient
 *    (sqrt(1 + x^2) - sqrt(1 + x1^2)) / (x - x1), and that is identically (x + x1) / (sqrt(1 + x^2) +
 *    sqrt(1 + x1^2)): no cancellation, no special case when x is close to x1, one square root and one
 *    division per sample. The sigmoid bends a little earlier than tanh (x - x^3/2 against x - x^3/3),
 *    which the drive knob covers.
 *
 * **Fewer divisions.** On the vector path the kernel is bound by the divider: a division or a square
 * root of eight lanes occupies it for several cycles, and the ladder of Ladder.h spends ten divisions
 * and five roots per sample, the saturator one of each. The kernel computes the same ladder with four
 * divisions and hands the saturator's division to the ladder's last one:
 *  - With S_j = sqrt(1 + v_j^2) the ladder's local gains are t_j = 1 / S_j, so its stage factor
 *    d_i = 1 / (1 + g t_i) is S_i / (S_i + g), and the stage coefficients become
 *    a_i = g t_{i-1} d_i = g S_i / (S_{i-1} (S_i + g)) and b_i = s_{i-1} d_i = s_{i-1} S_i S_{i-1} /
 *    (S_{i-1} (S_i + g)): one reciprocal per stage.
 *  - The saturator's output is a fraction A / B (above), the ladder's input stage solves
 *    u = (x c - k Q) / (1 + k P) with c = 1 + comp k, so u = (A norm c - B k Q) / (B (1 + k P)).
 *  The result is the ladder of Ladder.h up to rounding; the lanes stay bit-identical to the float
 *  instantiation of this kernel, which is what the vector test checks.
 *
 * **Order.** VCOs -> drive -> saturator -> ladder (all at 2x) -> half-band -> DC blocker at 8 Hz ->
 * VCA. The VCA comes last, after the AC coupling: the filters then always carry the running
 * oscillators, never a decaying tail, so no state sinks into denormals while a voice is silent, and a
 * note starts on a signal without DC.
 */
#pragma once
#include "eph/Halfband.h"
#include "eph/Vec.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include "eph/synth/Filters.h"

/** @def EPH_FORCE_INLINE
 *  @brief Inlines a small lane helper whatever the compiler's size heuristics say. */
#if defined(_MSC_VER)
  #define EPH_FORCE_INLINE __forceinline
#else
  #define EPH_FORCE_INLINE inline __attribute__((always_inline))
#endif

namespace eph {

constexpr int kBankLanes = 16;   ///< lanes of the voice bank's arrays: kBankVoices rounded up to whole registers
constexpr int kBankVoices = 10;  ///< voices of the bank: the eight rows, the lead, the drone
constexpr int kBankSpan = 32;    ///< longest span the kernel renders at once (the engine's cell)

/** @brief The voices' audio-path state and coefficients, one lane per voice (structure of arrays). */
struct VoiceLanes {
    /** @name State
     *  @{ */
    alignas(32) float ph1[kBankLanes] = {}, ph2[kBankLanes] = {};    ///< VCO phases [0, 1)
    alignas(32) float satX[kBankLanes] = {}, satS[kBankLanes] = {};  ///< saturator: previous input, sqrt(1 + x^2) of it
    alignas(32) float fv[4][kBankLanes] = {}, fs[4][kBankLanes] = {};   ///< the filter's node voltages and trapezoidal states (Filters.h)
    alignas(32) float hx[kHalfbandMaxCoefs][kBankLanes] = {}, hy[kHalfbandMaxCoefs][kBankLanes] = {};   ///< decimator
    alignas(32) float dcX[kBankLanes] = {}, dcY[kBankLanes] = {};    ///< DC blocker
    /** @} */
    /** @name Constant over a span
     *  @{ */
    alignas(32) float drive[kBankLanes] = {};   ///< mixer drive, linear
    alignas(32) float norm[kBankLanes] = {};    ///< level after the saturator
    alignas(32) float fmodel[kBankLanes] = {};  ///< the filter model (FilterModel) as a number
    alignas(32) float pm[5][kBankLanes] = {};   ///< the Xpander's pole mix: weights of the input and the four stages (the knob's)
    alignas(32) float dcR[kBankLanes] = {};     ///< DC blocker pole
    alignas(32) float tbl[kBankLanes] = {};     ///< 1: the lane plays its wavetable oscillators (wt1, wt2) instead
    /** @} */
    /** @name Per sample of a span (index i * kBankLanes + lane)
     *  @{ */
    alignas(32) float dt1[kBankSpan * kBankLanes] = {}, inv1[kBankSpan * kBankLanes] = {};   ///< VCO 1 step at 2x, 1 / step
    alignas(32) float dt2[kBankSpan * kBankLanes] = {}, inv2[kBankSpan * kBankLanes] = {};   ///< VCO 2 step at 2x, 1 / step
    alignas(32) float g[kBankSpan * kBankLanes] = {};      ///< ladder integrator gain tan(pi fc / 2 fs)
    alignas(32) float gain[kBankSpan * kBankLanes] = {};   ///< VCA
    // 26.09.2026: what the modulation matrix moves (Modulation.h) runs per sample too.
    alignas(32) float wave[kBankSpan * kBankLanes] = {};   ///< 0 saw .. 1 pulse
    alignas(32) float pw[kBankSpan * kBankLanes] = {};     ///< pulse width
    alignas(32) float k[kBankSpan * kBankLanes] = {};      ///< the filter's feedback (FilterVoicing::feedback: k, R or the comb's)
    alignas(32) float fmk[kBankSpan * kBankLanes] = {};    ///< the pass band's makeup (FilterVoicing::makeup)
    alignas(32) float fmode[kBankSpan * kBankLanes] = {};  ///< the SEM's morph, the Polivoks' band pass, the comb's sign
    alignas(32) float ffm[kBankSpan * kBankLanes] = {};    ///< filter FM: oscillator 1 on the cutoff, in octaves at full swing / 3
    /** @} */
    /** @name Per sample at twice the rate (index (2 i + h) * kBankLanes + lane): the wavetable oscillators (ModVoice)
     *  @{ */
    alignas(32) float wt1[2 * kBankSpan * kBankLanes] = {}, wt2[2 * kBankSpan * kBankLanes] = {};
    /** @} */

    /** @name The comb filters (unaligned, last)
     *  @{ */
    static constexpr int kCombLen = 4096;                                ///< the comb's line at twice the rate (down to 24 Hz)
    std::vector<float> comb;                                             ///< the comb filter's lines, kCombLen a lane (ModVoiceBank::prepare)
    int combPos[kBankLanes] = {};                                        ///< ... their write positions
    float combLp[kBankLanes] = {};                                       ///< ... the damping in their loops
    /** @} */

    /** @brief Clears lane @p l's filter: its nodes, its states, its comb (a new model starts from rest). */
    void clearFilter(int l)
    {
        for (int j = 0; j < 4; ++j) fv[j][l] = fs[j][l] = 0.0f;
        if (!comb.empty()) std::fill(comb.begin() + static_cast<std::ptrdiff_t>(l) * kCombLen, comb.begin() + static_cast<std::ptrdiff_t>(l + 1) * kCombLen, 0.0f);
        combPos[l] = 0;
        combLp[l] = 0.0f;
    }
    /** @brief Clears the filters' states; the VCO phases run on. */
    void clearFilters()
    {
        for (int l = 0; l < kBankLanes; ++l) {
            satX[l] = 0.0f; satS[l] = 1.0f; dcX[l] = dcY[l] = 0.0f;
            clearFilter(l);
            for (int j = 0; j < kHalfbandMaxCoefs; ++j) hx[j][l] = hy[j][l] = 0.0f;
        }
    }
};

/** @brief PolyBLEP residual at phase @p t for step @p dt (1/dt = @p inv), branch-free. */
template <class V>
EPH_FORCE_INLINE V laneBlep(V t, V dt, V inv)
{
    const V one = lanes<V>(1.0f), zero = lanes<V>(0.0f);
    const V a = t * inv;
    const V r = vselect(vlt(t, dt), a + a - a * a - one, zero);
    const V b = (t - one) * inv;
    return r + vselect(vgt(t, one - dt), b * b + b + b + one, zero);
}

/** @brief One VCO sample (saw blended into the inverted pulse, as Oscillator.h), then the phase steps. */
template <class V>
EPH_FORCE_INLINE V laneVco(V& ph, V dt, V inv, V wave, V pw, bool pulse)
{
    const V one = lanes<V>(1.0f);
    const V t = ph;
    const V bt = laneBlep(t, dt, inv);
    V v = lanes<V>(2.0f) * t - one - bt;
    if (pulse) {
        V t2 = t + one - pw;
        t2 = vselect(vge(t2, one), t2 - one, t2);
        const V p = -(vselect(vlt(t, pw), one, -one) + bt - laneBlep(t2, dt, inv));
        v = v + wave * (p - v);
    }
    const V np = t + dt;
    ph = vselect(vge(np, one), np - one, np);
    return v;
}

/**
 * @brief Renders @p n samples of the lanes [lane, lane + R laneWidth<V>()).
 *
 * The @p R registers run through each stage side by side: their dependency chains -- the ladder's
 * divisions and square roots, sample after sample -- are independent, so the processor overlaps them.
 * One register alone waits on its own chain most of the time.
 *
 * @tparam V    lane type
 * @tparam R    registers per call
 * @param s     the lanes' state and coefficients
 * @param hbd   the decimator's coefficients
 * @param lane  the first lane of this call
 * @param n     samples, at most kBankSpan
 * @param pulse whether any lane mixes in the pulse (decided for the whole bank, so every path runs the
 *              same arithmetic; a lane with wave 0 gets exactly its saw either way)
 * @param out   per sample and lane (index i * kBankLanes + lane)
 */
template <class V, int R>
void voiceKernel(VoiceLanes& s, const HalfbandDesign& hbd, int lane, int n, bool pulse, float* out, bool table = false,
                 unsigned models = 1u, bool fm = false)
{
    constexpr int width = laneWidth<V>();
    auto at = [lane](const float* a, int r) { return loadLanes<V>(a + lane + r * width); };
    const V one = lanes<V>(1.0f), half = lanes<V>(0.5f);
    V dcr[R], drive[R], cin[R], tb[R], model[R], pmw[R][5];
    V ph1[R], ph2[R], sx[R], ss[R], dcx[R], dcy[R], fv[R][4], fs[R][4];
    HalfbandDown<V> hb[R];
    for (int r = 0; r < R; ++r) {
        dcr[r] = at(s.dcR, r); tb[r] = at(s.tbl, r);
        drive[r] = at(s.drive, r);
        cin[r] = at(s.norm, r);   // the level after the saturator
        model[r] = at(s.fmodel, r);
        for (int j = 0; j < 5; ++j) pmw[r][j] = at(s.pm[j], r);
        ph1[r] = at(s.ph1, r); ph2[r] = at(s.ph2, r); sx[r] = at(s.satX, r); ss[r] = at(s.satS, r);
        dcx[r] = at(s.dcX, r); dcy[r] = at(s.dcY, r);
        for (int j = 0; j < 4; ++j) { fv[r][j] = at(s.fv[j], r); fs[r][j] = at(s.fs[j], r); }
        hb[r].d = hbd;
        for (int j = 0; j < hbd.count; ++j) { hb[r].x[j] = at(s.hx[j], r); hb[r].y[j] = at(s.hy[j], r); }
    }

    for (int i = 0; i < n; ++i) {
        const int row = i * kBankLanes;
        // The knobs the modulation matrix moves, this sample's (26.09.2026).
        V wave[R], pw[R], k[R], mode[R], fmd[R], mk[R];
        for (int r = 0; r < R; ++r) {
            wave[r] = at(s.wave + row, r); pw[r] = at(s.pw + row, r); k[r] = at(s.k + row, r);
            mode[r] = at(s.fmode + row, r); fmd[r] = at(s.ffm + row, r); mk[r] = at(s.fmk + row, r);
        }
        V hi[2][R];
        for (int h = 0; h < 2; ++h) {
            for (int r = 0; r < R; ++r) {
                // VCOs, drive, and the saturator's fraction A / B.
                V o1 = laneVco(ph1[r], at(s.dt1 + row, r), at(s.inv1 + row, r), wave[r], pw[r], pulse);
                V o2 = laneVco(ph2[r], at(s.dt2 + row, r), at(s.inv2 + row, r), wave[r], pw[r], pulse);
                if (table) {
                    // A lane with a wavetable takes its own oscillators (ModVoice, Wavetable.h) in place of these.
                    const int w = (2 * i + h) * kBankLanes;
                    o1 = o1 + tb[r] * (at(s.wt1 + w, r) - o1);
                    o2 = o2 + tb[r] * (at(s.wt2 + w, r) - o2);
                }
                const V x = half * (o1 + o2) * drive[r];
                const V sq = vsqrt(vfmadd(x, x, one));
                const V A = x + sx[r], B = sq + ss[r];
                sx[r] = x;
                ss[r] = sq;
                // The filter (Filters.h, 26.09.2026): the saturator's output into the lane's model, each model computed
                // where a lane of the bank uses it, the lane taking its own; Newton-solved circuit equations.
                const V xs = cin[r] * A / B;
                V g = at(s.g + row, r);
                if (fm) g = g * fpow2(fmd[r] * o1);   // filter FM: oscillator 1 on the cutoff at audio rate
                V y = lanes<V>(0.0f);
                V nv[4], ns[4];
                for (int j = 0; j < 4; ++j) { nv[j] = fv[r][j]; ns[j] = fs[r][j]; }
                auto take = [&](int m, V out, const V* tv, const V* ts) {
                    const auto on = vge(model[r], lanes<V>(static_cast<float>(m) - 0.5f)) & vlt(model[r], lanes<V>(static_cast<float>(m) + 0.5f));
                    y = vselect(on, out, y);
                    for (int j = 0; j < 4; ++j) { nv[j] = vselect(on, tv[j], nv[j]); ns[j] = vselect(on, ts[j], ns[j]); }
                };
                auto runModel = [&](int m) {
                    V tv[4], ts[4];
                    for (int j = 0; j < 4; ++j) { tv[j] = fv[r][j]; ts[j] = fs[r][j]; }
                    V out = lanes<V>(0.0f);
                    switch (static_cast<FilterModel>(m)) {
                    case FilterModel::Moog: out = ladderMoog(tv, ts, xs, g, k[r]); break;
                    case FilterModel::Prophet: case FilterModel::Juno: case FilterModel::Xpander: {
                        const FilterModel fmodel = static_cast<FilterModel>(m);
                        out = otaCascade(tv, ts, xs, g, k[r], FilterVoicing::otaDrive(fmodel), FilterVoicing::otaRes(fmodel));
                        if (fmodel == FilterModel::Xpander) {
                            const float rr = FilterVoicing::otaRes(fmodel);
                            const V a0 = xs - k[r] * ftanh(lanes<V>(rr) * tv[3]) * lanes<V>(1.0f / rr);
                            out = pmw[r][0] * a0 + pmw[r][1] * tv[0] + pmw[r][2] * tv[1] + pmw[r][3] * tv[2] + pmw[r][4] * tv[3];
                        }
                        break;
                    }
                    case FilterModel::Sem: case FilterModel::Wasp:
                        out = svfNonlinear(tv, ts, xs, g, k[r], FilterVoicing::svfRange(static_cast<FilterModel>(m)),
                                           FilterVoicing::svfAsym(static_cast<FilterModel>(m)), mode[r]);
                        break;
                    case FilterModel::Polivoks:
                        out = svfNonlinear(tv, ts, xs, g, k[r], FilterVoicing::svfRange(FilterModel::Polivoks), 0.0f, mode[r], true);
                        break;
                    case FilterModel::Diode: out = diodeLadder(tv, ts, xs, g * lanes<V>(0.70710678f), k[r]); break;
                    case FilterModel::Korg35: out = korg35(tv, ts, xs, g, k[r]); break;
                    case FilterModel::Comb: {
                        // Lane by lane: a line tuned to the cutoff (at twice the rate), its feedback damped by a pole.
                        alignas(32) float xin[8], gg[8], res[8];
                        vstore(xin, xs); vstore(gg, g);
                        for (int w = 0; w < width; ++w) {
                            const int L = lane + r * width + w;
                            if (s.fmodel[L] < 8.5f || s.comb.empty()) { res[w] = 0.0f; continue; }
                            const float period = std::min(static_cast<float>(VoiceLanes::kCombLen - 2), 3.14159265f / std::atan(std::max(gg[w], 1e-4f)));
                            float rp = static_cast<float>(s.combPos[L]) - period;
                            if (rp < 0.0f) rp += static_cast<float>(VoiceLanes::kCombLen);
                            const int i0 = static_cast<int>(rp);
                            const float fr = rp - static_cast<float>(i0);
                            float* line = s.comb.data() + static_cast<size_t>(L) * VoiceLanes::kCombLen;
                            const float d = line[i0] + fr * (line[(i0 + 1) & (VoiceLanes::kCombLen - 1)] - line[i0]);
                            s.combLp[L] += 0.5f * (d - s.combLp[L]);
                            const float fb = (s.fmode[row + L] >= 0.5f ? -1.0f : 1.0f) * s.k[row + L];
                            const float yv = xin[w] + fb * s.combLp[L];
                            line[s.combPos[L]] = yv;
                            s.combPos[L] = (s.combPos[L] + 1) & (VoiceLanes::kCombLen - 1);
                            res[w] = 0.5f * yv;
                        }
                        out = loadLanes<V>(res);
                        break;
                    }
                    default: break;
                    }
                    take(m, out, tv, ts);
                };
                for (int m = 0; m < kFilterModels; ++m) if (models & (1u << m)) runModel(m);
                for (int j = 0; j < 4; ++j) { fv[r][j] = nv[j]; fs[r][j] = ns[j]; }
                hi[h][r] = y * mk[r];
            }
        }
        for (int r = 0; r < R; ++r) {
            const V d = hb[r].process(hi[0][r], hi[1][r]);
            const V dc = d - dcx[r] + dcr[r] * dcy[r];
            dcx[r] = d;
            dcy[r] = dc;
            vstore(out + row + lane + r * width, dc * at(s.gain + row, r));
        }
    }

    for (int r = 0; r < R; ++r) {
        auto put = [lane, r](float* a, V v) { vstore(a + lane + r * width, v); };
        put(s.ph1, ph1[r]); put(s.ph2, ph2[r]); put(s.satX, sx[r]); put(s.satS, ss[r]); put(s.dcX, dcx[r]); put(s.dcY, dcy[r]);
        for (int j = 0; j < 4; ++j) { put(s.fv[j], fv[r][j]); put(s.fs[j], fs[r][j]); }
        for (int j = 0; j < hbd.count; ++j) { put(s.hx[j], hb[r].x[j]); put(s.hy[j], hb[r].y[j]); }
    }
}

} // namespace eph
