/**
 * @file Vec.h
 * @brief Lane-parallel SIMD: one arithmetic written once, compiled for AVX2 (8 lanes), NEON
 *        (4 lanes) and scalar (1 lane).
 *
 * A recursive filter cannot be vectorised along time -- every sample needs the previous one -- but
 * eight filters can run side by side. Phosphene's synthesizers are therefore written as templates
 * over a lane type @c V: instantiated with @c float they are the scalar reference, instantiated
 * with @c VecF they process kVecWidth voices, layers or percussion lanes at once.
 *
 * **Bit-exactness.** Every operation offered here is a single IEEE-754 operation per lane with the
 * same rounding on all three paths: add, subtract, multiply, divide, square root, floor, min/max
 * (written as the comparison the hardware does), and the fused multiply-add, which is
 * @c _mm256_fmadd_ps on AVX2, @c vfmaq_f32 on AArch64 and @c std::fma on the scalar path. A lane
 * computed by the vector path is therefore identical to the scalar computation, bit for bit, and
 * the tests (Tests/vectest.cpp) demand exactly that. Reductions across lanes are not offered except
 * as sumOrdered(), which adds the lanes in index order like a scalar loop would. The build disables
 * floating-point contraction so the compiler cannot fuse a scalar @c a*b+c behind our back
 * (CMakeLists.txt).
 *
 * **Path selection**, first match wins:
 *  - @c EPH_FORCE_SCALAR: scalar (test variant)
 *  - @c EPH_NEON_SHIM or an ARM target: NEON (on x86 through Tests/neonshim/arm_neon.h)
 *  - @c __AVX2__: AVX2 + FMA
 *  - otherwise scalar
 *
 * @note Written for Phosphene (15.09.2026) after the pattern of Noctuary's Simd.h, whose NEON test
 *       shim it extends.
 * @note Copied from Phosphene `Core/include/phos/Vec.h` at 9a2f615 (24.09.2026); namespace eph, prefix EPH_.
 */
#pragma once
#include <cmath>
#include <cstddef>

#if defined(EPH_FORCE_SCALAR)
  #define EPH_VEC_PATH 0
#elif defined(EPH_NEON_SHIM) || defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON) || defined(__ARM_NEON__)
  #define EPH_VEC_PATH 2
  #include <arm_neon.h>
#elif defined(__AVX2__)
  #define EPH_VEC_PATH 1
  #include <immintrin.h>
#else
  #define EPH_VEC_PATH 0
#endif

