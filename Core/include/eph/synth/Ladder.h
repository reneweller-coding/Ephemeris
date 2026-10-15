/**
 * @file Ladder.h
 * @brief Four-pole transistor ladder low pass, zero-delay feedback, nonlinear, as a lane template.
 *
 * **Model.** The Moog ladder's four stages obey dy_i/dt = w (tanh(y_{i-1}) - tanh(y_i)), with the
 * input stage fed tanh(x - k y_4) (Huovilainen, "Non-linear digital implementation of the Moog
 * ladder filter", DAFx 2004). Discretised with trapezoidal integrators in the topology-preserving
 * form, the feedback loop has no unit delay (Zavalishin, "The Art of VA Filter Design", ch. 5),
 * which keeps the cutoff and the resonance peak where they belong up to Nyquist.
 *
 * **Solving the nonlinear loop cheaply.** An exact zero-delay solution needs an iterative solve per
 * sample. Instead each nonlinearity is replaced by its local gain tanh(v)/v, evaluated at the
 * previous sample's value of v. With those gains fixed, the loop is linear and is solved in closed
 * form; the next sample updates the gains. This is the method T. Voipio published as "cheap
 * non-linear zero-delay filters" (KVR forum, 2012); it keeps the ZDF tuning, self-oscillates
 * cleanly and costs one division and one square root per stage.
 *
 * **Saturation curve.** The gain uses the algebraic sigmoid v / sqrt(1 + v^2) rather than tanh:
 * it is bounded, smooth, a little softer than tanh, and needs only a square root and a division --
 * both single IEEE operations, so the lane path is bit-identical to the scalar path (see Vec.h).
 *
 * **Derivation of the closed form.** With gains t_i, stage i is y_i = (s_i + g m_t{i-1} y_{i-1}) /
 * (1 + g t_i), i.e. y_i = A_i y_{i-1} + B_i. Chaining gives y_4 = P u + Q with P = A_4 A_3 A_2 A_1
 * and Q = A_4 A_3 A_2 B_1 + A_4 A_3 B_2 + A_4 B_3 + B_4. The input u = x - k y_4 then solves to
 * u = (x - k Q) / (1 + k P). The integrator states update as s_i <- 2 y_i - s_i.
 *
 * @tparam V lane type (float or VecF)
 * @note Copied from Phosphene `Core/include/phos/Ladder.h` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 * @author Rene Weller
 */
#pragma once
#include "eph/Vec.h"

namespace eph {

/** @brief The ladder, one filter per lane of @p V (see the file comment). */
template <class VT>
struct LadderT {
    VT m_s[4];     ///< trapezoidal integrator states
    VT m_y[4];     ///< stage outputs of the previous sample (gain estimates)
    VT m_u;        ///< input stage value of the previous sample

    /** @brief Clears all states. */
    void reset()
    {
        for (int i = 0; i < 4; ++i) { m_s[i] = lanes<VT>(0.0f); m_y[i] = lanes<VT>(0.0f); }
        m_u = lanes<VT>(0.0f);
    }

    /**
     * @brief Local gain of the algebraic sigmoid: sigma(v)/v = 1/sqrt(1 + v^2).
     * @param[in] v  the stage's input
     * @return sigma(v)/v
     */
    static inline VT gainOf(VT v)
    {
        const VT one = lanes<VT>(1.0f);
        return one / vsqrt(vfmadd(v, v, one));
    }

    /**
     * @brief One sample.
     * @param[in] x     input (already scaled by the drive)
     * @param[in] g     prewarped integrator gain tan(pi fc / fs), per lane
     * @param[in] k     feedback amount; the linear filter self-oscillates at 4
     * @param[in] comp  input gain (1 + comp k) that restores the passband level lost to resonance
     * @return the four-pole low-pass output
     */
    inline VT tick(VT x, VT g, VT k, VT comp)
    {
        const VT one = lanes<VT>(1.0f);
        const VT t0 = gainOf(m_u), t1 = gainOf(m_y[0]), t2 = gainOf(m_y[1]), t3 = gainOf(m_y[2]), t4 = gainOf(m_y[3]);
        const VT d1 = one / vfmadd(g, t1, one), d2 = one / vfmadd(g, t2, one);
        const VT d3 = one / vfmadd(g, t3, one), d4 = one / vfmadd(g, t4, one);
        const VT a1 = g * t0 * d1, a2 = g * t1 * d2, a3 = g * t2 * d3, a4 = g * t3 * d4;
        const VT b1 = m_s[0] * d1, b2 = m_s[1] * d2, b3 = m_s[2] * d3, b4 = m_s[3] * d4;
        const VT a43 = a4 * a3, a432 = a43 * a2;
        const VT P = a432 * a1;
        const VT Q = vfmadd(a432, b1, vfmadd(a43, b2, vfmadd(a4, b3, b4)));
        const VT xin = x * vfmadd(comp, k, one);
        const VT un = (xin - k * Q) / vfmadd(k, P, one);
        const VT y1 = vfmadd(a1, un, b1);
        const VT y2 = vfmadd(a2, y1, b2);
        const VT y3 = vfmadd(a3, y2, b3);
        const VT y4 = vfmadd(a4, y3, b4);
        const VT two = lanes<VT>(2.0f);
        m_s[0] = two * y1 - m_s[0];
        m_s[1] = two * y2 - m_s[1];
        m_s[2] = two * y3 - m_s[2];
        m_s[3] = two * y4 - m_s[3];
        m_y[0] = y1; m_y[1] = y2; m_y[2] = y3; m_y[3] = y4;
        m_u = un;
        return y4;
    }
};

} // namespace eph
