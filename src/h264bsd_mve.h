/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Helium (M-profile Vector Extension) versions of the kernels that walk whole
 * rows of pixels. Every tap of a horizontal filter is a run of consecutive
 * bytes, so those need no permute, which matters because MVE has none at all.
 * A vertical edge runs down the rows instead and is gathered.
 */

#ifndef H264BSD_MVE_H
#define H264BSD_MVE_H

#include "basetype.h"

/*
 * Bit 0 of __ARM_FEATURE_MVE is the integer subset, which is all these
 * kernels need. Anything else compiles the scalar code unchanged.
 */
#if defined(__ARM_FEATURE_MVE) && (((__ARM_FEATURE_MVE) & 1) != 0)

#define H264BSD_HAS_MVE 1

#include <arm_mve.h>

/* (p0 + q0 + 1) >> 1, the rounded average both p1 and q1 corrections use */
static inline int16x8_t mve_avg_pq(int16x8_t p0, int16x8_t q0)
{
	return vshrq_n_s16(vaddq_n_s16(vaddq_s16(p0, q0), 1), 1);
}

static inline int16x8_t mve_clip3(int16x8_t v, int16x8_t lo, int16x8_t hi)
{
	return vminq_s16(vmaxq_s16(v, lo), hi);
}

static inline int16x8_t mve_clip255(int16x8_t v)
{
	return vminq_s16(vmaxq_s16(v, vdupq_n_s16(0)), vdupq_n_s16(255));
}

/*
 * One pass of eight of the sixteen columns of a horizontal luma edge, for
 * bS < 4. Mirrors the scalar loop exactly, including that the clip bound is
 * tc0 raised by one for each of p1 and q1 that gets filtered.
 */
static inline void mve_filter_hor_luma8(u8 *d, i32 stride, int16_t tc0,
					int16_t alpha, int16_t beta, unsigned n)
{
	mve_pred16_t lanes = vctp16q(n);
	int16x8_t p2 = (int16x8_t)vldrbq_z_u16(d - stride * 3, lanes);
	int16x8_t p1 = (int16x8_t)vldrbq_z_u16(d - stride * 2, lanes);
	int16x8_t p0 = (int16x8_t)vldrbq_z_u16(d - stride, lanes);
	int16x8_t q0 = (int16x8_t)vldrbq_z_u16(d, lanes);
	int16x8_t q1 = (int16x8_t)vldrbq_z_u16(d + stride, lanes);
	int16x8_t q2 = (int16x8_t)vldrbq_z_u16(d + stride * 2, lanes);
	int16x8_t valpha = vdupq_n_s16(alpha);
	int16x8_t vbeta = vdupq_n_s16(beta);
	int16x8_t avg = mve_avg_pq(p0, q0);
	int16x8_t tcv = vdupq_n_s16(tc0);
	int16x8_t tmpv = tcv;
	mve_pred16_t filt, ap, aq;
	int16x8_t val, delta;

	filt = lanes & vcmpltq_s16(vabdq_s16(p0, q0), valpha) &
	       vcmpltq_s16(vabdq_s16(p1, p0), vbeta) &
	       vcmpltq_s16(vabdq_s16(q1, q0), vbeta);
	if (filt == 0) {
		return;
	}

	ap = filt & vcmpltq_s16(vabdq_s16(p2, p0), vbeta);
	aq = filt & vcmpltq_s16(vabdq_s16(q2, q0), vbeta);

	/* p1' = p1 + clip(-tc0, tc0, (p2 + avg - 2*p1) >> 1) */
	val = vshrq_n_s16(vsubq_s16(vaddq_s16(p2, avg), vshlq_n_s16(p1, 1)), 1);
	val = mve_clip3(val, vnegq_s16(tcv), tcv);
	vstrbq_p_u16(d - stride * 2, (uint16x8_t)vaddq_s16(p1, val), ap);

	/* q1' = q1 + clip(-tc0, tc0, (q2 + avg - 2*q1) >> 1) */
	val = vshrq_n_s16(vsubq_s16(vaddq_s16(q2, avg), vshlq_n_s16(q1, 1)), 1);
	val = mve_clip3(val, vnegq_s16(tcv), tcv);
	vstrbq_p_u16(d + stride, (uint16x8_t)vaddq_s16(q1, val), aq);

	tmpv = vaddq_m_n_s16(tmpv, tmpv, 1, ap);
	tmpv = vaddq_m_n_s16(tmpv, tmpv, 1, aq);

	/* delta = clip(-tmp, tmp, (((q0 - p0) << 2) + (p1 - q1) + 4) >> 3) */
	val = vshrq_n_s16(vaddq_n_s16(vaddq_s16(vshlq_n_s16(vsubq_s16(q0, p0), 2),
					        vsubq_s16(p1, q1)), 4), 3);
	delta = mve_clip3(val, vnegq_s16(tmpv), tmpv);

	vstrbq_p_u16(d - stride, (uint16x8_t)mve_clip255(vaddq_s16(p0, delta)), filt);
	vstrbq_p_u16(d, (uint16x8_t)mve_clip255(vsubq_s16(q0, delta)), filt);
}


