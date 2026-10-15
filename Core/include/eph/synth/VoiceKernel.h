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
 *    a_i = g m_t{i-1} d_i = g S_i / (S_{i-1} (S_i + g)) and b_i = m_s{i-1} d_i = m_s{i-1} S_i S_{i-1} /
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

constexpr int BankLanes = 16;   ///< lanes of the voice bank's arrays: BankVoices rounded up to whole registers
constexpr int BankVoices = 10;  ///< voices of the bank: the eight rows, the lead, the drone
constexpr int BankSpan = 32;    ///< longest span the kernel renders at once (the engine's cell)

/** @brief The voices' audio-path state and coefficients, one lane per voice (structure of arrays). */
struct VoiceLanes {
    /** @name State
     *  @{ */
    alignas(32) float m_ph1[BankLanes] = {}, m_ph2[BankLanes] = {};    ///< VCO phases [0, 1)
    alignas(32) float m_satX[BankLanes] = {}, m_satS[BankLanes] = {};  ///< saturator: previous input, sqrt(1 + x^2) of it
    alignas(32) float m_fv[4][BankLanes] = {}, m_fs[4][BankLanes] = {};   ///< the filter's node voltages and trapezoidal states (Filters.h)
    alignas(32) float m_hx[HalfbandMaxCoefs][BankLanes] = {}, m_hy[HalfbandMaxCoefs][BankLanes] = {};   ///< decimator
    alignas(32) float m_dcX[BankLanes] = {}, m_dcY[BankLanes] = {};    ///< DC blocker
    /** @} */
    /** @name Constant over a span
     *  @{ */
    alignas(32) float m_drive[BankLanes] = {};   ///< mixer drive, linear
    alignas(32) float m_norm[BankLanes] = {};    ///< level after the saturator
    alignas(32) float m_fmodel[BankLanes] = {};  ///< the filter model (FilterModel) as a number
    alignas(32) float m_pm[5][BankLanes] = {};   ///< the Xpander's pole mix: weights of the input and the four stages (the knob's)
    alignas(32) float m_dcR[BankLanes] = {};     ///< DC blocker pole
    alignas(32) float m_tbl[BankLanes] = {};     ///< 1: the lane plays its wavetable oscillators (wt1, wt2) instead
    /** @} */
    /** @name Per sample of a span (index i * BankLanes + lane)
     *  @{ */
    alignas(32) float m_dt1[BankSpan * BankLanes] = {}, m_inv1[BankSpan * BankLanes] = {};   ///< VCO 1 step at 2x, 1 / step
    alignas(32) float m_dt2[BankSpan * BankLanes] = {}, m_inv2[BankSpan * BankLanes] = {};   ///< VCO 2 step at 2x, 1 / step
    alignas(32) float m_g[BankSpan * BankLanes] = {};      ///< ladder integrator gain tan(pi fc / 2 fs)
    alignas(32) float m_gain[BankSpan * BankLanes] = {};   ///< VCA
    // 26.09.2026: what the modulation matrix moves (Modulation.h) runs per sample too.
    alignas(32) float m_wave[BankSpan * BankLanes] = {};   ///< 0 saw .. 1 pulse
    alignas(32) float m_pw[BankSpan * BankLanes] = {};     ///< pulse width
    alignas(32) float m_k[BankSpan * BankLanes] = {};      ///< the filter's feedback (FilterVoicing::feedback: k, R or the comb's)
    alignas(32) float m_fmk[BankSpan * BankLanes] = {};    ///< the pass band's makeup (FilterVoicing::makeup)
    alignas(32) float m_fmode[BankSpan * BankLanes] = {};  ///< the SEM's morph, the Polivoks' band pass, the comb's sign
    alignas(32) float m_ffm[BankSpan * BankLanes] = {};    ///< filter FM: oscillator 1 on the cutoff, in octaves at full swing / 3
    /** @} */
    /** @name Per sample at twice the rate (index (2 i + h) * BankLanes + lane): the wavetable oscillators (ModVoice)
     *  @{ */
    alignas(32) float m_wt1[2 * BankSpan * BankLanes] = {};   ///< the first wavetable oscillator, per sample at twice the rate and lane
    alignas(32) float m_wt2[2 * BankSpan * BankLanes] = {};   ///< the second
    /** @} */