namespace eph {

// ------------------------------------------------------------------------------------------------
// Scalar lane: always available, so every template can be instantiated with float on any path.
// ------------------------------------------------------------------------------------------------

/** @brief Lane count of a lane type: 1 for float, the register width for VecF. */
template <class V> constexpr int laneWidth() { return static_cast<int>(sizeof(V) / sizeof(float)); }

inline float vset1(float x, float) { return x; }                                       ///< constant (overload tag: float)
inline float vloadAs(const float* p, float) { return *p; }                             ///< load one lane (tag: float)
inline void  vstore(float* p, float a) { *p = a; }                                     ///< store one lane
inline float vfmadd(float a, float b, float c) { return std::fma(a, b, c); }           ///< a*b + c, fused
inline float vfnmadd(float a, float b, float c) { return std::fma(-a, b, c); }         ///< c - a*b, fused
inline float vmin(float a, float b) { return a < b ? a : b; }                          ///< minimum
inline float vmax(float a, float b) { return a > b ? a : b; }                          ///< maximum
inline float vsqrt(float a) { return std::sqrt(a); }                                   ///< square root
inline float vabs(float a) { return std::fabs(a); }                                    ///< absolute value
inline float vfloor(float a) { return std::floor(a); }                                 ///< floor
inline bool  vlt(float a, float b) { return a < b; }                                   ///< a < b
inline bool  vgt(float a, float b) { return a > b; }                                   ///< a > b
inline bool  vge(float a, float b) { return a >= b; }                                  ///< a >= b
inline float vselect(bool m, float t, float f) { return m ? t : f; }                   ///< m ? t : f
inline float sumOrdered(float a) { return a; }                                         ///< lanes added in order
inline float laneOf(float a, int) { return a; }                                        ///< one lane's value

#if EPH_VEC_PATH == 1
// ------------------------------------------------------------------------------------------------
// AVX2 + FMA, eight lanes.
// ------------------------------------------------------------------------------------------------
constexpr int kVecWidth = 8;                 ///< lanes per register on this path
constexpr const char* kVecPathName = "avx2"; ///< the path this translation unit was built for

/** @brief Eight float lanes. */
struct VecF { __m256 v; };
/** @brief Eight lane masks (all bits set = true). */
struct MaskF { __m256 v; };

inline VecF vset1(float x, VecF) { return { _mm256_set1_ps(x) }; }
inline VecF vloadAs(const float* p, VecF) { return { _mm256_loadu_ps(p) }; }
inline void vstore(float* p, VecF a) { _mm256_storeu_ps(p, a.v); }
inline VecF operator+(VecF a, VecF b) { return { _mm256_add_ps(a.v, b.v) }; }
inline VecF operator-(VecF a, VecF b) { return { _mm256_sub_ps(a.v, b.v) }; }
inline VecF operator*(VecF a, VecF b) { return { _mm256_mul_ps(a.v, b.v) }; }
inline VecF operator/(VecF a, VecF b) { return { _mm256_div_ps(a.v, b.v) }; }
inline VecF operator-(VecF a) { return { _mm256_sub_ps(_mm256_setzero_ps(), a.v) }; }
inline VecF vfmadd(VecF a, VecF b, VecF c) { return { _mm256_fmadd_ps(a.v, b.v, c.v) }; }
inline VecF vfnmadd(VecF a, VecF b, VecF c) { return { _mm256_fnmadd_ps(a.v, b.v, c.v) }; }
// _mm256_min_ps(a, b) is (a < b ? a : b) per lane, exactly the scalar spelling above.
inline VecF vmin(VecF a, VecF b) { return { _mm256_min_ps(a.v, b.v) }; }
inline VecF vmax(VecF a, VecF b) { return { _mm256_max_ps(a.v, b.v) }; }
inline VecF vsqrt(VecF a) { return { _mm256_sqrt_ps(a.v) }; }
inline VecF vabs(VecF a) { return { _mm256_andnot_ps(_mm256_set1_ps(-0.0f), a.v) }; }
inline VecF vfloor(VecF a) { return { _mm256_floor_ps(a.v) }; }
inline MaskF vlt(VecF a, VecF b) { return { _mm256_cmp_ps(a.v, b.v, _CMP_LT_OQ) }; }
inline MaskF vgt(VecF a, VecF b) { return { _mm256_cmp_ps(a.v, b.v, _CMP_GT_OQ) }; }
inline MaskF vge(VecF a, VecF b) { return { _mm256_cmp_ps(a.v, b.v, _CMP_GE_OQ) }; }
inline MaskF operator&(MaskF a, MaskF b) { return { _mm256_and_ps(a.v, b.v) }; }
inline MaskF operator|(MaskF a, MaskF b) { return { _mm256_or_ps(a.v, b.v) }; }
inline VecF vselect(MaskF m, VecF t, VecF f) { return { _mm256_blendv_ps(f.v, t.v, m.v) }; }
inline float laneOf(VecF a, int i) { alignas(32) float t[8]; _mm256_store_ps(t, a.v); return t[i]; }
inline float sumOrdered(VecF a)
{
    alignas(32) float t[8];
    _mm256_store_ps(t, a.v);
    float s = t[0];
    for (int i = 1; i < 8; ++i) s += t[i];
    return s;
}

#elif EPH_VEC_PATH == 2
// ------------------------------------------------------------------------------------------------
// NEON (AArch64), four lanes.
// ------------------------------------------------------------------------------------------------
constexpr int kVecWidth = 4;
#if defined(EPH_NEON_SHIM)
constexpr const char* kVecPathName = "neon-shim";
#else
constexpr const char* kVecPathName = "neon";
#endif

/** @brief Four float lanes. */
struct VecF { float32x4_t v; };
/** @brief Four lane masks. */
struct MaskF { uint32x4_t v; };

inline VecF vset1(float x, VecF) { return { vdupq_n_f32(x) }; }
inline VecF vloadAs(const float* p, VecF) { return { vld1q_f32(p) }; }
inline void vstore(float* p, VecF a) { vst1q_f32(p, a.v); }
inline VecF operator+(VecF a, VecF b) { return { vaddq_f32(a.v, b.v) }; }
inline VecF operator-(VecF a, VecF b) { return { vsubq_f32(a.v, b.v) }; }
inline VecF operator*(VecF a, VecF b) { return { vmulq_f32(a.v, b.v) }; }
inline VecF operator/(VecF a, VecF b) { return { vdivq_f32(a.v, b.v) }; }
inline VecF operator-(VecF a) { return { vnegq_f32(a.v) }; }
// vfmaq_f32(c, a, b) = c + a*b fused; vfmsq_f32(c, a, b) = c - a*b fused.
inline VecF vfmadd(VecF a, VecF b, VecF c) { return { vfmaq_f32(c.v, a.v, b.v) }; }
inline VecF vfnmadd(VecF a, VecF b, VecF c) { return { vfmsq_f32(c.v, a.v, b.v) }; }
// Written as compare-and-select rather than vminq_f32, whose NaN rule differs from the scalar
// comparison: nothing here should see a NaN, but if one appears all three paths agree on it.
inline MaskF vlt(VecF a, VecF b) { return { vcltq_f32(a.v, b.v) }; }
inline MaskF vgt(VecF a, VecF b) { return { vcgtq_f32(a.v, b.v) }; }
inline MaskF vge(VecF a, VecF b) { return { vcgeq_f32(a.v, b.v) }; }
inline MaskF operator&(MaskF a, MaskF b) { return { vandq_u32(a.v, b.v) }; }
inline MaskF operator|(MaskF a, MaskF b) { return { vorrq_u32(a.v, b.v) }; }
inline VecF vselect(MaskF m, VecF t, VecF f) { return { vbslq_f32(m.v, t.v, f.v) }; }
inline VecF vmin(VecF a, VecF b) { return vselect(vlt(a, b), a, b); }
inline VecF vmax(VecF a, VecF b) { return vselect(vgt(a, b), a, b); }
inline VecF vsqrt(VecF a) { return { vsqrtq_f32(a.v) }; }
inline VecF vabs(VecF a) { return { vabsq_f32(a.v) }; }
inline VecF vfloor(VecF a) { return { vrndmq_f32(a.v) }; }
inline float laneOf(VecF a, int i) { float t[4]; vst1q_f32(t, a.v); return t[i]; }
inline float sumOrdered(VecF a)
{
    float t[4];
    vst1q_f32(t, a.v);
    return ((t[0] + t[1]) + t[2]) + t[3];
}

#else
// ------------------------------------------------------------------------------------------------
// Scalar: the lane type is float itself.
// ------------------------------------------------------------------------------------------------
constexpr int kVecWidth = 1;
constexpr const char* kVecPathName = "scalar";
using VecF = float;   ///< one lane
using MaskF = bool;   ///< one lane mask
#endif

/** @brief A constant in lane type @p V. */
template <class V> inline V lanes(float x) { return vset1(x, V{}); }
/** @brief Loads laneWidth<V>() floats from @p p. */
template <class V> inline V loadLanes(const float* p) { return vloadAs(p, V{}); }

/**
 * @brief Array length rounded up to the widest register, so lane arrays work on every path.
 * @param n number of lanes actually used
 */
constexpr int paddedLanes(int n) { return (n + 7) & ~7; }

} // namespace eph
