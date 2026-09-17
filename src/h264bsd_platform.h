/*
 * Copyright (C) 2009 The Android Open Source Project
 * Modified for use by h264bsd standalone library
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*------------------------------------------------------------------------------

    Platform abstraction for the hot decoding loops.

    Everything here has two implementations with identical results:

      * an Arm one that maps to single Armv7E-M / Armv8-M Mainline
        instructions (Cortex-M4/M7/M33/M55/M85: USAT, CLZ, REV, UXTB16,
        SADD16/SSUB16, SMLAD, UHADD8, PKHBT/PKHTB, unaligned LDR/STR), and
      * a portable C one used by every other target (x86, wasm, MSVC, ...).

    The portable versions are exact bit-for-bit emulations of the packed
    16-bit lane / 8-bit lane arithmetic, so the same algorithms run (and are
    tested) on the host as on the microcontroller.

    Build-time knobs:
      H264BSD_NO_ARM_SIMD   force the portable code on Arm
      H264BSD_FAST_CODE     attribute put on the hottest functions, e.g.
                            -DH264BSD_FAST_CODE=__ramfunc on Zephyr to run
                            them from TCM/SRAM instead of slow flash
      H264BSD_FAST_DATA     same for the hottest lookup tables

------------------------------------------------------------------------------*/

#ifndef H264BSD_PLATFORM_H
#define H264BSD_PLATFORM_H

#include <string.h>
#include "basetype.h"

#if defined(_MSC_VER)
#define H264BSD_INLINE static __inline
#include <stdlib.h>
#include <intrin.h>
#else
#define H264BSD_INLINE static inline
#endif

#ifndef H264BSD_FAST_CODE
#define H264BSD_FAST_CODE
#endif
#ifndef H264BSD_FAST_DATA
#define H264BSD_FAST_DATA
#endif

#if defined(__GNUC__)
#define H264BSD_LIKELY(x)   __builtin_expect(!!(x), 1)
#define H264BSD_UNLIKELY(x) __builtin_expect(!!(x), 0)
/* force inlining of kernels called with constant mode arguments so the
 * mode tests are resolved at compile time */
#define H264BSD_ALWAYS_INLINE static inline __attribute__((always_inline))
#else
#define H264BSD_LIKELY(x)   (x)
#define H264BSD_UNLIKELY(x) (x)
#define H264BSD_ALWAYS_INLINE H264BSD_INLINE
#endif

/* Arm 32-bit with the DSP/SIMD32 extension and saturating instructions:
 * Cortex-M4, M7, M33, M35P, M55, M85 and Cortex-A/R in AArch32 mode. */
#if !defined(H264BSD_NO_ARM_SIMD) && defined(__GNUC__) && \
    defined(__ARM_32BIT_STATE) && defined(__ARM_FEATURE_SIMD32) && \
    defined(__ARM_FEATURE_SAT)
#define H264BSD_ARM_SIMD32 1
#include <arm_acle.h>
#else
#define H264BSD_ARM_SIMD32 0
#endif

/* Which implementation of the pixel kernels to build:
 *   H264BSD_PACKED_KERNELS    sub-pixel interpolation / residual add on packed
 *                             16-bit lanes (single DSP instructions on Arm,
 *                             emulated in C elsewhere)
 *   H264BSD_INLOOP_DEBLOCKING deblock each macroblock row as soon as it is
 *                             complete instead of a second pass over the
 *                             whole picture (keeps the working set small on
 *                             cores with little or no cache)
 * Both default to on for Arm cores with the DSP extension and to off (the
 * original scalar code, which is faster on wide out-of-order CPUs) elsewhere.
 * Either setting can be forced from the command line; the decoded output is
 * bit-exact in every combination. */
#ifndef H264BSD_PACKED_KERNELS
#define H264BSD_PACKED_KERNELS H264BSD_ARM_SIMD32
#endif
#ifndef H264BSD_INLOOP_DEBLOCKING
#define H264BSD_INLOOP_DEBLOCKING H264BSD_ARM_SIMD32
#endif

