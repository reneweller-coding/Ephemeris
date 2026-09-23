/**
 * @file TestSupport.h
 * @brief A minimal check framework and measurement helpers for the self tests.
 * @note Copied from Phosphene `Tests/TestSupport.h` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 */
#pragma once
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <vector>

namespace ephtest {

/** @brief Running totals of passed and failed checks. */
struct Tally {
    int passed = 0;   ///< checks that held
    int failed = 0;   ///< checks that did not
};

/** @brief The one tally of a test executable. */
inline Tally& tally() { static Tally t; return t; }

/**
 * @brief Records a check; prints failures always and passes when @p verbose.
 * @param ok     outcome
 * @param what   short description
 * @param detail measured values, printed with the description
 */
inline void check(bool ok, const char* what, const std::string& detail = std::string())
{
    if (ok) ++tally().passed;
    else ++tally().failed;
    std::printf("  [%s] %s%s%s\n", ok ? " ok " : "FAIL", what, detail.empty() ? "" : "  -- ", detail.c_str());
}

/** @brief printf into a std::string. */
template <class... A>
inline std::string fmt(const char* f, A... args)
{
    // 1024 since 20.09.2026 (round "dialogue"). At 512 this silently truncated, and measureLock built
    // its parameter string with it: everything past the first of ten mute assignments was cut off, so
    // "render the bass alone" rendered the bass *and* the pad, the drone and the arp. It stayed
    // invisible while those voices happened to be quiet in the measured bars and became a failing
    // sub-sample phase check the moment the drone became a continuous carpet. measureLock now
    // concatenates instead; the wider buffer is the second half of the fix, for every other caller.
    char buf[1024];
    std::snprintf(buf, sizeof(buf), f, args...);
    return buf;
}

/** @brief Seconds since the previous section began (steady clock). */
inline double sectionSeconds()
{
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const double s = std::chrono::duration<double>(now - last).count();
    last = now;
    return s;
}

/** @brief Prints a section header, with the time the previous section took. */
inline void section(const char* name)
{
    std::printf("         (%.1f s)\n%s\n", sectionSeconds(), name);
}

/** @brief Ends the run: prints totals and returns the process exit code. */
inline int finish()
{
    std::printf("\n%d passed, %d failed\n", tally().passed, tally().failed);
    return tally().failed == 0 ? 0 : 1;
}

/** @brief In-place radix-2 FFT (n a power of two). */
inline void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * 3.141592653589793 / static_cast<double>(len);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

/**
 * @brief Power spectrum of a Blackman-Harris-windowed segment.
 * @param x signal, at least @p n samples
 * @param n FFT length (power of two)
 * @return power per bin, bins 0..n/2
 */
inline std::vector<double> powerSpectrum(const float* x, size_t n)
{
    std::vector<std::complex<double>> a(n);
    for (size_t i = 0; i < n; ++i) {
        const double t = 2.0 * 3.141592653589793 * static_cast<double>(i) / static_cast<double>(n);
        const double w = 0.35875 - 0.48829 * std::cos(t) + 0.14128 * std::cos(2 * t) - 0.01168 * std::cos(3 * t);
        a[i] = static_cast<double>(x[i]) * w;
    }
    fft(a);
    std::vector<double> p(n / 2 + 1);
    for (size_t i = 0; i <= n / 2; ++i) p[i] = std::norm(a[i]);
    return p;
}

/** @brief Decibels of a power ratio. */
inline double powDb(double ratio) { return 10.0 * std::log10(ratio > 1e-300 ? ratio : 1e-300); }

} // namespace ephtest
