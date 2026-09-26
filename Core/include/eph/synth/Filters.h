/**
 * @file Filters.h
 * @brief The filter models (26.09.2026): the classic voltage-controlled filters as circuit models, solved sample by
 *        sample -- templates for one lane (float) and for the voice bank's registers (VecF), so both compute the same.
 *
 * **Method.** Every model is the circuit's ordinary differential equation with its nonlinearities where the circuit
 * has them -- the transistor differential pairs of a ladder (tanh), the OTAs of a cascade (tanh of their differential
 * input), the diode pairs of a diode ladder, the diode limiter in a Sallen-Key's feedback, the slew-limited op-amps of
 * the Polivoks, the CMOS inverters of the Wasp. The capacitors are integrated by the trapezoidal rule in the form of
 * the topology-preserving transform (Zavalishin, "The Art of VA Filter Design"; Simper's equivalent capacitor
 * currents): with g = tan(pi fc / fs) each state obeys v = s + g f(v), and after the step s <- 2 v - s. The implicit
 * equation is solved every sample with Newton-Raphson on the analytic Jacobian, warm-started from the last sample --
 * the delay-free loop resolved exactly rather than broken by a unit delay (D'Angelo and Välimäki 2014 for the Moog
 * ladder; Holters and Zölzer 2015 for the general nonlinear state-space form). The Jacobians are solved by their
 * structure: a cascade is bidiagonal with the resonance feedback in one corner (forward substitution and one scalar
 * division), the diode ladder tridiagonal with that corner (Thomas and Sherman-Morrison). A fixed number of iterations
 * (kNewton) keeps every lane path bit for bit the same and the sound independent of the host's blocks; the voices run
 * at twice the rate, which keeps the aliasing of the nonlinear stages low.
 *
 * **The models** (FilterModel): the Moog transistor ladder (Huovilainen's differential-pair model), the OTA cascades of
 * the Prophet (SSM2040, its reissue SSI2140, CEM3320, the Microwave's CEM3389) and of the Juno and Jupiter (IR3109), the
 * Oberheim SEM's state-variable filter morphing from low pass through notch to high pass, the Xpander's and Matrix-12's
 * pole mixing on the cascade (eight responses), the diode ladder of the TB-303 and the EMS (Stinchcombe's analysis:
 * the last node's capacitor half, self-oscillation at a loop gain of 17 and sqrt 2 times the cutoff), the Korg35 of the
 * MS-20 (a Sallen-Key low pass whose feedback runs through a diode limiter), the Polivoks (integrators whose op-amps
 * are slew-limited on purpose), the EDP Wasp (CMOS inverters as integrators, asymmetric), and a comb filter.
 */
#pragma once
#include "eph/Vec.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#ifndef EPH_FORCE_INLINE
  #if defined(_MSC_VER)
    #define EPH_FORCE_INLINE __forceinline
  #else
    #define EPH_FORCE_INLINE inline __attribute__((always_inline))
  #endif
#endif