/*------------------------------------------------------------------------------
    Scalar helpers
------------------------------------------------------------------------------*/

/* Number of leading zeros, 32 for x == 0 */
H264BSD_INLINE u32 h264bsdClz32(u32 x)
{
#if defined(__GNUC__)
    return x ? (u32)__builtin_clz(x) : 32u;
#elif defined(_MSC_VER)
    unsigned long idx;
    return _BitScanReverse(&idx, x) ? 31u - (u32)idx : 32u;
#else
    u32 n = 0;
    if (!x) return 32;
    while (!(x & 0x80000000u)) { x <<= 1; n++; }
    return n;
#endif
}

H264BSD_INLINE u32 h264bsdBswap32(u32 x)
{
#if defined(__GNUC__)
    return __builtin_bswap32(x);
#elif defined(_MSC_VER)
    return _byteswap_ulong(x);
#else
    return (x >> 24) | ((x >> 8) & 0xFF00u) | ((x << 8) & 0xFF0000u) | (x << 24);
#endif
}

/* Unaligned 32-bit load/store in native byte order. memcpy() lets the
 * compiler pick the best sequence: a single (unaligned) LDR/STR on
 * Cortex-M3 and later, byte accesses on cores without unaligned support. */
H264BSD_INLINE u32 h264bsdLoadU32(const u8 *p)
{
    u32 w;
    memcpy(&w, p, 4);
    return w;
}

H264BSD_INLINE void h264bsdStoreU32(u8 *p, u32 w)
{
    memcpy(p, &w, 4);
}

H264BSD_INLINE u32 h264bsdLoadU16(const u8 *p)
{
    u16 w;
    memcpy(&w, p, 2);
    return w;
}

H264BSD_INLINE void h264bsdStoreU16(u8 *p, u32 w)
{
    u16 v = (u16)w;
    memcpy(p, &v, 2);
}

/* 32 bits of the bitstream, first byte in the MSB (one LDR + REV on Arm) */
H264BSD_INLINE u32 h264bsdLoadBe32(const u8 *p)
{
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
    return h264bsdBswap32(h264bsdLoadU32(p));
#elif defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return h264bsdLoadU32(p);
#else
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
#endif
}

/* Clip to [0,255] (USAT #8 on Arm, replaces the 1280-byte lookup table) */
H264BSD_INLINE u32 h264bsdClip255(i32 x)
{
#if H264BSD_ARM_SIMD32
    return __usat(x, 8);
#else
    return ((u32)x <= 255u) ? (u32)x : (x < 0 ? 0u : 255u);
#endif
}

/* Clip (x >> sh) to [0,255]; a single "usat rd, #8, rn, asr #sh" on Arm */
#if H264BSD_ARM_SIMD32
#define H264BSD_CLIP255_ASR(x, sh) ((u32)__usat((i32)(x) >> (sh), 8))
#else
#define H264BSD_CLIP255_ASR(x, sh) h264bsdClip255((i32)(x) >> (sh))
#endif

/*------------------------------------------------------------------------------
    Packed 8-bit x4 / 16-bit x2 lane helpers (Armv6 SIMD / Cortex-M DSP)
------------------------------------------------------------------------------*/

/* rotate right by 8 */
H264BSD_INLINE u32 h264bsdRor8(u32 w)
{
    return (w >> 8) | (w << 24);
}

/* bytes 0 and 2 of w as two 16-bit lanes */
H264BSD_INLINE u32 h264bsdUxtb16(u32 w)
{
#if H264BSD_ARM_SIMD32
    return __uxtb16(w);
#else
    return w & 0x00FF00FFu;
#endif
}

/* bytes 1 and 3 of w as two 16-bit lanes */
H264BSD_INLINE u32 h264bsdUxtb16Ror8(u32 w)
{
#if H264BSD_ARM_SIMD32
    u32 r;
    __asm__("uxtb16 %0, %1, ror #8" : "=r"(r) : "r"(w));
    return r;
#else
    return (w >> 8) & 0x00FF00FFu;
#endif
}