/*
 * The six-tap half-pel filter, (A - 5B + 20C + 20D - 5E + F + 16) >> 5,
 * across up to eight columns. Every tap is a whole row, so the taps are
 * plain contiguous loads and the filter needs no lane movement at all.
 */
static inline void mve_sixtap_ver8(u8 *dst, const u8 *s, i32 stride, unsigned n)
{
	mve_pred16_t p = vctp16q(n);
	int16x8_t a = (int16x8_t)vldrbq_z_u16(s, p);
	int16x8_t b = (int16x8_t)vldrbq_z_u16(s + stride, p);
	int16x8_t c = (int16x8_t)vldrbq_z_u16(s + stride * 2, p);
	int16x8_t d = (int16x8_t)vldrbq_z_u16(s + stride * 3, p);
	int16x8_t e = (int16x8_t)vldrbq_z_u16(s + stride * 4, p);
	int16x8_t f = (int16x8_t)vldrbq_z_u16(s + stride * 5, p);
	int16x8_t near = vaddq_s16(c, d);
	int16x8_t far = vaddq_s16(b, e);
	int16x8_t acc = vaddq_n_s16(vaddq_s16(a, f), 16);

	/* 20*near - 5*far, as shifts: (x<<4)+(x<<2) and (x<<2)+x */
	acc = vaddq_s16(acc, vaddq_s16(vshlq_n_s16(near, 4), vshlq_n_s16(near, 2)));
	acc = vsubq_s16(acc, vaddq_s16(vshlq_n_s16(far, 2), far));

	vstrbq_p_u16(dst, (uint16x8_t)mve_clip255(vshrq_n_s16(acc, 5)), p);
}


/*
 * Chroma is bilinear: out = (a*A + b*B + c*C + d*D + 32) >> 6 with the four
 * weights summing to 64, so everything stays inside int16. One row of a
 * chroma partition is at most eight samples, which is exactly one vector.
 */
static inline void mve_chroma_bilin8(u8 *dst, const u8 *s, i32 stride,
				     unsigned n, int16_t wx, int16_t wy)
{
	mve_pred16_t p = vctp16q(n);
	int16x8_t a = (int16x8_t)vldrbq_z_u16(s, p);
	int16x8_t b = (int16x8_t)vldrbq_z_u16(s + 1, p);
	int16x8_t c = (int16x8_t)vldrbq_z_u16(s + stride, p);
	int16x8_t e = (int16x8_t)vldrbq_z_u16(s + stride + 1, p);
	int16_t ix = (int16_t)(8 - wx);
	int16_t iy = (int16_t)(8 - wy);
	int16x8_t acc = vdupq_n_s16(32);

	acc = vmlaq_n_s16(acc, a, (int16_t)(ix * iy));
	acc = vmlaq_n_s16(acc, b, (int16_t)(wx * iy));
	acc = vmlaq_n_s16(acc, c, (int16_t)(ix * wy));
	acc = vmlaq_n_s16(acc, e, (int16_t)(wx * wy));

	vstrbq_p_u16(dst, (uint16x8_t)vshrq_n_s16(acc, 6), p);
}


/*
 * The bS == 4 strong filter, eight columns at a time. Every result is an
 * average of pixel values so nothing can leave 0..255 and no clipping is
 * needed. The two sides are independent, and both the strong and the weak
 * form of each side are computed from the original samples, so the two
 * mutually exclusive predicates just select which store lands.
 */
