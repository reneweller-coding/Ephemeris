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
 * division), the diode ladder tridiagonal with that corner (Thomas and Sherman-Morrison). The pivots' reciprocals are
 * taken first, side by side, and the substitution multiplies (26.09.2026, after Noctuary's port of these filters): the
 * divider, which bounds the lanes, does four divisions a step instead of seven (the ladders and cascades) or twelve
 * (the diode ladder). A fixed number of iterations
 * (Newton) keeps every lane path bit for bit the same and the sound independent of the host's blocks; the voices run
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
    /** @brief Inlines a function whatever the compiler would decide (the kernels' inner steps). */
    #define EPH_FORCE_INLINE inline __attribute__((always_inline))
  #endif
#endif

namespace eph {

/** @brief The filter models, in the order of the `filter` parameter (Params.cpp FilterNames). */
enum class FilterModelE : int { MOOG = 0, PROPHET, JUNO, SEM, XPANDER, DIODE, KORG35, POLIVOKS, WASP, COMB, COUNT };
constexpr int FilterModels = static_cast<int>(FilterModelE::COUNT);   ///< how many filter models there are

#ifndef EPH_FILTER_NEWTON
/** @brief Newton steps per sample of the nonlinear models, unless the build sets another number. */
#define EPH_FILTER_NEWTON 3
#endif
constexpr int Newton = EPH_FILTER_NEWTON;   ///< Newton iterations per sample (fixed: the same on every lane path)

/** @brief tanh by Lambert's continued fraction (7th order), exact to 1e-7 inside +-4.97 and held there. */
template <class VT>
EPH_FORCE_INLINE VT ftanh(VT x)
{
    const VT c = lanes<VT>(4.97f);
    x = vmax(vmin(x, c), lanes<VT>(-4.97f));
    const VT x2 = x * x;
    const VT num = x * vfmadd(x2, vfmadd(x2, vfmadd(x2, lanes<VT>(1.0f), lanes<VT>(378.0f)), lanes<VT>(17325.0f)), lanes<VT>(135135.0f));
    const VT den = vfmadd(x2, vfmadd(x2, vfmadd(x2, lanes<VT>(28.0f), lanes<VT>(3150.0f)), lanes<VT>(62370.0f)), lanes<VT>(135135.0f));
    return num / den;
}

/** @brief 2^x for |x| <= 3 (a cubic, never under 1/20): the audio-rate filter FM. */
template <class VT>
EPH_FORCE_INLINE VT fpow2(VT x)
{
    x = vmax(vmin(x, lanes<VT>(3.0f)), lanes<VT>(-3.0f));
    const VT p = vfmadd(x, vfmadd(x, vfmadd(x, lanes<VT>(0.0555041f), lanes<VT>(0.2402265f)), lanes<VT>(0.6931472f)), lanes<VT>(1.0f));
    return vmax(p, lanes<VT>(0.05f));
}

/**
 * @brief The Moog transistor ladder (Huovilainen 2004; solved implicitly as D'Angelo and Välimäki 2014):
 *        v0' = w (tanh(x - k v3) - tanh v0), vi' = w (tanh v(i-1) - tanh vi).
 * @param v  the four stage voltages (in: the last sample's, the Newton start; out: this sample's)
 * @param s  the four trapezoidal states
 * @param x  input; @param g tan(pi fc / fs); @param k resonance feedback (4: self-oscillation)
 * @return   the fourth stage
 */
template <class VT>
EPH_FORCE_INLINE VT ladderMoog(VT* v, VT* s, VT x, VT g, VT k)
{
    const VT one = lanes<VT>(1.0f);
    for (int it = 0; it < Newton; ++it) {
        const VT tx = ftanh(x - k * v[3]);
        const VT t0 = ftanh(v[0]), t1 = ftanh(v[1]), t2 = ftanh(v[2]), t3 = ftanh(v[3]);
        const VT F0 = v[0] - s[0] - g * (tx - t0), F1 = v[1] - s[1] - g * (t0 - t1);
        const VT F2 = v[2] - s[2] - g * (t1 - t2), F3 = v[3] - s[3] - g * (t2 - t3);
        const VT d0 = one - t0 * t0, d1 = one - t1 * t1, d2 = one - t2 * t2, d3 = one - t3 * t3, dx = one - tx * tx;
        // The diagonal's reciprocals first: three divisions side by side, off the chain of the substitution.
        const VT i1 = one / vfmadd(g, d1, one), i2 = one / vfmadd(g, d2, one), i3 = one / vfmadd(g, d3, one);
        const VT a1 = -F1 * i1, b1 = g * d0 * i1;
        const VT a2 = vfmadd(g * d1, a1, -F2) * i2, b2 = g * d1 * b1 * i2;
        const VT a3 = vfmadd(g * d2, a2, -F3) * i3, b3 = g * d2 * b2 * i3;
        const VT J00 = vfmadd(g, d0, one), J03 = g * dx * k;
        const VT D0 = vfnmadd(J03, a3, -F0) / vfmadd(J03, b3, J00);
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
template <class VT>
EPH_FORCE_INLINE VT otaCascade(VT* v, VT* s, VT x, VT g, VT k, float d, float r, int mix = 0)
{
    const VT one = lanes<VT>(1.0f), D = lanes<VT>(d), Di = lanes<VT>(1.0f / d), R = lanes<VT>(r), Ri = lanes<VT>(1.0f / r);
    VT a0 = x;
    for (int it = 0; it < Newton; ++it) {
        const VT tr = ftanh(R * v[3]);
        a0 = x - k * tr * Ri;
        const VT e0 = ftanh(D * (a0 - v[0])), e1 = ftanh(D * (v[0] - v[1])), e2 = ftanh(D * (v[1] - v[2])), e3 = ftanh(D * (v[2] - v[3]));
        const VT F0 = v[0] - s[0] - g * e0 * Di, F1 = v[1] - s[1] - g * e1 * Di;
        const VT F2 = v[2] - s[2] - g * e2 * Di, F3 = v[3] - s[3] - g * e3 * Di;
        const VT q0 = g * (one - e0 * e0), q1 = g * (one - e1 * e1), q2 = g * (one - e2 * e2), q3 = g * (one - e3 * e3);
        const VT i1 = one / (one + q1), i2 = one / (one + q2), i3 = one / (one + q3);   // the diagonal's reciprocals first
        const VT a1 = -F1 * i1, b1 = q1 * i1;
        const VT a2 = vfmadd(q2, a1, -F2) * i2, b2 = q2 * b1 * i2;
        const VT a3 = vfmadd(q3, a2, -F3) * i3, b3 = q3 * b2 * i3;
        const VT J00 = one + q0, J03 = q0 * k * (one - tr * tr);
        const VT D0 = vfnmadd(J03, a3, -F0) / vfmadd(J03, b3, J00);
        v[0] = v[0] + D0;
        v[1] = v[1] + vfmadd(b1, D0, a1);
        v[2] = v[2] + vfmadd(b2, D0, a2);
        v[3] = v[3] + vfmadd(b3, D0, a3);
    }
    for (int i = 0; i < 4; ++i) s[i] = v[i] + v[i] - s[i];
    // The pole mixing of the Xpander and the Matrix-12: responses as sums of the cascade's taps (per stage 1/(1+s)).
    const VT two = lanes<VT>(2.0f), four = lanes<VT>(4.0f), six = lanes<VT>(6.0f);
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
template <class VT>
EPH_FORCE_INLINE VT svfNonlinear(VT* v, VT* s, VT x, VT g, VT R, float L, float a, VT morph, bool toBand = false)
{
    const VT one = lanes<VT>(1.0f), Lv = lanes<VT>(L), Li = lanes<VT>(1.0f / L), A = lanes<VT>(a);
    const VT off = Lv * ftanh(A);
    const VT two = lanes<VT>(2.0f);
    const VT lb = lanes<VT>(1.5f), lbi = lanes<VT>(1.0f / 1.5f);
    auto damp = [&](VT b) { return two * R * b + b - lb * ftanh(b * lbi); };
    for (int it = 0; it < Newton; ++it) {
        const VT tq = ftanh(v[0] * lbi);
        const VT h = x - damp(v[0]) - v[1];
        const VT th = ftanh(vfmadd(h, Li, A)), tb = ftanh(vfmadd(v[0], Li, A));
        const VT F0 = v[0] - s[0] - g * (Lv * th - off), F1 = v[1] - s[1] - g * (Lv * tb - off);
        const VT nh = g * (one - th * th), nb = g * (one - tb * tb);
        const VT dh = vfmadd(tq, tq, two * R);                        // -dh/dv0: 2R + tanh^2(v0 / 1.5)
        const VT det = vfmadd(nh, dh + nb, one);                      // 1 + g N'(h) (2R + ...) + g^2 N'(h) N'(v0)
        const VT D0 = vfmadd(F1, nh, -F0) / det;
        v[0] = v[0] + D0;
        v[1] = v[1] + vfmadd(nb, D0, -F1);
    }
    s[0] = v[0] + v[0] - s[0];
    s[1] = v[1] + v[1] - s[1];
    const VT hp = x - damp(v[0]) - v[1];
    if (toBand) return v[1] + morph * (v[0] - v[1]);
    // Low pass -> notch (lp + hp) -> high pass.
    const VT half = lanes<VT>(0.5f);
    const VT m2 = morph + morph;
    return vselect(vlt(morph, half), v[1] + m2 * hp, (two - m2) * v[1] + hp);
}

/**
 * @brief The diode ladder (TB-303, EMS; Stinchcombe): four capacitor nodes coupled through diode pairs, the last one
 *        of half the capacitance, u = x - k v3:
 *        v0' = w (T(u - v0) - T(v0 - v1)), v1' = w (T(v0 - v1) - T(v1 - v2)), v2' = w (T(v1 - v2) - T(v2 - v3)),
 *        v3' = 2 w T(v2 - v3). Linear, it oscillates at k = 17 and sqrt 2 times w: the caller passes w / sqrt 2.
 */
template <class VT>
EPH_FORCE_INLINE VT diodeLadder(VT* v, VT* s, VT x, VT g, VT k)
{
    const VT one = lanes<VT>(1.0f), g2 = g + g;
    for (int it = 0; it < Newton; ++it) {
        const VT u = x - k * v[3];
        const VT ti = ftanh(u - v[0]), t01 = ftanh(v[0] - v[1]), t12 = ftanh(v[1] - v[2]), t23 = ftanh(v[2] - v[3]);
        const VT r0 = -(v[0] - s[0] - g * (ti - t01)), r1 = -(v[1] - s[1] - g * (t01 - t12));
        const VT r2 = -(v[2] - s[2] - g * (t12 - t23)), r3 = -(v[3] - s[3] - g2 * t23);
        const VT di = one - ti * ti, d01 = one - t01 * t01, d12 = one - t12 * t12, d23 = one - t23 * t23;
        // The tridiagonal part.
        const VT T00 = vfmadd(g, di + d01, one), T01 = -g * d01;
        const VT T10 = T01, T11 = vfmadd(g, d01 + d12, one), T12 = -g * d12;
        const VT T21 = T12, T22 = vfmadd(g, d12 + d23, one), T23 = -g * d23;
        const VT T32 = -g2 * d23, T33 = vfmadd(g2, d23, one);
        const VT J03 = g * di * k;   // the corner: the feedback into the first node
        // Thomas, for the residual and for the unit vector e0 at once; every pivot's reciprocal once, the rest multiplied.
        const VT z0 = one / T00;
        const VT c0 = T01 * z0, x0 = r0 * z0;
        const VT n1 = one / vfnmadd(T10, c0, T11);
        const VT c1 = T12 * n1, x1 = vfnmadd(T10, x0, r1) * n1, z1 = -(T10 * z0) * n1;
        const VT n2 = one / vfnmadd(T21, c1, T22);
        const VT c2 = T23 * n2, x2 = vfnmadd(T21, x1, r2) * n2, z2 = -(T21 * z1) * n2;
        const VT n3 = one / vfnmadd(T32, c2, T33);
        const VT X3 = vfnmadd(T32, x2, r3) * n3, Z3 = -(T32 * z2) * n3;
        const VT X2 = vfnmadd(c2, X3, x2), Z2 = vfnmadd(c2, Z3, z2);
        const VT X1 = vfnmadd(c1, X2, x1), Z1 = vfnmadd(c1, Z2, z1);
        const VT X0 = vfnmadd(c0, X1, x0), Z0 = vfnmadd(c0, Z1, z0);
        // Sherman-Morrison: J = T + J03 e0 e3^T.
        const VT f = J03 * X3 / vfmadd(J03, Z3, one);
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
template <class VT>
EPH_FORCE_INLINE VT korg35(VT* v, VT* s, VT x, VT g, VT K)
{
    const VT one = lanes<VT>(1.0f), two = lanes<VT>(2.0f);
    for (int it = 0; it < Newton; ++it) {
        const VT dq = ftanh(K * v[1]);
        const VT A = v[0] + dq;
        const VT F0 = v[0] - s[0] - g * (x - two * A + v[1]), F1 = v[1] - s[1] - g * (A - v[1]);
        const VT kd = K * (one - dq * dq);                            // dA / dq1
        const VT J00 = vfmadd(two, g, one), J01 = g * (two * kd - one), J10 = -g, J11 = one + g - g * kd;
        const VT idet = one / (J00 * J11 - J01 * J10);
        const VT D0 = (J01 * F1 - J11 * F0) * idet, D1 = (J10 * F0 - J00 * F1) * idet;
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
    static float otaDrive(FilterModelE m) { return m == FilterModelE::JUNO ? 0.45f : 0.7f; }
    static float otaRes(FilterModelE m) { return m == FilterModelE::JUNO ? 1.2f : 1.6f; }   ///< @copydoc otaDrive
    /** @brief The state-variable filters' nonlinearity: the integrators' range L and asymmetry a (SEM, Polivoks, Wasp). */
    static float svfRange(FilterModelE m) { return m == FilterModelE::POLIVOKS ? 0.35f : (m == FilterModelE::WASP ? 0.6f : 1.5f); }
    static float svfAsym(FilterModelE m) { return m == FilterModelE::WASP ? 0.35f : 0.0f; }   ///< @copydoc svfRange
    /** @brief Resonance 0..1 as the model's feedback: 4 for the ladders and cascades, 17 for the diode ladder, 3 for
     *         the Korg35 (their self-oscillation just at 1); for the state-variable filters the damping R (Q = 1 / 2R). */
    static float feedback(FilterModelE m, float res)
    {
        switch (m) {
        case FilterModelE::MOOG: return 4.0f * res * 0.985f;
        case FilterModelE::PROPHET: case FilterModelE::JUNO: case FilterModelE::XPANDER: return 4.1f * res;
        case FilterModelE::DIODE: return 17.5f * res;
        case FilterModelE::KORG35: return 3.05f * res;
        case FilterModelE::SEM: return 0.707f * (1.0f - res) + 0.012f;
        case FilterModelE::POLIVOKS: case FilterModelE::WASP: return 0.6f * (1.0f - res) * (1.0f - res) + 0.004f;
        default: return 0.97f * res;   // the comb's feedback
        }
    }
    /** @brief The pass band's loss by the feedback, partly made good (the ladders and cascades lose 1 / (1 + k)). */
    static float makeup(FilterModelE m, float k)
    {
        switch (m) {
        case FilterModelE::MOOG: return 1.0f + 0.5f * k;
        case FilterModelE::PROPHET: case FilterModelE::XPANDER: return 1.0f + 0.5f * k;
        case FilterModelE::JUNO: return 1.0f + 0.25f * k;
        case FilterModelE::DIODE: return 1.0f + 0.3f * k;
        default: return 1.0f;
        }
    }
};

/** @brief The Xpander's pole mixes (FilterModel::Xpander): weights of the cascade's input and its four stages. */
inline const float* poleMix(float mode)
{
    static const float Mix[8][5] = { { 0, 0, 0, 0, 1 }, { 0, 0, 1, 0, 0 }, { 0, 2, -2, 0, 0 }, { 0, 0, 4, -8, 4 },
                                      { 1, -2, 1, 0, 0 }, { 1, -4, 6, -4, 1 }, { 1, -2, 2, 0, 0 }, { 1, -4, 4, 0, 0 } };
    return Mix[std::clamp(static_cast<int>(std::lround(mode * 7.0f)), 0, 7)];
}

/**
 * @brief One filter of any model on one lane (the pad synth's; the voice bank's kernel runs the same models in its
 *        registers): its nodes, its states and, for the comb, its line.
 */
struct FilterLane {
    float m_v[4] = {};   ///< the nodes' voltages
    float m_s[4] = {};   ///< the integrators' states
    std::vector<float> m_line;   ///< the comb's line (allocate(); a power of two)
    int m_pos = 0;   ///< where the comb's line is written
    float m_damp = 0.0f;   ///< the comb's damping state
    void allocate(int length) { m_line.assign(static_cast<size_t>(length), 0.0f); m_pos = 0; }   ///< not on the audio thread
    /** @brief Silence: every state, the line. */
    void clear() { for (int i = 0; i < 4; ++i) m_v[i] = m_s[i] = 0.0f; std::fill(m_line.begin(), m_line.end(), 0.0f); m_damp = 0.0f; }
    /**
     * @brief One sample through model @p m: @p g tan(pi fc / fs), @p k the model's feedback (FilterVoicing::feedback),
     *        @p mode its mode; the pass band's makeup applied.
     */
    float tick(FilterModelE m, float x, float g, float k, float mode)
    {
        float y = 0.0f;
        switch (m) {
        case FilterModelE::MOOG: y = ladderMoog<float>(m_v, m_s, x, g, k); break;
        case FilterModelE::PROPHET: case FilterModelE::JUNO:
            y = otaCascade<float>(m_v, m_s, x, g, k, FilterVoicing::otaDrive(m), FilterVoicing::otaRes(m));
            break;
        case FilterModelE::XPANDER: {
            otaCascade<float>(m_v, m_s, x, g, k, FilterVoicing::otaDrive(m), FilterVoicing::otaRes(m));
            const float r = FilterVoicing::otaRes(m);
            const float a0 = x - k * ftanh<float>(r * m_v[3]) / r;
            const float* w = poleMix(mode);
            y = w[0] * a0 + w[1] * m_v[0] + w[2] * m_v[1] + w[3] * m_v[2] + w[4] * m_v[3];
            break;
        }
        case FilterModelE::SEM: case FilterModelE::WASP:
            y = svfNonlinear<float>(m_v, m_s, x, g, k, FilterVoicing::svfRange(m), FilterVoicing::svfAsym(m), mode);
            break;
        case FilterModelE::POLIVOKS: y = svfNonlinear<float>(m_v, m_s, x, g, k, FilterVoicing::svfRange(m), 0.0f, mode, true); break;
        case FilterModelE::DIODE: y = diodeLadder<float>(m_v, m_s, x, g * 0.70710678f, k); break;
        case FilterModelE::KORG35: y = korg35<float>(m_v, m_s, x, g, k); break;
        case FilterModelE::COMB: {
            if (m_line.empty()) return 0.0f;
            const int len = static_cast<int>(m_line.size());
            const float period = std::min(static_cast<float>(len - 2), 3.14159265f / std::atan(std::max(g, 1e-4f)));
            float rp = static_cast<float>(m_pos) - period;
            if (rp < 0.0f) rp += static_cast<float>(len);
            const int i0 = static_cast<int>(rp);
            const float d = m_line[static_cast<size_t>(i0)] + (rp - static_cast<float>(i0)) * (m_line[static_cast<size_t>((i0 + 1) & (len - 1))] - m_line[static_cast<size_t>(i0)]);
            m_damp += 0.5f * (d - m_damp);
            const float yv = x + (mode >= 0.5f ? -k : k) * m_damp;
            m_line[static_cast<size_t>(m_pos)] = yv;
            m_pos = (m_pos + 1) & (len - 1);
            y = 0.5f * yv;
            break;
        }
        default: break;
        }
        return y * FilterVoicing::makeup(m, k);
    }
};

} // namespace eph
