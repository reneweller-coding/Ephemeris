/**
 * @file Halfband.cpp
 * @brief Coefficient design for the polyphase IIR half-band filters.
 *
 * The design maps the transition width to the elliptic modulus k, computes the nome q, derives the
 * filter order needed for the attenuation, and evaluates the all-pass coefficients from truncated
 * theta-function series. Correctness is not taken on trust: Tests/selftest.cpp measures the
 * passband and the image rejection of the designed filters.
 * @note Copied from Phosphene `Core/src/Halfband.cpp` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 */
#include "eph/Halfband.h"
#include <cmath>

namespace eph {

namespace {

constexpr double kPiD = 3.14159265358979323846;   ///< pi

/** @brief The elliptic modulus @p k and the nome @p q of a half-band filter with transition band @p transition. */
void transitionParams(double transition, double& k, double& q)
{
    k = std::tan((1.0 - transition * 2.0) * kPiD / 4.0);
    k *= k;
    const double kksqrt = std::pow(1.0 - k * k, 0.25);
    const double e = 0.5 * (1.0 - kksqrt) / (1.0 + kksqrt);
    const double e2 = e * e;
    const double e4 = e2 * e2;
    q = e * (1.0 + e4 * (2.0 + e4 * (15.0 + 150.0 * e4)));
}

/** @brief The numerator's series of the c-th coefficient of an @p order filter with nome @p q. */
double accNum(double q, int order, int c)
{
    double acc = 0.0, term = 0.0;
    int i = 0, sign = 1;
    do {
        term = std::pow(q, i * (i + 1)) * std::sin((i * 2 + 1) * c * kPiD / order) * sign;
        acc += term;
        sign = -sign;
        ++i;
    } while (std::fabs(term) > 1e-100 && i < 1000);
    return acc;
}

/** @brief The denominator's series of it. */
double accDen(double q, int order, int c)
{
    double acc = 0.0, term = 0.0;
    int i = 1, sign = -1;
    do {
        term = std::pow(q, i * i) * std::cos(i * 2 * c * kPiD / order) * sign;
        acc += term;
        sign = -sign;
        ++i;
    } while (std::fabs(term) > 1e-100 && i < 1000);
    return acc;
}

} // namespace

HalfbandDesign designHalfband(double attenuationDb, double transition)
{
    HalfbandDesign out;
    double k = 0.0, q = 0.0;
    transitionParams(transition, k, q);
    const double attn = std::pow(10.0, -attenuationDb / 10.0);
    const double a = attn / (1.0 - attn);
    int order = static_cast<int>(std::ceil(std::log(a * a / 16.0) / std::log(q)));
    if ((order & 1) == 0) ++order;
    if (order == 1) order = 3;
    int count = (order - 1) / 2;
    if (count > kHalfbandMaxCoefs) count = kHalfbandMaxCoefs;
    const int designOrder = count * 2 + 1;
    for (int i = 0; i < count; ++i) {
        const int c = i + 1;
        const double num = accNum(q, designOrder, c) * std::pow(q, 0.25);
        const double den = accDen(q, designOrder, c) + 0.5;
        const double ww = num / den;
        const double wwsq = ww * ww;
        const double x = std::sqrt((1.0 - wwsq * k) * (1.0 - wwsq / k)) / (1.0 + wwsq);
        out.coef[i] = static_cast<float>((1.0 - x) / (1.0 + x));
    }
    out.count = count;
    return out;
}

} // namespace eph