static inline void mve_filter_hor_luma8_bs4(u8 *d, i32 stride, int16_t alpha,
					    int16_t beta)
{
	int16x8_t p3 = (int16x8_t)vldrbq_u16(d - stride * 4);
	int16x8_t p2 = (int16x8_t)vldrbq_u16(d - stride * 3);
	int16x8_t p1 = (int16x8_t)vldrbq_u16(d - stride * 2);
	int16x8_t p0 = (int16x8_t)vldrbq_u16(d - stride);
	int16x8_t q0 = (int16x8_t)vldrbq_u16(d);
	int16x8_t q1 = (int16x8_t)vldrbq_u16(d + stride);
	int16x8_t q2 = (int16x8_t)vldrbq_u16(d + stride * 2);
	int16x8_t q3 = (int16x8_t)vldrbq_u16(d + stride * 3);
	int16x8_t vbeta = vdupq_n_s16(beta);
	int16x8_t dpq = vabdq_s16(p0, q0);
	mve_pred16_t filt, small, sp, sq, tmp;
	int16x8_t sum, v;

	filt = vcmpltq_s16(dpq, vdupq_n_s16(alpha)) &
	       vcmpltq_s16(vabdq_s16(p1, p0), vbeta) &
	       vcmpltq_s16(vabdq_s16(q1, q0), vbeta);
	if (filt == 0) {
		return;
	}

	small = vcmpltq_s16(dpq, vdupq_n_s16((int16_t)((alpha >> 2) + 2)));
	sp = filt & small & vcmpltq_s16(vabdq_s16(p2, p0), vbeta);
	sq = filt & small & vcmpltq_s16(vabdq_s16(q2, q0), vbeta);

	/* p side, strong: uses p1 + p0 + q0 */
	sum = vaddq_s16(vaddq_s16(p1, p0), q0);
	v = vshrq_n_s16(vaddq_n_s16(vaddq_s16(vaddq_s16(p2, vshlq_n_s16(sum, 1)), q1), 4), 3);
	vstrbq_p_u16(d - stride, (uint16x8_t)v, sp);
	v = vshrq_n_s16(vaddq_n_s16(vaddq_s16(p2, sum), 2), 2);
	vstrbq_p_u16(d - stride * 2, (uint16x8_t)v, sp);
	v = vaddq_s16(vshlq_n_s16(p3, 1), vaddq_s16(vshlq_n_s16(p2, 1), p2));
	v = vshrq_n_s16(vaddq_n_s16(vaddq_s16(v, sum), 4), 3);
	vstrbq_p_u16(d - stride * 3, (uint16x8_t)v, sp);

	/* p side, weak: (2*p1 + p0 + q1 + 2) >> 2 */
	tmp = filt & (mve_pred16_t)~sp;
	v = vshrq_n_s16(vaddq_n_s16(vaddq_s16(vshlq_n_s16(p1, 1), vaddq_s16(p0, q1)), 2), 2);
	vstrbq_p_u16(d - stride, (uint16x8_t)v, tmp);

	/* q side, strong: uses p0 + q0 + q1 */
	sum = vaddq_s16(vaddq_s16(p0, q0), q1);
	v = vshrq_n_s16(vaddq_n_s16(vaddq_s16(vaddq_s16(p1, vshlq_n_s16(sum, 1)), q2), 4), 3);
	vstrbq_p_u16(d, (uint16x8_t)v, sq);
	v = vshrq_n_s16(vaddq_n_s16(vaddq_s16(sum, q2), 2), 2);
	vstrbq_p_u16(d + stride, (uint16x8_t)v, sq);
	v = vaddq_s16(vshlq_n_s16(q3, 1), vaddq_s16(vshlq_n_s16(q2, 1), q2));
	v = vshrq_n_s16(vaddq_n_s16(vaddq_s16(v, sum), 4), 3);
	vstrbq_p_u16(d + stride * 2, (uint16x8_t)v, sq);

	/* q side, weak: (2*q1 + q0 + p1 + 2) >> 2 */
	tmp = filt & (mve_pred16_t)~sq;
	v = vshrq_n_s16(vaddq_n_s16(vaddq_s16(vshlq_n_s16(q1, 1), vaddq_s16(q0, p1)), 2), 2);
	vstrbq_p_u16(d, (uint16x8_t)v, tmp);
}