/* lane-wise signed 16-bit add / subtract (no carry between lanes) */
H264BSD_INLINE u32 h264bsdSadd16(u32 a, u32 b)
{
#if H264BSD_ARM_SIMD32
    return (u32)__sadd16((i32)a, (i32)b);
#else
    return ((a + b) & 0x0000FFFFu) | (((a >> 16) + (b >> 16)) << 16);
#endif
}

H264BSD_INLINE u32 h264bsdSsub16(u32 a, u32 b)
{
#if H264BSD_ARM_SIMD32
    return (u32)__ssub16((i32)a, (i32)b);
#else
    return ((a - b) & 0x0000FFFFu) | (((a >> 16) - (b >> 16)) << 16);
#endif
}

/* dual 16-bit multiply, results added: lo(a)*lo(b) + hi(a)*hi(b) (+ acc) */
H264BSD_INLINE i32 h264bsdSmuad(u32 a, u32 b)
{
#if H264BSD_ARM_SIMD32
    return __smuad((i32)a, (i32)b);
#else
    return (i32)(i16)(a & 0xFFFFu) * (i32)(i16)(b & 0xFFFFu) +
           (i32)(i16)(a >> 16) * (i32)(i16)(b >> 16);
#endif
}

H264BSD_INLINE i32 h264bsdSmlad(u32 a, u32 b, i32 acc)
{
#if H264BSD_ARM_SIMD32
    return __smlad((i32)a, (i32)b, acc);
#else
    return acc + h264bsdSmuad(a, b);
#endif
}

/* acc + lo16(a) * lo16(b) and acc + hi16(a) * lo16(b), all signed */
H264BSD_INLINE i32 h264bsdSmlabb(u32 a, u32 b, i32 acc)
{
#if H264BSD_ARM_SIMD32
    return __smlabb((i32)a, (i32)b, acc);
#else
    return acc + (i32)(i16)(a & 0xFFFFu) * (i32)(i16)(b & 0xFFFFu);
#endif
}

H264BSD_INLINE i32 h264bsdSmlatb(u32 a, u32 b, i32 acc)
{
#if H264BSD_ARM_SIMD32
    return __smlatb((i32)a, (i32)b, acc);
#else
    return acc + (i32)(i16)(a >> 16) * (i32)(i16)(b & 0xFFFFu);
#endif
}

/* lane-wise acc + bytes 0 and 2 of w, and acc + bytes 1 and 3 of w */
H264BSD_INLINE u32 h264bsdUxtab16(u32 acc, u32 w)
{
#if H264BSD_ARM_SIMD32
    return __uxtab16(acc, w);
#else
    return (((acc & 0xFFFFu) + (w & 0xFFu)) & 0xFFFFu) |
           (((acc >> 16) + ((w >> 16) & 0xFFu)) << 16);
#endif
}

H264BSD_INLINE u32 h264bsdUxtab16Ror8(u32 acc, u32 w)
{
#if H264BSD_ARM_SIMD32
    u32 r;
    __asm__("uxtab16 %0, %1, %2, ror #8" : "=r"(r) : "r"(acc), "r"(w));
    return r;
#else
    return (((acc & 0xFFFFu) + ((w >> 8) & 0xFFu)) & 0xFFFFu) |
           (((acc >> 16) + ((w >> 24) & 0xFFu)) << 16);
#endif
}

/* saturate both signed 16-bit lanes to [0, 2^n - 1] */
#if H264BSD_ARM_SIMD32
#define H264BSD_USAT16(x, n) ((u32)__usat16((i32)(x), (n)))
#else
H264BSD_INLINE u32 h264bsdUsat16C(u32 x, u32 n)
{
    i32 lo = (i32)(i16)(x & 0xFFFFu);
    i32 hi = (i32)(i16)(x >> 16);
    i32 max = (i32)((1u << n) - 1u);
    lo = lo < 0 ? 0 : (lo > max ? max : lo);
    hi = hi < 0 ? 0 : (hi > max ? max : hi);
    return (u32)lo | ((u32)hi << 16);
}
#define H264BSD_USAT16(x, n) h264bsdUsat16C((u32)(x), (n))
#endif

