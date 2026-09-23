/**
 * @file Halfband.h
 * @brief Polyphase IIR half-band filters for 2x oversampling, as lane templates.
 *
 * Two parallel chains of first-order all-pass sections in z^-2 whose outputs are averaged form a
 * half-band low pass with an elliptic-like response: very steep, a few samples of group delay at low
 * frequencies, and only two multiplies per coefficient per sample. The phase is not linear near
 * Nyquist, which does not matter for a bass or an acid line.
 *
 * The coefficients are designed at run time from a stopband attenuation and a transition width
 * (designHalfband()), using the closed-form design of Valenzuela and Constantinides, "Digital signal
 * processing schemes for efficient interpolation and decimation", IEE Proc. G, 1983, in the
 * formulation of Laurent de Soras' HIIR library.
 * @note Copied from Phosphene `Core/include/phos/Halfband.h` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 */
#pragma once
#include "eph/Vec.h"

namespace eph {

constexpr int kHalfbandMaxCoefs = 16;   ///< upper bound on all-pass sections

/** @brief A designed coefficient set. */
struct HalfbandDesign {
    int count = 0;                              ///< number of coefficients (sections)
    float coef[kHalfbandMaxCoefs] = {};         ///< all-pass coefficients, alternating between the chains
};

/**
 * @brief Designs a half-band filter.
 * @param attenuationDb stopband attenuation in dB (e.g. 96)
 * @param transition    transition width as a fraction of the *oversampled* rate, 0 < t < 0.5;
 *                      the passband ends at (0.25 - t/2) and the stopband starts at (0.25 + t/2)
 * @return the coefficients; count is capped at kHalfbandMaxCoefs
 */
HalfbandDesign designHalfband(double attenuationDb, double transition);

/**
 * @brief 2x decimator: two samples at the high rate in, one at the base rate out.
 * @tparam V lane type (float or VecF)
 */
template <class V>
struct HalfbandDown {
    HalfbandDesign d;                           ///< coefficients
    V x[kHalfbandMaxCoefs];                     ///< previous inputs per section
    V y[kHalfbandMaxCoefs];                     ///< previous outputs per section

    /** @brief Takes a design and clears the state. */
    void setup(const HalfbandDesign& design) { d = design; reset(); }
    /** @brief Clears the state. */
    void reset() { for (int i = 0; i < kHalfbandMaxCoefs; ++i) { x[i] = lanes<V>(0.0f); y[i] = lanes<V>(0.0f); } }

    /**
     * @brief One decimation step.
     * @param first  the earlier of the two high-rate samples
     * @param second the later one
     */
    inline V process(V first, V second)
    {
        V a = second, b = first;   // chain 0 gets the later sample, chain 1 the earlier
        for (int i = 0; i < d.count; ++i) {
            const V c = lanes<V>(d.coef[i]);
            V& in = (i & 1) ? b : a;
            const V out = vfmadd(in - y[i], c, x[i]);   // (in - y1) c + x1
            x[i] = in;
            y[i] = out;
            in = out;
        }
        return (a + b) * lanes<V>(0.5f);
    }
};

/**
 * @brief 2x interpolator: one base-rate sample in, two high-rate samples out.
 * @tparam V lane type (float or VecF)
 */
template <class V>
struct HalfbandUp {
    HalfbandDesign d;
    V x[kHalfbandMaxCoefs];
    V y[kHalfbandMaxCoefs];

    void setup(const HalfbandDesign& design) { d = design; reset(); }
    void reset() { for (int i = 0; i < kHalfbandMaxCoefs; ++i) { x[i] = lanes<V>(0.0f); y[i] = lanes<V>(0.0f); } }

    /** @brief One interpolation step; @p o0 is the earlier output sample, @p o1 the later. */
    inline void process(V in, V& o0, V& o1)
    {
        V a = in, b = in;
        for (int i = 0; i < d.count; ++i) {
            const V c = lanes<V>(d.coef[i]);
            V& s = (i & 1) ? b : a;
            const V out = vfmadd(s - y[i], c, x[i]);
            x[i] = s;
            y[i] = out;
            s = out;
        }
        o0 = a;
        o1 = b;
    }
};

} // namespace eph
