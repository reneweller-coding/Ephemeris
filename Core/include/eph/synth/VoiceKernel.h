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
    alignas(32) float ls[4][kBankLanes] = {}, ly[4][kBankLanes] = {}, lu[kBankLanes] = {};   ///< ladder: integrators, stage outputs, input stage
    alignas(32) float hx[kHalfbandMaxCoefs][kBankLanes] = {}, hy[kHalfbandMaxCoefs][kBankLanes] = {};   ///< decimator
    alignas(32) float dcX[kBankLanes] = {}, dcY[kBankLanes] = {};    ///< DC blocker
    /** @} */
    /** @name Constant over a span
     *  @{ */
    alignas(32) float wave[kBankLanes] = {};    ///< 0 saw .. 1 pulse
    alignas(32) float pw[kBankLanes] = {};      ///< pulse width
    alignas(32) float drive[kBankLanes] = {};   ///< mixer drive, linear
    alignas(32) float norm[kBankLanes] = {};    ///< level after the saturator
    alignas(32) float k[kBankLanes] = {};       ///< ladder feedback
    alignas(32) float dcR[kBankLanes] = {};     ///< DC blocker pole
    /** @} */
    /** @name Per sample of a span (index i * kBankLanes + lane)
     *  @{ */
    alignas(32) float dt1[kBankSpan * kBankLanes] = {}, inv1[kBankSpan * kBankLanes] = {};   ///< VCO 1 step at 2x, 1 / step
    alignas(32) float dt2[kBankSpan * kBankLanes] = {}, inv2[kBankSpan * kBankLanes] = {};   ///< VCO 2 step at 2x, 1 / step
    alignas(32) float g[kBankSpan * kBankLanes] = {};      ///< ladder integrator gain tan(pi fc / 2 fs)
    alignas(32) float gain[kBankSpan * kBankLanes] = {};   ///< VCA
    /** @} */

    /** @brief Clears the filters' states; the VCO phases run on. */
    void clearFilters()
    {
        for (int l = 0; l < kBankLanes; ++l) {
            satX[l] = 0.0f; satS[l] = 1.0f; lu[l] = 0.0f; dcX[l] = dcY[l] = 0.0f;
            for (int j = 0; j < 4; ++j) ls[j][l] = ly[j][l] = 0.0f;
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
void voiceKernel(VoiceLanes& s, const HalfbandDesign& hbd, int lane, int n, bool pulse, float* out)
{
    constexpr int width = laneWidth<V>();
    auto at = [lane](const float* a, int r) { return loadLanes<V>(a + lane + r * width); };
    const V one = lanes<V>(1.0f), half = lanes<V>(0.5f), two = lanes<V>(2.0f), comp = lanes<V>(0.5f);
    V dcr[R], wave[R], pw[R], drive[R], k[R], cin[R];
    V ph1[R], ph2[R], sx[R], ss[R], dcx[R], dcy[R], ls[R][4], ly[R][4], lu[R];
    HalfbandDown<V> hb[R];
    for (int r = 0; r < R; ++r) {
        dcr[r] = at(s.dcR, r); wave[r] = at(s.wave, r); pw[r] = at(s.pw, r);
        drive[r] = at(s.drive, r); k[r] = at(s.k, r);
        cin[r] = at(s.norm, r) * vfmadd(comp, k[r], one);   // the level after the saturator times the ladder's input gain
        ph1[r] = at(s.ph1, r); ph2[r] = at(s.ph2, r); sx[r] = at(s.satX, r); ss[r] = at(s.satS, r);
        dcx[r] = at(s.dcX, r); dcy[r] = at(s.dcY, r);
        for (int j = 0; j < 4; ++j) { ls[r][j] = at(s.ls[j], r); ly[r][j] = at(s.ly[j], r); }
        lu[r] = at(s.lu, r);
        hb[r].d = hbd;
        for (int j = 0; j < hbd.count; ++j) { hb[r].x[j] = at(s.hx[j], r); hb[r].y[j] = at(s.hy[j], r); }
    }

    for (int i = 0; i < n; ++i) {
        const int row = i * kBankLanes;
        V hi[2][R];
        for (int h = 0; h < 2; ++h) {
            for (int r = 0; r < R; ++r) {
                // VCOs, drive, and the saturator's fraction A / B.
                const V o1 = laneVco(ph1[r], at(s.dt1 + row, r), at(s.inv1 + row, r), wave[r], pw[r], pulse);
                const V o2 = laneVco(ph2[r], at(s.dt2 + row, r), at(s.inv2 + row, r), wave[r], pw[r], pulse);
                const V x = half * (o1 + o2) * drive[r];
                const V sq = vsqrt(vfmadd(x, x, one));
                const V A = x + sx[r], B = sq + ss[r];
                sx[r] = x;
                ss[r] = sq;
                // The ladder (see the file comment): S_j from the previous sample's values.
                const V g = at(s.g + row, r);
                const V S0 = vsqrt(vfmadd(lu[r], lu[r], one));
                const V S1 = vsqrt(vfmadd(ly[r][0], ly[r][0], one)), S2 = vsqrt(vfmadd(ly[r][1], ly[r][1], one));
                const V S3 = vsqrt(vfmadd(ly[r][2], ly[r][2], one)), S4 = vsqrt(vfmadd(ly[r][3], ly[r][3], one));
                const V q1 = one / (S0 * (S1 + g)), q2 = one / (S1 * (S2 + g));
                const V q3 = one / (S2 * (S3 + g)), q4 = one / (S3 * (S4 + g));
                const V a1 = g * S1 * q1, a2 = g * S2 * q2, a3 = g * S3 * q3, a4 = g * S4 * q4;
                const V b1 = ls[r][0] * S1 * (S0 * q1), b2 = ls[r][1] * S2 * (S1 * q2);
                const V b3 = ls[r][2] * S3 * (S2 * q3), b4 = ls[r][3] * S4 * (S3 * q4);
                const V a43 = a4 * a3, a432 = a43 * a2;
                const V P = a432 * a1;
                const V Q = vfmadd(a432, b1, vfmadd(a43, b2, vfmadd(a4, b3, b4)));
                const V un = (A * cin[r] - B * k[r] * Q) / (B * vfmadd(k[r], P, one));
                const V y1 = vfmadd(a1, un, b1);
                const V y2 = vfmadd(a2, y1, b2);
                const V y3 = vfmadd(a3, y2, b3);
                const V y4 = vfmadd(a4, y3, b4);
                ls[r][0] = two * y1 - ls[r][0];
                ls[r][1] = two * y2 - ls[r][1];
                ls[r][2] = two * y3 - ls[r][2];
                ls[r][3] = two * y4 - ls[r][3];
                ly[r][0] = y1; ly[r][1] = y2; ly[r][2] = y3; ly[r][3] = y4;
                lu[r] = un;
                hi[h][r] = y4;
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
        for (int j = 0; j < 4; ++j) { put(s.ls[j], ls[r][j]); put(s.ly[j], ly[r][j]); }
        put(s.lu, lu[r]);
        for (int j = 0; j < hbd.count; ++j) { put(s.hx[j], hb[r].x[j]); put(s.hy[j], hb[r].y[j]); }
    }
}

} // namespace eph
