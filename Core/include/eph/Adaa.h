/**
 * @file Adaa.h
 * @brief Saturation with first-order antiderivative antialiasing (ADAA).
 *
 * A saturator creates partials above Nyquist that fold back as inharmonic tones. First-order ADAA
 * evaluates the antiderivative of the curve and differentiates it across the sample interval --
 * the curve convolved with a one-sample rectangle -- which lowers the aliases by roughly 20 dB for
 * half a sample of delay.
 *
 * References: Parker, Zavalishin, Le Bihan, "Reducing the aliasing of nonlinear waveshaping using
 * continuous-time convolution", DAFx 2016; Bilbao, Esqueda, Parker, Valimaki, "Antiderivative
 * antialiasing for memoryless nonlinearities", IEEE SPL 2017.
 *
 * @note TanhAdaa copied from Noctuary `Core/include/ambient/Adaa.h` at b60a2fe (15.09.2026);
 *       HardClipAdaa is new in Phosphene.
 * @note Copied from Phosphene `Core/include/phos/Adaa.h` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 */
#pragma once
#include <cmath>

namespace eph {

/** @brief log(cosh(x)), the antiderivative of tanh, without overflow or cancellation. */
inline double logCosh(double x)
{
    const double a = std::fabs(x);
    return a + std::log1p(std::exp(-2.0 * a)) - 0.6931471805599453;
}

/**
 * @brief tanh through first-order ADAA.
 *
 * Where two successive inputs are too close for the difference quotient to be accurate, the
 * curve at their midpoint is used, which is its limit.
 */
struct TanhAdaa {
    float  x1 = 0.0f;   ///< previous input
    double f1 = 0.0;    ///< antiderivative at the previous input

    /** @brief One sample; half a sample late. */
    float operator()(float x)
    {
        const double fx = logCosh(static_cast<double>(x));
        const double dx = static_cast<double>(x) - static_cast<double>(x1);
        const float y = std::fabs(dx) > 1.0e-5 ? static_cast<float>((fx - f1) / dx)
                                               : std::tanh(0.5f * (x + x1));
        x1 = x;
        f1 = fx;
        return y;
    }
    /** @brief Clears the state. */
    void reset() { x1 = 0.0f; f1 = 0.0; }
};

/**
 * @brief Hard clip at +-1 through first-order ADAA.
 *
 * The antiderivative of clip(x) is x^2/2 inside and |x| - 1/2 outside. A hard clip is the
 * "punchier" saturation for a kick: it keeps the attack's shape up to the ceiling and squares the
 * body off beyond it, where tanh rounds everything a little.
 */
struct HardClipAdaa {
    float  x1 = 0.0f;   ///< previous input
    double f1 = 0.0;    ///< antiderivative at the previous input

    /** @brief The antiderivative of the clip. */
    static double anti(double x) { const double a = std::fabs(x); return a <= 1.0 ? 0.5 * x * x : a - 0.5; }

    /** @brief One sample; half a sample late. */
    float operator()(float x)
    {
        const double fx = anti(static_cast<double>(x));
        const double dx = static_cast<double>(x) - static_cast<double>(x1);
        float y;
        if (std::fabs(dx) > 1.0e-5) y = static_cast<float>((fx - f1) / dx);
        else { const float m = 0.5f * (x + x1); y = m > 1.0f ? 1.0f : (m < -1.0f ? -1.0f : m); }
        x1 = x;
        f1 = fx;
        return y;
    }
    /** @brief Clears the state. */
    void reset() { x1 = 0.0f; f1 = 0.0; }
};

} // namespace eph