namespace eph {

/** @brief The filter models, in the order of the `filter` parameter (Params.cpp kFilterNames). */
enum class FilterModel : int { Moog = 0, Prophet, Juno, Sem, Xpander, Diode, Korg35, Polivoks, Wasp, Comb, Count };
constexpr int kFilterModels = static_cast<int>(FilterModel::Count);

#ifndef EPH_FILTER_NEWTON
#define EPH_FILTER_NEWTON 3
#endif
constexpr int kNewton = EPH_FILTER_NEWTON;   ///< Newton iterations per sample (fixed: the same on every lane path)

/** @brief tanh by Lambert's continued fraction (7th order), exact to 1e-7 inside +-4.97 and held there. */
template <class V>
EPH_FORCE_INLINE V ftanh(V x)
{
    const V c = lanes<V>(4.97f);
    x = vmax(vmin(x, c), lanes<V>(-4.97f));
    const V x2 = x * x;
    const V num = x * vfmadd(x2, vfmadd(x2, vfmadd(x2, lanes<V>(1.0f), lanes<V>(378.0f)), lanes<V>(17325.0f)), lanes<V>(135135.0f));
    const V den = vfmadd(x2, vfmadd(x2, vfmadd(x2, lanes<V>(28.0f), lanes<V>(3150.0f)), lanes<V>(62370.0f)), lanes<V>(135135.0f));
    return num / den;
}

/** @brief 2^x for |x| <= 3 (a cubic, never under 1/20): the audio-rate filter FM. */
template <class V>
EPH_FORCE_INLINE V fpow2(V x)
{
    x = vmax(vmin(x, lanes<V>(3.0f)), lanes<V>(-3.0f));
    const V p = vfmadd(x, vfmadd(x, vfmadd(x, lanes<V>(0.0555041f), lanes<V>(0.2402265f)), lanes<V>(0.6931472f)), lanes<V>(1.0f));
    return vmax(p, lanes<V>(0.05f));
}

/**
 * @brief The Moog transistor ladder (Huovilainen 2004; solved implicitly as D'Angelo and Välimäki 2014):
 *        v0' = w (tanh(x - k v3) - tanh v0), vi' = w (tanh v(i-1) - tanh vi).
 * @param v  the four stage voltages (in: the last sample's, the Newton start; out: this sample's)
 * @param s  the four trapezoidal states
 * @param x  input; @param g tan(pi fc / fs); @param k resonance feedback (4: self-oscillation)
 * @return   the fourth stage
 */
template <class V>
EPH_FORCE_INLINE V ladderMoog(V* v, V* s, V x, V g, V k)
{
    const V one = lanes<V>(1.0f);
    for (int it = 0; it < kNewton; ++it) {
        const V tx = ftanh(x - k * v[3]);
        const V t0 = ftanh(v[0]), t1 = ftanh(v[1]), t2 = ftanh(v[2]), t3 = ftanh(v[3]);
        const V F0 = v[0] - s[0] - g * (tx - t0), F1 = v[1] - s[1] - g * (t0 - t1);
        const V F2 = v[2] - s[2] - g * (t1 - t2), F3 = v[3] - s[3] - g * (t2 - t3);
        const V d0 = one - t0 * t0, d1 = one - t1 * t1, d2 = one - t2 * t2, d3 = one - t3 * t3, dx = one - tx * tx;
        const V J11 = vfmadd(g, d1, one), J22 = vfmadd(g, d2, one), J33 = vfmadd(g, d3, one);
        const V a1 = -F1 / J11, b1 = g * d0 / J11;
        const V a2 = vfmadd(g * d1, a1, -F2) / J22, b2 = g * d1 * b1 / J22;
        const V a3 = vfmadd(g * d2, a2, -F3) / J33, b3 = g * d2 * b2 / J33;
        const V J00 = vfmadd(g, d0, one), J03 = g * dx * k;
        const V D0 = vfnmadd(J03, a3, -F0) / vfmadd(J03, b3, J00);
        v[0] = v[0] + D0;
        v[1] = v[1] + vfmadd(b1, D0, a1);
        v[2] = v[2] + vfmadd(b2, D0, a2);
        v[3] = v[3] + vfmadd(b3, D0, a3);
    }
    for (int i = 0; i < 4; ++i) s[i] = v[i] + v[i] - s[i];
    return v[3];
}

/**
 * @brief An OTA cascade (the Prophet's SSM2040 / CEM3320, the Juno's IR3109): four buffered OTA stages,
 *        vi' = w tanh(d (ai - vi)) / d with a0 = x - k tanh(r v3) / r and ai = v(i-1); @p d the stages' drive (their
 *        saturation), @p r the resonance VCA's. With @p mix the Xpander's pole mixing (0..7: LP4 LP2 BP2 BP4 HP2 HP4
 *        notch phaser; the cascade's input a0 and its four stages), else the fourth stage.
 */
template <class V>
EPH_FORCE_INLINE V otaCascade(V* v, V* s, V x, V g, V k, float d, float r, int mix = 0)
{
    const V one = lanes<V>(1.0f), D = lanes<V>(d), Di = lanes<V>(1.0f / d), R = lanes<V>(r), Ri = lanes<V>(1.0f / r);
    V a0 = x;
    for (int it = 0; it < kNewton; ++it) {
        const V tr = ftanh(R * v[3]);
        a0 = x - k * tr * Ri;
        const V e0 = ftanh(D * (a0 - v[0])), e1 = ftanh(D * (v[0] - v[1])), e2 = ftanh(D * (v[1] - v[2])), e3 = ftanh(D * (v[2] - v[3]));
        const V F0 = v[0] - s[0] - g * e0 * Di, F1 = v[1] - s[1] - g * e1 * Di;
        const V F2 = v[2] - s[2] - g * e2 * Di, F3 = v[3] - s[3] - g * e3 * Di;
        const V q0 = g * (one - e0 * e0), q1 = g * (one - e1 * e1), q2 = g * (one - e2 * e2), q3 = g * (one - e3 * e3);
        const V J11 = one + q1, J22 = one + q2, J33 = one + q3;
        const V a1 = -F1 / J11, b1 = q1 / J11;
        const V a2 = vfmadd(q2, a1, -F2) / J22, b2 = q2 * b1 / J22;
        const V a3 = vfmadd(q3, a2, -F3) / J33, b3 = q3 * b2 / J33;
        const V J00 = one + q0, J03 = q0 * k * (one - tr * tr);
        const V D0 = vfnmadd(J03, a3, -F0) / vfmadd(J03, b3, J00);
        v[0] = v[0] + D0;
        v[1] = v[1] + vfmadd(b1, D0, a1);
        v[2] = v[2] + vfmadd(b2, D0, a2);
        v[3] = v[3] + vfmadd(b3, D0, a3);
    }
    for (int i = 0; i < 4; ++i) s[i] = v[i] + v[i] - s[i];
    // The pole mixing of the Xpander and the Matrix-12: responses as sums of the cascade's taps (per stage 1/(1+s)).
    const V two = lanes<V>(2.0f), four = lanes<V>(4.0f), six = lanes<V>(6.0f);
    switch (mix) {
    case 1: return v[1];                                                          // LP2
    case 2: return two * (v[0] - v[1]);                                           // BP2
    case 3: return four * (v[1] - two * v[2] + v[3]);                             // BP4
    case 4: return a0 - two * v[0] + v[1];                                        // HP2
    case 5: return a0 - four * v[0] + six * v[1] - four * v[2] + v[3];            // HP4
    case 6: return a0 - two * v[0] + two * v[1];                                  // notch
    case 7: return a0 - four * v[0] + four * v[1];                                // phaser (two all-passes)
    default: return v[3];                                                         // LP4
    }
}

/**
 * @brief A state-variable filter with nonlinear integrators: v0 = band pass, v1 = low pass, h = x - 2 R v0 - v1,
 *        v0' = w N(h), v1' = w N(v0), N(e) = L tanh(e / L + a) - L tanh(a). The SEM's OTAs (L large, a = 0), the
 *        Polivoks' slew-limited op-amps (L small), the Wasp's CMOS inverters (asymmetric: a > 0). The damping grows with
 *        the band pass's swing, as the circuits' resonance paths saturate (h takes - (v0 - 1.5 tanh(v0 / 1.5)) more):
 *        nothing changes in the small-signal range, and a loud input at full resonance stays within a few times itself.
 *        @p morph moves the output from low pass through notch to high pass (the SEM's knob), or low pass to band pass
 *        (@p toBand).
 */
template <class V>
EPH_FORCE_INLINE V svfNonlinear(V* v, V* s, V x, V g, V R, float L, float a, V morph, bool toBand = false)
{
    const V one = lanes<V>(1.0f), Lv = lanes<V>(L), Li = lanes<V>(1.0f / L), A = lanes<V>(a);
    const V off = Lv * ftanh(A);
    const V two = lanes<V>(2.0f);
    const V lb = lanes<V>(1.5f), lbi = lanes<V>(1.0f / 1.5f);
    auto damp = [&](V b) { return two * R * b + b - lb * ftanh(b * lbi); };
    for (int it = 0; it < kNewton; ++it) {
        const V tq = ftanh(v[0] * lbi);
        const V h = x - damp(v[0]) - v[1];
        const V th = ftanh(vfmadd(h, Li, A)), tb = ftanh(vfmadd(v[0], Li, A));
        const V F0 = v[0] - s[0] - g * (Lv * th - off), F1 = v[1] - s[1] - g * (Lv * tb - off);
        const V nh = g * (one - th * th), nb = g * (one - tb * tb);
        const V dh = vfmadd(tq, tq, two * R);                        // -dh/dv0: 2R + tanh^2(v0 / 1.5)
        const V det = vfmadd(nh, dh + nb, one);                      // 1 + g N'(h) (2R + ...) + g^2 N'(h) N'(v0)
        const V D0 = vfmadd(F1, nh, -F0) / det;
        v[0] = v[0] + D0;
        v[1] = v[1] + vfmadd(nb, D0, -F1);
    }
    s[0] = v[0] + v[0] - s[0];
    s[1] = v[1] + v[1] - s[1];
    const V hp = x - damp(v[0]) - v[1];
    if (toBand) return v[1] + morph * (v[0] - v[1]);
    // Low pass -> notch (lp + hp) -> high pass.
    const V half = lanes<V>(0.5f);
    const V m2 = morph + morph;
    return vselect(vlt(morph, half), v[1] + m2 * hp, (two - m2) * v[1] + hp);
}

/**
 * @brief The diode ladder (TB-303, EMS; Stinchcombe): four capacitor nodes coupled through diode pairs, the last one
 *        of half the capacitance, u = x - k v3:
 *        v0' = w (T(u - v0) - T(v0 - v1)), v1' = w (T(v0 - v1) - T(v1 - v2)), v2' = w (T(v1 - v2) - T(v2 - v3)),
 *        v3' = 2 w T(v2 - v3). Linear, it oscillates at k = 17 and sqrt 2 times w: the caller passes w / sqrt 2.
 */
template <class V>
EPH_FORCE_INLINE V diodeLadder(V* v, V* s, V x, V g, V k)
{
    const V one = lanes<V>(1.0f), g2 = g + g;
    for (int it = 0; it < kNewton; ++it) {
        const V u = x - k * v[3];
        const V ti = ftanh(u - v[0]), t01 = ftanh(v[0] - v[1]), t12 = ftanh(v[1] - v[2]), t23 = ftanh(v[2] - v[3]);
        const V r0 = -(v[0] - s[0] - g * (ti - t01)), r1 = -(v[1] - s[1] - g * (t01 - t12));
        const V r2 = -(v[2] - s[2] - g * (t12 - t23)), r3 = -(v[3] - s[3] - g2 * t23);
        const V di = one - ti * ti, d01 = one - t01 * t01, d12 = one - t12 * t12, d23 = one - t23 * t23;
        // The tridiagonal part.
        const V T00 = vfmadd(g, di + d01, one), T01 = -g * d01;
        const V T10 = T01, T11 = vfmadd(g, d01 + d12, one), T12 = -g * d12;
        const V T21 = T12, T22 = vfmadd(g, d12 + d23, one), T23 = -g * d23;
        const V T32 = -g2 * d23, T33 = vfmadd(g2, d23, one);
        const V J03 = g * di * k;   // the corner: the feedback into the first node
        // Thomas, for the residual and for the unit vector e0 at once.
        const V c0 = T01 / T00, x0 = r0 / T00, z0 = one / T00;
        const V m1 = vfnmadd(T10, c0, T11);
        const V c1 = T12 / m1, x1 = vfnmadd(T10, x0, r1) / m1, z1 = -(T10 * z0) / m1;
        const V m2 = vfnmadd(T21, c1, T22);
        const V c2 = T23 / m2, x2 = vfnmadd(T21, x1, r2) / m2, z2 = -(T21 * z1) / m2;
        const V m3 = vfnmadd(T32, c2, T33);
        const V X3 = vfnmadd(T32, x2, r3) / m3, Z3 = -(T32 * z2) / m3;
        const V X2 = vfnmadd(c2, X3, x2), Z2 = vfnmadd(c2, Z3, z2);
        const V X1 = vfnmadd(c1, X2, x1), Z1 = vfnmadd(c1, Z2, z1);
        const V X0 = vfnmadd(c0, X1, x0), Z0 = vfnmadd(c0, Z1, z0);
        // Sherman-Morrison: J = T + J03 e0 e3^T.
        const V f = J03 * X3 / vfmadd(J03, Z3, one);
        v[0] = v[0] + vfnmadd(f, Z0, X0);
        v[1] = v[1] + vfnmadd(f, Z1, X1);
        v[2] = v[2] + vfnmadd(f, Z2, X2);
        v[3] = v[3] + vfnmadd(f, Z3, X3);
    }
    for (int i = 0; i < 4; ++i) s[i] = v[i] + v[i] - s[i];
    return v[3];
}

/**
 * @brief The Korg35 of the MS-20 (Stinchcombe): a Sallen-Key low pass, x -R- A -R- B, C2 from B to ground, C1 from A
 *        to the output, which is the gain K times B through the diode limiter D = tanh. With q0 the voltage on C1 and
 *        q1 = B: A = q0 + D(K q1), q0' = w (x - 2A + q1), q1' = w (A - q1). Linear, it oscillates at K = 3.
 */
template <class V>
EPH_FORCE_INLINE V korg35(V* v, V* s, V x, V g, V K)
{
    const V one = lanes<V>(1.0f), two = lanes<V>(2.0f);
    for (int it = 0; it < kNewton; ++it) {
        const V dq = ftanh(K * v[1]);
        const V A = v[0] + dq;
        const V F0 = v[0] - s[0] - g * (x - two * A + v[1]), F1 = v[1] - s[1] - g * (A - v[1]);
        const V kd = K * (one - dq * dq);                            // dA / dq1
        const V J00 = vfmadd(two, g, one), J01 = g * (two * kd - one), J10 = -g, J11 = one + g - g * kd;
        const V det = J00 * J11 - J01 * J10;
        const V D0 = (J01 * F1 - J11 * F0) / det, D1 = (J10 * F0 - J00 * F1) / det;
        v[0] = v[0] + D0;
        v[1] = v[1] + D1;
    }
    s[0] = v[0] + v[0] - s[0];
    s[1] = v[1] + v[1] - s[1];
    return v[1];
}

/** @brief How a model reads the voice's resonance (0..1) and where its output sits: feedback, damping, gain. */
struct FilterVoicing {
    /** @brief The OTA cascades' stage drive and resonance-VCA saturation: the Prophet's feedback saturates before its
     *         stages (its self-oscillation stays near the cutoff), the Juno's cleaner stages drive less. */
    static float otaDrive(FilterModel m) { return m == FilterModel::Juno ? 0.45f : 0.7f; }
    static float otaRes(FilterModel m) { return m == FilterModel::Juno ? 1.2f : 1.6f; }   ///< @copydoc otaDrive
    /** @brief The state-variable filters' nonlinearity: the integrators' range L and asymmetry a (SEM, Polivoks, Wasp). */
    static float svfRange(FilterModel m) { return m == FilterModel::Polivoks ? 0.35f : (m == FilterModel::Wasp ? 0.6f : 1.5f); }
    static float svfAsym(FilterModel m) { return m == FilterModel::Wasp ? 0.35f : 0.0f; }   ///< @copydoc svfRange
    /** @brief Resonance 0..1 as the model's feedback: 4 for the ladders and cascades, 17 for the diode ladder, 3 for
     *         the Korg35 (their self-oscillation just at 1); for the state-variable filters the damping R (Q = 1 / 2R). */
    static float feedback(FilterModel m, float res)
    {
        switch (m) {
        case FilterModel::Moog: return 4.0f * res * 0.985f;
        case FilterModel::Prophet: case FilterModel::Juno: case FilterModel::Xpander: return 4.1f * res;
        case FilterModel::Diode: return 17.5f * res;
        case FilterModel::Korg35: return 3.05f * res;
        case FilterModel::Sem: return 0.707f * (1.0f - res) + 0.012f;
        case FilterModel::Polivoks: case FilterModel::Wasp: return 0.6f * (1.0f - res) * (1.0f - res) + 0.004f;
        default: return 0.97f * res;   // the comb's feedback
        }
    }
    /** @brief The pass band's loss by the feedback, partly made good (the ladders and cascades lose 1 / (1 + k)). */
    static float makeup(FilterModel m, float k)
    {
        switch (m) {
        case FilterModel::Moog: return 1.0f + 0.5f * k;
        case FilterModel::Prophet: case FilterModel::Xpander: return 1.0f + 0.5f * k;
        case FilterModel::Juno: return 1.0f + 0.25f * k;
        case FilterModel::Diode: return 1.0f + 0.3f * k;
        default: return 1.0f;
        }
    }
};

/** @brief The Xpander's pole mixes (FilterModel::Xpander): weights of the cascade's input and its four stages. */
inline const float* poleMix(float mode)
{
    static const float kMix[8][5] = { { 0, 0, 0, 0, 1 }, { 0, 0, 1, 0, 0 }, { 0, 2, -2, 0, 0 }, { 0, 0, 4, -8, 4 },
                                      { 1, -2, 1, 0, 0 }, { 1, -4, 6, -4, 1 }, { 1, -2, 2, 0, 0 }, { 1, -4, 4, 0, 0 } };
    return kMix[std::clamp(static_cast<int>(std::lround(mode * 7.0f)), 0, 7)];
}

/**
 * @brief One filter of any model on one lane (the pad synth's; the voice bank's kernel runs the same models in its
 *        registers): its nodes, its states and, for the comb, its line.
 */
struct FilterLane {
    float v[4] = {}, s[4] = {};
    std::vector<float> line;   ///< the comb's line (allocate(); a power of two)
    int pos = 0;
    float damp = 0.0f;
    void allocate(int length) { line.assign(static_cast<size_t>(length), 0.0f); pos = 0; }   ///< not on the audio thread
    void clear() { for (int i = 0; i < 4; ++i) v[i] = s[i] = 0.0f; std::fill(line.begin(), line.end(), 0.0f); damp = 0.0f; }
    /**
     * @brief One sample through model @p m: @p g tan(pi fc / fs), @p k the model's feedback (FilterVoicing::feedback),
     *        @p mode its mode; the pass band's makeup applied.
     */
    float tick(FilterModel m, float x, float g, float k, float mode)
    {
        float y = 0.0f;
        switch (m) {
        case FilterModel::Moog: y = ladderMoog<float>(v, s, x, g, k); break;
        case FilterModel::Prophet: case FilterModel::Juno:
            y = otaCascade<float>(v, s, x, g, k, FilterVoicing::otaDrive(m), FilterVoicing::otaRes(m));
            break;
        case FilterModel::Xpander: {
            otaCascade<float>(v, s, x, g, k, FilterVoicing::otaDrive(m), FilterVoicing::otaRes(m));
            const float r = FilterVoicing::otaRes(m);
            const float a0 = x - k * ftanh<float>(r * v[3]) / r;
            const float* w = poleMix(mode);
            y = w[0] * a0 + w[1] * v[0] + w[2] * v[1] + w[3] * v[2] + w[4] * v[3];
            break;
        }
        case FilterModel::Sem: case FilterModel::Wasp:
            y = svfNonlinear<float>(v, s, x, g, k, FilterVoicing::svfRange(m), FilterVoicing::svfAsym(m), mode);
            break;
        case FilterModel::Polivoks: y = svfNonlinear<float>(v, s, x, g, k, FilterVoicing::svfRange(m), 0.0f, mode, true); break;
        case FilterModel::Diode: y = diodeLadder<float>(v, s, x, g * 0.70710678f, k); break;
        case FilterModel::Korg35: y = korg35<float>(v, s, x, g, k); break;
        case FilterModel::Comb: {
            if (line.empty()) return 0.0f;
            const int len = static_cast<int>(line.size());
            const float period = std::min(static_cast<float>(len - 2), 3.14159265f / std::atan(std::max(g, 1e-4f)));
            float rp = static_cast<float>(pos) - period;
            if (rp < 0.0f) rp += static_cast<float>(len);
            const int i0 = static_cast<int>(rp);
            const float d = line[static_cast<size_t>(i0)] + (rp - static_cast<float>(i0)) * (line[static_cast<size_t>((i0 + 1) & (len - 1))] - line[static_cast<size_t>(i0)]);
            damp += 0.5f * (d - damp);
            const float yv = x + (mode >= 0.5f ? -k : k) * damp;
            line[static_cast<size_t>(pos)] = yv;
            pos = (pos + 1) & (len - 1);
            y = 0.5f * yv;
            break;
        }
        default: break;
        }
        return y * FilterVoicing::makeup(m, k);
    }
};

} // namespace eph
