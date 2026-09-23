/**
 * @file arm_neon.h
 * @brief NOT the real <arm_neon.h>: the AArch64 NEON intrinsics Phosphene uses, written out lane
 *        by lane after their ACLE definitions, so an x86 test build (EPH_NEON_SHIM) runs the NEON
 *        code paths instead of skipping them.
 *
 * Per lane the arithmetic is what the hardware does: add, subtract, multiply, divide and square
 * root are single IEEE operations; the fused forms use std::fma, which is what vfmaq_f32 and
 * vfmsq_f32 compute; comparisons produce all-ones or all-zeros masks; vrndmq_f32 rounds towards
 * minus infinity. What this cannot check is whether the real header spells something differently;
 * the NDK build does that.
 *
 * @note Extended from Noctuary `Tests/neonshim/arm_neon.h` at b60a2fe (15.09.2026): masks,
 *       division, square root, absolute value, negation and floor added.
 */
#pragma once
#include <cmath>
#include <cstdint>

typedef float float32_t;                            ///< ACLE scalar type
struct float32x4_t { float32_t lane[4]; };          ///< four float lanes
struct uint32x4_t  { uint32_t  lane[4]; };          ///< four mask lanes

inline float32x4_t vld1q_f32(const float32_t* p) { float32x4_t r; for (int i = 0; i < 4; ++i) r.lane[i] = p[i]; return r; }
inline void vst1q_f32(float32_t* p, float32x4_t a) { for (int i = 0; i < 4; ++i) p[i] = a.lane[i]; }
inline float32x4_t vdupq_n_f32(float32_t x) { float32x4_t r; for (int i = 0; i < 4; ++i) r.lane[i] = x; return r; }
inline float32x4_t vaddq_f32(float32x4_t a, float32x4_t b) { for (int i = 0; i < 4; ++i) a.lane[i] += b.lane[i]; return a; }
inline float32x4_t vsubq_f32(float32x4_t a, float32x4_t b) { for (int i = 0; i < 4; ++i) a.lane[i] -= b.lane[i]; return a; }
inline float32x4_t vmulq_f32(float32x4_t a, float32x4_t b) { for (int i = 0; i < 4; ++i) a.lane[i] *= b.lane[i]; return a; }
inline float32x4_t vdivq_f32(float32x4_t a, float32x4_t b) { for (int i = 0; i < 4; ++i) a.lane[i] /= b.lane[i]; return a; }
inline float32x4_t vnegq_f32(float32x4_t a) { for (int i = 0; i < 4; ++i) a.lane[i] = -a.lane[i]; return a; }
inline float32x4_t vabsq_f32(float32x4_t a) { for (int i = 0; i < 4; ++i) a.lane[i] = std::fabs(a.lane[i]); return a; }
inline float32x4_t vsqrtq_f32(float32x4_t a) { for (int i = 0; i < 4; ++i) a.lane[i] = std::sqrt(a.lane[i]); return a; }
inline float32x4_t vrndmq_f32(float32x4_t a) { for (int i = 0; i < 4; ++i) a.lane[i] = std::floor(a.lane[i]); return a; }

/** @brief a + b c, fused. */
inline float32x4_t vfmaq_f32(float32x4_t a, float32x4_t b, float32x4_t c) { for (int i = 0; i < 4; ++i) a.lane[i] = std::fma(b.lane[i], c.lane[i], a.lane[i]); return a; }
/** @brief a - b c, fused. */
inline float32x4_t vfmsq_f32(float32x4_t a, float32x4_t b, float32x4_t c) { for (int i = 0; i < 4; ++i) a.lane[i] = std::fma(-b.lane[i], c.lane[i], a.lane[i]); return a; }

inline uint32x4_t vcltq_f32(float32x4_t a, float32x4_t b) { uint32x4_t r; for (int i = 0; i < 4; ++i) r.lane[i] = a.lane[i] <  b.lane[i] ? 0xFFFFFFFFu : 0u; return r; }
inline uint32x4_t vcgtq_f32(float32x4_t a, float32x4_t b) { uint32x4_t r; for (int i = 0; i < 4; ++i) r.lane[i] = a.lane[i] >  b.lane[i] ? 0xFFFFFFFFu : 0u; return r; }
inline uint32x4_t vcgeq_f32(float32x4_t a, float32x4_t b) { uint32x4_t r; for (int i = 0; i < 4; ++i) r.lane[i] = a.lane[i] >= b.lane[i] ? 0xFFFFFFFFu : 0u; return r; }
inline uint32x4_t vandq_u32(uint32x4_t a, uint32x4_t b) { for (int i = 0; i < 4; ++i) a.lane[i] &= b.lane[i]; return a; }
inline uint32x4_t vorrq_u32(uint32x4_t a, uint32x4_t b) { for (int i = 0; i < 4; ++i) a.lane[i] |= b.lane[i]; return a; }
/** @brief Bitwise select: bits of @p t where the mask is set, of @p f elsewhere. */
inline float32x4_t vbslq_f32(uint32x4_t m, float32x4_t t, float32x4_t f)
{
    float32x4_t r;
    for (int i = 0; i < 4; ++i) r.lane[i] = (m.lane[i] & 0x80000000u) ? t.lane[i] : f.lane[i];
    return r;
}