/* byte-wise (a + b) >> 1 */
H264BSD_INLINE u32 h264bsdUhadd8(u32 a, u32 b)
{
#if H264BSD_ARM_SIMD32
    return __uhadd8(a, b);
#else
    return (a & b) + (((a ^ b) >> 1) & 0x7F7F7F7Fu);
#endif
}

/* byte-wise (a + b + 1) >> 1, the H.264 quarter-sample average */
H264BSD_INLINE u32 h264bsdUrhadd8(u32 a, u32 b)
{
#if H264BSD_ARM_SIMD32
    return __uhadd8(a, b) + ((a ^ b) & 0x01010101u);
#else
    return (a | b) - (((a ^ b) >> 1) & 0x7F7F7F7Fu);
#endif
}

/* lo16(a) | lo16(b) << 16 */
H264BSD_INLINE u32 h264bsdPkhbt(u32 a, u32 b)
{
#if H264BSD_ARM_SIMD32
    u32 r;
    __asm__("pkhbt %0, %1, %2, lsl #16" : "=r"(r) : "r"(a), "r"(b));
    return r;
#else
    return (a & 0xFFFFu) | (b << 16);
#endif
}

/* lo16(a) | hi16(b) (as the high lane) */
H264BSD_INLINE u32 h264bsdPkhbt0(u32 a, u32 b)
{
#if H264BSD_ARM_SIMD32
    u32 r;
    __asm__("pkhbt %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
    return r;
#else
    return (a & 0xFFFFu) | (b & 0xFFFF0000u);
#endif
}

/* hi16(a) as the high lane | hi16(b) as the low lane */
H264BSD_INLINE u32 h264bsdPkhtb(u32 a, u32 b)
{
#if H264BSD_ARM_SIMD32
    u32 r;
    __asm__("pkhtb %0, %1, %2, asr #16" : "=r"(r) : "r"(a), "r"(b));
    return r;
#else
    return (a & 0xFFFF0000u) | (b >> 16);
#endif
}

/* combine four 8-bit values (each already in [0,255]) into a word */
H264BSD_INLINE u32 h264bsdPack4(u32 b0, u32 b1, u32 b2, u32 b3)
{
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

/* even lanes (x, x+2) and odd lanes (x+1, x+3) each holding a byte value
 * back into 4 bytes */
H264BSD_INLINE u32 h264bsdPackLanes(u32 even, u32 odd)
{
    return even | (odd << 8);
}

/*------------------------------------------------------------------------------
    Decoder building blocks built from the helpers above
------------------------------------------------------------------------------*/

/* Four prediction samples (one word) plus four residuals (in [-512,511]),
 * clipped to [0,255] and packed back into a word. */
H264BSD_INLINE u32 h264bsdAddResidualWord(u32 pred, const i32 *res)
{
    u32 even = h264bsdSadd16(h264bsdUxtb16(pred),
                             h264bsdPkhbt((u32)res[0], (u32)res[2]));
    u32 odd  = h264bsdSadd16(h264bsdUxtb16Ror8(pred),
                             h264bsdPkhbt((u32)res[1], (u32)res[3]));
    even = H264BSD_USAT16(even, 8);
    odd  = H264BSD_USAT16(odd, 8);
    return h264bsdPackLanes(even, odd);
}

/* Clip two 16-bit lanes holding a 6-tap sum plus its rounding offset:
 * lane = clip((sum + 16) >> 5) for luma (n = 13, shift 5) or
 * lane = clip((sum + 512) >> 10) for the 2D case (n = 18, shift 10).
 * Result lanes hold bytes, ready for h264bsdPackLanes(). */
#define H264BSD_LANES_CLIP_SHR5(x) ((H264BSD_USAT16((x), 13) >> 5) & 0x00FF00FFu)

#endif /* H264BSD_PLATFORM_H */