/*
 * The same six-tap filter along a row. MVE has no way to shift a vector by a
 * variable number of lanes, but a byte load is legal at any address, so the
 * six taps are simply six overlapping loads one byte apart.
 */
static inline int16x8_t mve_sixtap_hor_calc(const u8 *s, mve_pred16_t p)
{
	int16x8_t a = (int16x8_t)vldrbq_z_u16(s, p);
	int16x8_t b = (int16x8_t)vldrbq_z_u16(s + 1, p);
	int16x8_t c = (int16x8_t)vldrbq_z_u16(s + 2, p);
	int16x8_t d = (int16x8_t)vldrbq_z_u16(s + 3, p);
	int16x8_t e = (int16x8_t)vldrbq_z_u16(s + 4, p);
	int16x8_t f = (int16x8_t)vldrbq_z_u16(s + 5, p);
	int16x8_t near = vaddq_s16(c, d);
	int16x8_t far = vaddq_s16(b, e);
	int16x8_t acc = vaddq_n_s16(vaddq_s16(a, f), 16);

	acc = vaddq_s16(acc, vaddq_s16(vshlq_n_s16(near, 4), vshlq_n_s16(near, 2)));
	acc = vsubq_s16(acc, vaddq_s16(vshlq_n_s16(far, 2), far));

	return mve_clip255(vshrq_n_s16(acc, 5));
}

static inline void mve_sixtap_hor8(u8 *dst, const u8 *s, unsigned n)
{
	mve_pred16_t p = vctp16q(n);

	vstrbq_p_u16(dst, (uint16x8_t)mve_sixtap_hor_calc(s, p), p);
}

/* Quarter-pel is the half-pel sample averaged with the integer one */
static inline void mve_sixtap_hor_quarter8(u8 *dst, const u8 *s, unsigned n,
					   unsigned off)
{
	mve_pred16_t p = vctp16q(n);
	int16x8_t half = mve_sixtap_hor_calc(s, p);
	int16x8_t integ = (int16x8_t)vldrbq_z_u16(s + 2 + off, p);
	int16x8_t v = vshrq_n_s16(vaddq_n_s16(vaddq_s16(half, integ), 1), 1);

	vstrbq_p_u16(dst, (uint16x8_t)v, p);
}

static inline int16x8_t mve_sixtap_ver_calc(const u8 *s, i32 stride, mve_pred16_t p)
{
	int16x8_t a = (int16x8_t)vldrbq_z_u16(s, p);
	int16x8_t b = (int16x8_t)vldrbq_z_u16(s + stride, p);
	int16x8_t c = (int16x8_t)vldrbq_z_u16(s + stride * 2, p);
	int16x8_t d = (int16x8_t)vldrbq_z_u16(s + stride * 3, p);
	int16x8_t e = (int16x8_t)vldrbq_z_u16(s + stride * 4, p);
	int16x8_t f = (int16x8_t)vldrbq_z_u16(s + stride * 5, p);
	int16x8_t near = vaddq_s16(c, d);
	int16x8_t far = vaddq_s16(b, e);
	int16x8_t acc = vaddq_n_s16(vaddq_s16(a, f), 16);

	acc = vaddq_s16(acc, vaddq_s16(vshlq_n_s16(near, 4), vshlq_n_s16(near, 2)));
	acc = vsubq_s16(acc, vaddq_s16(vshlq_n_s16(far, 2), far));

	return mve_clip255(vshrq_n_s16(acc, 5));
}

static inline void mve_sixtap_ver_quarter8(u8 *dst, const u8 *s, i32 stride,
					   unsigned n, unsigned off)
{
	mve_pred16_t p = vctp16q(n);
	int16x8_t half = mve_sixtap_ver_calc(s, stride, p);
	int16x8_t integ = (int16x8_t)vldrbq_z_u16(s + stride * (2 + (i32)off), p);
	int16x8_t v = vshrq_n_s16(vaddq_n_s16(vaddq_s16(half, integ), 1), 1);

	vstrbq_p_u16(dst, (uint16x8_t)v, p);
}


