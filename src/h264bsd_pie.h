/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-S3 PIE versions of the pixel kernels: the 128-bit vector unit of the
 * Xtensa LX7 cores, coprocessor 3. Built when H264BSD_PIE is defined; the
 * kernels themselves are in h264bsd_pie.S and take over from the C code once
 * h264bsdPieInit() has checked them against it.
 *
 * The PIE registers are not part of a thread's context on Zephyr, so a
 * stretch of kernel calls is bracketed by H264BSD_SIMD_ENTER/LEAVE, which
 * keeps the scheduler from switching this CPU to another thread meanwhile
 * (interrupt handlers never touch the unit). A port can define the pair to
 * something else before this header is seen.
 */

#ifndef H264BSD_PIE_H
#define H264BSD_PIE_H

#include "basetype.h"

#ifdef H264BSD_PIE

#define H264BSD_HAS_PIE 1

/* Non-zero once the kernels have been checked against the C code */
extern u32 h264bsdPieOn;

/* The constants the kernels use, 16-byte aligned */
extern const u8 h264bsdPieK[112];

/* Checks every kernel against the C code and enables them if they agree */
void h264bsdPieInit(void);

/* Width and height packed the way the kernels take them */
#define H264BSD_PIE_WH(w, h) ((u32)(w) | ((u32)(h) << 8))

void h264bsdPieLumaH(const u8 *ref, u32 stride, u8 *mb, u32 wh, u32 mode, const u8 *k);
void h264bsdPieLumaV(const u8 *ref, u32 stride, u8 *mb, u32 wh, u32 mode, const u8 *k);
void h264bsdPieLumaHV(const u8 *ref, u32 stride, u8 *mb, u32 wh, u32 mode, const u8 *k);
void h264bsdPieLumaMid(const u8 *ref, u32 stride, u8 *mb, u32 wh, const u8 *avg, const u8 *k);
void h264bsdPieChroma(const u8 *ref, u32 stride, u8 *out, u32 wh, const u8 *coef, const u8 *k);
void h264bsdPieCopy(const u8 *ref, u32 stride, u8 *out, u32 ostride, u32 wh);
void h264bsdPieWriteMb(const u8 *data, u8 *luma, u8 *cb, u8 *cr, u32 stride);

#if !defined(H264BSD_SIMD_ENTER)
#if defined(__ZEPHYR__)
#include <zephyr/kernel.h>
#define H264BSD_SIMD_ENTER() do { k_sched_lock(); h264bsdPieEnable(); } while (0)
#define H264BSD_SIMD_LEAVE() k_sched_unlock()
#else
#define H264BSD_SIMD_ENTER() h264bsdPieEnable()
#define H264BSD_SIMD_LEAVE() ((void)0)
#endif
#endif

/* Turns coprocessor 3 on for this CPU: Zephyr leaves CPENABLE alone on the LX7 */
static inline void h264bsdPieEnable(void)
{
#if defined(__XTENSA__)
    u32 cp;

    __asm__ volatile("rsr.cpenable %0" : "=a"(cp));
    if (!(cp & 8))
    {
        cp |= 8;
        __asm__ volatile("wsr.cpenable %0\n\trsync" :: "a"(cp));
    }
#endif
}

#else /* H264BSD_PIE */

#define H264BSD_SIMD_ENTER() ((void)0)
#define H264BSD_SIMD_LEAVE() ((void)0)

#endif /* H264BSD_PIE */

#endif /* H264BSD_PIE_H */