    /** @name The comb filters (unaligned, last)
     *  @{ */
    static constexpr int CombLen = 4096;                                ///< the comb's line at twice the rate (down to 24 Hz)
    std::vector<float> m_comb;                                             ///< the comb filter's lines, CombLen a lane (ModVoiceBank::prepare)
    int m_combPos[BankLanes] = {};                                        ///< ... their write positions
    float m_combLp[BankLanes] = {};                                       ///< ... the damping in their loops
    /** @} */

    /** @brief Clears lane @p l's filter: its nodes, its states, its comb (a new model starts from rest). */
    void clearFilter(int l)
    {
        for (int j = 0; j < 4; ++j) m_fv[j][l] = m_fs[j][l] = 0.0f;
        if (!m_comb.empty()) std::fill(m_comb.begin() + static_cast<std::ptrdiff_t>(l) * CombLen, m_comb.begin() + static_cast<std::ptrdiff_t>(l + 1) * CombLen, 0.0f);
        m_combPos[l] = 0;
        m_combLp[l] = 0.0f;
    }
    /** @brief Clears the filters' states; the VCO phases run on. */
    void clearFilters()
    {
        for (int l = 0; l < BankLanes; ++l) {
            m_satX[l] = 0.0f; m_satS[l] = 1.0f; m_dcX[l] = m_dcY[l] = 0.0f;
            clearFilter(l);
            for (int j = 0; j < HalfbandMaxCoefs; ++j) m_hx[j][l] = m_hy[j][l] = 0.0f;
        }
    }
};

/** @brief PolyBLEP residual at phase @p t for step @p dt (1/dt = @p inv), branch-free. */
template <class VT>
EPH_FORCE_INLINE VT laneBlep(VT t, VT dt, VT inv)
{
    const VT one = lanes<VT>(1.0f), zero = lanes<VT>(0.0f);
    const VT a = t * inv;
    const VT r = vselect(vlt(t, dt), a + a - a * a - one, zero);
    const VT b = (t - one) * inv;
    return r + vselect(vgt(t, one - dt), b * b + b + b + one, zero);
}