/*
 * Pixels e, g, p and r: the average of the horizontal and the vertical
 * half-pel sample. The scalar code makes two passes over the block, writing
 * the horizontal result out and reading it back; both fit in registers here.
 */
static inline void mve_horver_quarter8(u8 *dst, const u8 *ref, i32 stride,
				       u32 r, u32 x, unsigned n, unsigned hv)
{
	mve_pred16_t p = vctp16q(n);
	const u8 *hs = ref + (i32)(r + 2U + ((hv & 2U) >> 1)) * stride + (i32)x;
	const u8 *vs = ref + (i32)r * stride + (i32)(x + 2U + (hv & 1U));
	int16x8_t h = mve_sixtap_hor_calc(hs, p);
	int16x8_t v = mve_sixtap_ver_calc(vs, stride, p);

	vstrbq_p_u16(dst, (uint16x8_t)vshrq_n_s16(vaddq_n_s16(vaddq_s16(h, v), 1), 1), p);
}


/*
 * Chroma edges only ever correct p0 and q0, so this is the tail of the luma
 * filter without the p1/q1 half. bS < 4 uses tc0 + 1 as the clip bound.
 */
static inline void mve_filter_hor_chroma8(u8 *d, i32 stride, int16_t tc,
					  int16_t alpha, int16_t beta, unsigned n)
{
	mve_pred16_t lanes = vctp16q(n);
	int16x8_t p1 = (int16x8_t)vldrbq_z_u16(d - stride * 2, lanes);
	int16x8_t p0 = (int16x8_t)vldrbq_z_u16(d - stride, lanes);
	int16x8_t q0 = (int16x8_t)vldrbq_z_u16(d, lanes);
	int16x8_t q1 = (int16x8_t)vldrbq_z_u16(d + stride, lanes);
	int16x8_t vbeta = vdupq_n_s16(beta);
	int16x8_t tcv = vdupq_n_s16(tc);
	mve_pred16_t filt;
	int16x8_t val;

	filt = lanes & vcmpltq_s16(vabdq_s16(p0, q0), vdupq_n_s16(alpha)) &
	       vcmpltq_s16(vabdq_s16(p1, p0), vbeta) &
	       vcmpltq_s16(vabdq_s16(q1, q0), vbeta);
	if (filt == 0) {
		return;
	}

	val = vshrq_n_s16(vaddq_n_s16(vaddq_s16(vshlq_n_s16(vsubq_s16(q0, p0), 2),
					        vsubq_s16(p1, q1)), 4), 3);
	val = mve_clip3(val, vnegq_s16(tcv), tcv);

	vstrbq_p_u16(d - stride, (uint16x8_t)mve_clip255(vaddq_s16(p0, val)), filt);
	vstrbq_p_u16(d, (uint16x8_t)mve_clip255(vsubq_s16(q0, val)), filt);
}

/* bS == 4 on chroma is a plain three-tap average on each side */
static inline void mve_filter_hor_chroma8_bs4(u8 *d, i32 stride, int16_t alpha,
					      int16_t beta, unsigned n)
{
	mve_pred16_t lanes = vctp16q(n);
	int16x8_t p1 = (int16x8_t)vldrbq_z_u16(d - stride * 2, lanes);
	int16x8_t p0 = (int16x8_t)vldrbq_z_u16(d - stride, lanes);
	int16x8_t q0 = (int16x8_t)vldrbq_z_u16(d, lanes);
	int16x8_t q1 = (int16x8_t)vldrbq_z_u16(d + stride, lanes);
	int16x8_t vbeta = vdupq_n_s16(beta);
	mve_pred16_t filt;
	int16x8_t v;

	filt = lanes & vcmpltq_s16(vabdq_s16(p0, q0), vdupq_n_s16(alpha)) &
	       vcmpltq_s16(vabdq_s16(p1, p0), vbeta) &
	       vcmpltq_s16(vabdq_s16(q1, q0), vbeta);
	if (filt == 0) {
		return;
	}

	v = vshrq_n_s16(vaddq_n_s16(vaddq_s16(vshlq_n_s16(p1, 1), vaddq_s16(p0, q1)), 2), 2);
	vstrbq_p_u16(d - stride, (uint16x8_t)v, filt);
	v = vshrq_n_s16(vaddq_n_s16(vaddq_s16(vshlq_n_s16(q1, 1), vaddq_s16(q0, p1)), 2), 2);
	vstrbq_p_u16(d, (uint16x8_t)v, filt);
}