/** @brief One VCO sample (saw blended into the inverted pulse, as Oscillator.h), then the phase steps. */
template <class VT>
EPH_FORCE_INLINE VT laneVco(VT& ph, VT dt, VT inv, VT wave, VT pw, bool pulse)
{
    const VT one = lanes<VT>(1.0f);
    const VT t = ph;
    const VT bt = laneBlep(t, dt, inv);
    VT v = lanes<VT>(2.0f) * t - one - bt;
    if (pulse) {
        VT t2 = t + one - pw;
        t2 = vselect(vge(t2, one), t2 - one, t2);
        const VT p = -(vselect(vlt(t, pw), one, -one) + bt - laneBlep(t2, dt, inv));
        v = v + wave * (p - v);
    }
    const VT np = t + dt;
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
 * @param n     samples, at most BankSpan
 * @param pulse whether any lane mixes in the pulse (decided for the whole bank, so every path runs the
 *              same arithmetic; a lane with wave 0 gets exactly its saw either way)
 * @param out   per sample and lane (index i * BankLanes + lane)
 * @param models per register of this call: the filter models its lanes use, as bits (26.09.2026: a register computes only
 *              its own models -- a lane takes its own model's output either way, so the lanes stay the same to the bit)
 */
template <class VT, int R>
void voiceKernel(VoiceLanes& s, const HalfbandDesign& hbd, int lane, int n, bool pulse, float* out, bool table,
                 const unsigned* models, bool fm = false)
{
    constexpr int width = laneWidth<VT>();
    auto at = [lane](const float* a, int r) { return loadLanes<VT>(a + lane + r * width); };
    const VT one = lanes<VT>(1.0f), half = lanes<VT>(0.5f);
    VT dcr[R], drive[R], cin[R], tb[R], model[R], pmw[R][5];
    VT ph1[R], ph2[R], sx[R], ss[R], dcx[R], dcy[R], fv[R][4], fs[R][4];
    HalfbandDown<VT> hb[R];
    for (int r = 0; r < R; ++r) {
        dcr[r] = at(s.m_dcR, r); tb[r] = at(s.m_tbl, r);
        drive[r] = at(s.m_drive, r);
        cin[r] = at(s.m_norm, r);   // the level after the saturator
        model[r] = at(s.m_fmodel, r);
        for (int j = 0; j < 5; ++j) pmw[r][j] = at(s.m_pm[j], r);
        ph1[r] = at(s.m_ph1, r); ph2[r] = at(s.m_ph2, r); sx[r] = at(s.m_satX, r); ss[r] = at(s.m_satS, r);
        dcx[r] = at(s.m_dcX, r); dcy[r] = at(s.m_dcY, r);
        for (int j = 0; j < 4; ++j) { fv[r][j] = at(s.m_fv[j], r); fs[r][j] = at(s.m_fs[j], r); }
        hb[r].m_d = hbd;
        for (int j = 0; j < hbd.m_count; ++j) { hb[r].m_x[j] = at(s.m_hx[j], r); hb[r].m_y[j] = at(s.m_hy[j], r); }
    }

    for (int i = 0; i < n; ++i) {
        const int row = i * BankLanes;
        // The knobs the modulation matrix moves, this sample's (26.09.2026).
        VT wave[R], pw[R], k[R], mode[R], fmd[R], mk[R];
        for (int r = 0; r < R; ++r) {
            wave[r] = at(s.m_wave + row, r); pw[r] = at(s.m_pw + row, r); k[r] = at(s.m_k + row, r);
            mode[r] = at(s.m_fmode + row, r); fmd[r] = at(s.m_ffm + row, r); mk[r] = at(s.m_fmk + row, r);
        }
        VT hi[2][R];
        for (int h = 0; h < 2; ++h) {
            for (int r = 0; r < R; ++r) {
                // VCOs, drive, and the saturator's fraction A / B.
                VT o1 = laneVco(ph1[r], at(s.m_dt1 + row, r), at(s.m_inv1 + row, r), wave[r], pw[r], pulse);
                VT o2 = laneVco(ph2[r], at(s.m_dt2 + row, r), at(s.m_inv2 + row, r), wave[r], pw[r], pulse);
                if (table) {
                    // A lane with a wavetable takes its own oscillators (ModVoice, Wavetable.h) in place of these.
                    const int w = (2 * i + h) * BankLanes;
                    o1 = o1 + tb[r] * (at(s.m_wt1 + w, r) - o1);
                    o2 = o2 + tb[r] * (at(s.m_wt2 + w, r) - o2);
                }
                const VT x = half * (o1 + o2) * drive[r];
                const VT sq = vsqrt(vfmadd(x, x, one));
                const VT A = x + sx[r], B = sq + ss[r];
                sx[r] = x;
                ss[r] = sq;
                // The filter (Filters.h, 26.09.2026): the saturator's output into the lane's model, each model computed
                // where a lane of the bank uses it, the lane taking its own; Newton-solved circuit equations.
                const VT xs = cin[r] * A / B;
                VT g = at(s.m_g + row, r);
                if (fm) g = g * fpow2(fmd[r] * o1);   // filter FM: oscillator 1 on the cutoff at audio rate
                VT y = lanes<VT>(0.0f);
                VT nv[4], ns[4];
                for (int j = 0; j < 4; ++j) { nv[j] = fv[r][j]; ns[j] = fs[r][j]; }
                auto take = [&](int m, VT out, const VT* tv, const VT* ts) {
                    const auto on = vge(model[r], lanes<VT>(static_cast<float>(m) - 0.5f)) & vlt(model[r], lanes<VT>(static_cast<float>(m) + 0.5f));
                    y = vselect(on, out, y);
                    for (int j = 0; j < 4; ++j) { nv[j] = vselect(on, tv[j], nv[j]); ns[j] = vselect(on, ts[j], ns[j]); }
                };
                auto runModel = [&](int m) {
                    VT tv[4], ts[4];
                    for (int j = 0; j < 4; ++j) { tv[j] = fv[r][j]; ts[j] = fs[r][j]; }
                    VT out = lanes<VT>(0.0f);
                    switch (static_cast<FilterModelE>(m)) {
                    case FilterModelE::MOOG: out = ladderMoog(tv, ts, xs, g, k[r]); break;
                    case FilterModelE::PROPHET: case FilterModelE::JUNO: case FilterModelE::XPANDER: {
                        const FilterModelE fmodel = static_cast<FilterModelE>(m);
                        out = otaCascade(tv, ts, xs, g, k[r], FilterVoicing::otaDrive(fmodel), FilterVoicing::otaRes(fmodel));
                        if (fmodel == FilterModelE::XPANDER) {
                            const float rr = FilterVoicing::otaRes(fmodel);
                            const VT a0 = xs - k[r] * ftanh(lanes<VT>(rr) * tv[3]) * lanes<VT>(1.0f / rr);
                            out = pmw[r][0] * a0 + pmw[r][1] * tv[0] + pmw[r][2] * tv[1] + pmw[r][3] * tv[2] + pmw[r][4] * tv[3];
                        }
                        break;
                    }
                    case FilterModelE::SEM: case FilterModelE::WASP:
                        out = svfNonlinear(tv, ts, xs, g, k[r], FilterVoicing::svfRange(static_cast<FilterModelE>(m)),
                                           FilterVoicing::svfAsym(static_cast<FilterModelE>(m)), mode[r]);
                        break;
                    case FilterModelE::POLIVOKS:
                        out = svfNonlinear(tv, ts, xs, g, k[r], FilterVoicing::svfRange(FilterModelE::POLIVOKS), 0.0f, mode[r], true);
                        break;
                    case FilterModelE::DIODE: out = diodeLadder(tv, ts, xs, g * lanes<VT>(0.70710678f), k[r]); break;
                    case FilterModelE::KORG35: out = korg35(tv, ts, xs, g, k[r]); break;
                    case FilterModelE::COMB: {
                        // Lane by lane: a line tuned to the cutoff (at twice the rate), its feedback damped by a pole.
                        alignas(32) float xin[8], gg[8], res[8];
                        vstore(xin, xs); vstore(gg, g);
                        for (int w = 0; w < width; ++w) {
                            const int L = lane + r * width + w;
                            if (s.m_fmodel[L] < 8.5f || s.m_comb.empty()) { res[w] = 0.0f; continue; }
                            const float period = std::min(static_cast<float>(VoiceLanes::CombLen - 2), 3.14159265f / std::atan(std::max(gg[w], 1e-4f)));
                            float rp = static_cast<float>(s.m_combPos[L]) - period;
                            if (rp < 0.0f) rp += static_cast<float>(VoiceLanes::CombLen);
                            const int i0 = static_cast<int>(rp);
                            const float fr = rp - static_cast<float>(i0);
                            float* line = s.m_comb.data() + static_cast<size_t>(L) * VoiceLanes::CombLen;
                            const float d = line[i0] + fr * (line[(i0 + 1) & (VoiceLanes::CombLen - 1)] - line[i0]);
                            s.m_combLp[L] += 0.5f * (d - s.m_combLp[L]);
                            const float fb = (s.m_fmode[row + L] >= 0.5f ? -1.0f : 1.0f) * s.m_k[row + L];
                            const float yv = xin[w] + fb * s.m_combLp[L];
                            line[s.m_combPos[L]] = yv;
                            s.m_combPos[L] = (s.m_combPos[L] + 1) & (VoiceLanes::CombLen - 1);
                            res[w] = 0.5f * yv;
                        }
                        out = loadLanes<VT>(res);
                        break;
                    }
                    default: break;
                    }
                    take(m, out, tv, ts);
                };
                for (int m = 0; m < FilterModels; ++m) if (models[r] & (1u << m)) runModel(m);
                for (int j = 0; j < 4; ++j) { fv[r][j] = nv[j]; fs[r][j] = ns[j]; }
                hi[h][r] = y * mk[r];
            }
        }
        for (int r = 0; r < R; ++r) {
            const VT d = hb[r].process(hi[0][r], hi[1][r]);
            const VT dc = d - dcx[r] + dcr[r] * dcy[r];
            dcx[r] = d;
            dcy[r] = dc;
            vstore(out + row + lane + r * width, dc * at(s.m_gain + row, r));
        }
    }

    for (int r = 0; r < R; ++r) {
        auto put = [lane, r](float* a, VT v) { vstore(a + lane + r * width, v); };
        put(s.m_ph1, ph1[r]); put(s.m_ph2, ph2[r]); put(s.m_satX, sx[r]); put(s.m_satS, ss[r]); put(s.m_dcX, dcx[r]); put(s.m_dcY, dcy[r]);
        for (int j = 0; j < 4; ++j) { put(s.m_fv[j], fv[r][j]); put(s.m_fs[j], fs[r][j]); }
        for (int j = 0; j < hbd.m_count; ++j) { put(s.m_hx[j], hb[r].m_x[j]); put(s.m_hy[j], hb[r].m_y[j]); }
    }
}

} // namespace eph