/*
 * A vertical edge runs down the rows, so one lane has to be one row. MVE has
 * no transpose, but a byte gather with 16-bit offsets reaches 65535 bytes and
 * four rows of an 800-wide picture is 2400, so the taps are gathered instead.
 * A gather costs about twice a contiguous load, which is the price of not
 * having a permute.
 */
static inline void mve_filter_ver_luma_edge(u8 *data, i32 stride, int16_t tc0,
					    int16_t alpha, int16_t beta)
{
	uint16x8_t offs = vmulq_n_u16(vidupq_n_u16(0, 1), (uint16_t)stride);
	mve_pred16_t lanes = vctp16q(4);
	int16x8_t p2 = (int16x8_t)vldrbq_gather_offset_z_u16(data - 3, offs, lanes);
	int16x8_t p1 = (int16x8_t)vldrbq_gather_offset_z_u16(data - 2, offs, lanes);
	int16x8_t p0 = (int16x8_t)vldrbq_gather_offset_z_u16(data - 1, offs, lanes);
	int16x8_t q0 = (int16x8_t)vldrbq_gather_offset_z_u16(data, offs, lanes);
	int16x8_t q1 = (int16x8_t)vldrbq_gather_offset_z_u16(data + 1, offs, lanes);
	int16x8_t q2 = (int16x8_t)vldrbq_gather_offset_z_u16(data + 2, offs, lanes);
	int16x8_t vbeta = vdupq_n_s16(beta);
	int16x8_t avg = mve_avg_pq(p0, q0);
	int16x8_t tcv = vdupq_n_s16(tc0);
	int16x8_t tmpv = tcv;
	mve_pred16_t filt, ap, aq;
	int16x8_t val, delta;

	filt = lanes & vcmpltq_s16(vabdq_s16(p0, q0), vdupq_n_s16(alpha)) &
	       vcmpltq_s16(vabdq_s16(p1, p0), vbeta) &
	       vcmpltq_s16(vabdq_s16(q1, q0), vbeta);
	if (filt == 0) {
		return;
	}

	ap = filt & vcmpltq_s16(vabdq_s16(p2, p0), vbeta);
	aq = filt & vcmpltq_s16(vabdq_s16(q2, q0), vbeta);

	val = vshrq_n_s16(vsubq_s16(vaddq_s16(p2, avg), vshlq_n_s16(p1, 1)), 1);
	val = mve_clip3(val, vnegq_s16(tcv), tcv);
	vstrbq_scatter_offset_p_u16(data - 2, offs, (uint16x8_t)vaddq_s16(p1, val), ap);

	val = vshrq_n_s16(vsubq_s16(vaddq_s16(q2, avg), vshlq_n_s16(q1, 1)), 1);
	val = mve_clip3(val, vnegq_s16(tcv), tcv);
	vstrbq_scatter_offset_p_u16(data + 1, offs, (uint16x8_t)vaddq_s16(q1, val), aq);

	tmpv = vaddq_m_n_s16(tmpv, tmpv, 1, ap);
	tmpv = vaddq_m_n_s16(tmpv, tmpv, 1, aq);

	val = vshrq_n_s16(vaddq_n_s16(vaddq_s16(vshlq_n_s16(vsubq_s16(q0, p0), 2),
					        vsubq_s16(p1, q1)), 4), 3);
	delta = mve_clip3(val, vnegq_s16(tmpv), tmpv);

	vstrbq_scatter_offset_p_u16(data - 1, offs,
				    (uint16x8_t)mve_clip255(vaddq_s16(p0, delta)), filt);
	vstrbq_scatter_offset_p_u16(data, offs,
				    (uint16x8_t)mve_clip255(vsubq_s16(q0, delta)), filt);
}

#endif /* __ARM_FEATURE_MVE */

#endif /* H264BSD_MVE_H */
