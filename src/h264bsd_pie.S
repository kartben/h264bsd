/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-S3 PIE (Xtensa LX7 "Processor Instruction Extensions") versions of the
 * pixel kernels: 16 bytes at a time in the eight 128-bit Q registers, with the
 * multiply-accumulates in QACC. See h264bsd_pie.h for the calling conventions;
 * the constants every kernel needs come in through a pointer to h264bsdPieK.
 *
 * Signed pixels: the 8-bit MACs multiply signed lanes, so a pixel is used as
 * p ^ 0x80 = p - 128. A 6-tap sum over such pixels is S - 128 * 32 = S - 4096,
 * and (S - 4096 + 16) >> 5 = ((S + 16) >> 5) - 128, so saturating that to a
 * signed byte and flipping the top bit again is exactly the clipped sample.
 * A rounded average (u + v + 1) >> 1 of two such bytes is the same in either
 * representation, as 256 / 2 = 128.
 *
 * EE.VMULAS.U8 saturates the accumulator as an unsigned value, so all MACs here
 * are the signed ones. EE.SRCMB shifts arithmetically, that is it floors.
 *
 * All the kernels are leaf functions that use a2..a7 for their arguments and
 * a8..a15 as scratch, and every 128-bit load and store below is on an address
 * whose low bits the hardware would ignore anyway.
 */

/* Offsets into h264bsdPieK, see h264bsd_pie.c */
#define K_COEF8   0	/* s8 lanes 1, -5, 20, 20, -5, 1 */
#define K_X80     16	/* 16 x 0x80 */
#define K_R16     32	/* 16 x 16 */
#define K_ONES    48	/* 16 x 1 */
#define K_R32     64	/* 16 x 32 */
#define K_COEF16  80	/* s16 lanes 1, -5, 20, 20, -5, 1, 512, 1 */
#define K_C255    96	/* s16 255 */
#define K_C512    98	/* s16 512 */

/* qd = the 16 bytes at the address in \ap, whatever its alignment. Clobbers a8, a9, \qt */
.macro uload qd, qt, ap
	and	a8, \ap, a14
	extui	a9, \ap, 0, 4
	wur.sar_byte a9
	ee.vld.128.ip	\qd, a8, 16
	ee.vld.128.ip	\qt, a8, 0
	ee.src.q	\qd, \qd, \qt
.endm

/* \w0:\w1 = the 32 bytes at the address in \ap, as signed pixels. Clobbers a8, a9, \qt, \qu */
.macro uload32 w0, w1, qt, qu, ap
	and	a8, \ap, a14
	extui	a9, \ap, 0, 4
	wur.sar_byte a9
	ee.vld.128.ip	\qt, a8, 16
	ee.vld.128.ip	\qu, a8, 16
	ee.vld.128.ip	\w1, a8, 0
	ee.src.q	\w0, \qt, \qu
	ee.src.q	\w1, \qu, \w1
	ee.xorq		\w0, \w0, q7
	ee.xorq		\w1, \w1, q7
.endm

/* One horizontal 6-tap pass over the window \w0:\w1 into QACC (lanes of q6). Clobbers a11, \qt */
.macro htaps w0, w1, qt
	ee.vsmulas.s8.qacc	\w0, q6, 0
	movi	a11, 1
	wur.sar_byte a11
	ee.src.q	\qt, \w0, \w1
	ee.vsmulas.s8.qacc	\qt, q6, 1
	movi	a11, 2
	wur.sar_byte a11
	ee.src.q	\qt, \w0, \w1
	ee.vsmulas.s8.qacc	\qt, q6, 2
	movi	a11, 3
	wur.sar_byte a11
	ee.src.q	\qt, \w0, \w1
	ee.vsmulas.s8.qacc	\qt, q6, 3
	movi	a11, 4
	wur.sar_byte a11
	ee.src.q	\qt, \w0, \w1
	ee.vsmulas.s8.qacc	\qt, q6, 4
	movi	a11, 5
	wur.sar_byte a11
	ee.src.q	\qt, \w0, \w1
	ee.vsmulas.s8.qacc	\qt, q6, 5
.endm

/* Six vertical taps from consecutive 16-byte rows at \ap (advanced by 96) */
.macro vtaps ap, qt
	ee.vld.128.ip	\qt, \ap, 16
	ee.vsmulas.s8.qacc	\qt, q6, 0
	ee.vld.128.ip	\qt, \ap, 16
	ee.vsmulas.s8.qacc	\qt, q6, 1
	ee.vld.128.ip	\qt, \ap, 16
	ee.vsmulas.s8.qacc	\qt, q6, 2
	ee.vld.128.ip	\qt, \ap, 16
	ee.vsmulas.s8.qacc	\qt, q6, 3
	ee.vld.128.ip	\qt, \ap, 16
	ee.vsmulas.s8.qacc	\qt, q6, 4
	ee.vld.128.ip	\qt, \ap, 16
	ee.vsmulas.s8.qacc	\qt, q6, 5
.endm

/* \qa = (\qa + \qb + 1) >> 1 on signed bytes. Clobbers a11, \qt */
.macro avg qa, qb, qt
	ee.vld.128.ip	\qt, a7, 0
	ee.mov.s8.qacc	\qt
	ee.vmulas.s8.qacc	\qa, \qt
	ee.vmulas.s8.qacc	\qb, \qt
	movi	a11, 1
	ee.srcmb.s8.qacc	\qa, a11, 0
.endm

/*
 * Stores the low \w bytes (16, 8 or 4) of \qv at \ap and advances \ap by 16.
 * Widths 16 and 8 need the alignment their stores force. Clobbers a11.
 */
.macro store_w qv, ap, w, label
	bnei	\w, 16, 1f
	ee.vst.128.ip	\qv, \ap, 16
	j	\label
1:	bnei	\w, 8, 2f
	ee.vst.l.64.ip	\qv, \ap, 16
	j	\label
2:	ee.movi.32.a	\qv, a11, 0
	s32i	a11, \ap, 0
	addi	\ap, \ap, 16
.endm

/* Common prologue: q6 = 8-bit coefficient lanes, q7 = 0x80, q0 = rounding 16, a7 -> ones */
.macro consts8
	ee.vld.128.ip	q6, a7, 16
	ee.vld.128.ip	q7, a7, 16
	ee.vld.128.ip	q0, a7, 16
	movi	a14, -16
.endm

	.text

/*
 * void h264bsdPieLumaH(const u8 *ref, u32 stride, u8 *mb, u32 wh, u32 mode,
 *                      const u8 *k)
 *
 * Horizontal 6-tap luma interpolation. ref points two pixels left of the
 * first output; w = wh & 0xFF, h = wh >> 8. mode 0 writes the half sample b,
 * 1 and 2 the quarter samples a and c (b averaged with G, resp. H).
 */
	.align	4
	.global	h264bsdPieLumaH
	.type	h264bsdPieLumaH, @function
h264bsdPieLumaH:
	entry	a1, 32
	consts8
	extui	a10, a5, 8, 8
	extui	a5, a5, 0, 8
	addi	a15, a6, 1
.LH_row:
	uload32	q4, q5, q1, q2, a2
	ee.mov.s8.qacc	q0
	htaps	q4, q5, q1
	movi	a11, 5
	ee.srcmb.s8.qacc	q1, a11, 0
	beqz	a6, .LH_store
	wur.sar_byte a15
	ee.src.q	q2, q4, q5
	avg	q1, q2, q3
.LH_store:
	ee.xorq		q1, q1, q7
	store_w	q1, a4, a5, .LH_next
.LH_next:
	add	a2, a2, a3
	addi	a10, a10, -1
	bnez	a10, .LH_row
	retw.n
	.size	h264bsdPieLumaH, .-h264bsdPieLumaH

/*
 * void h264bsdPieLumaV(const u8 *ref, u32 stride, u8 *mb, u32 wh, u32 mode,
 *                      const u8 *k)
 *
 * Vertical 6-tap luma interpolation. ref points two rows above the first
 * output. mode 0 writes the half sample h, 1 and 2 the quarter samples d and
 * n (h averaged with G, resp. M).
 *
 * The h + 5 source rows are first realigned into a table on the stack, so
 * that each output row is six aligned loads.
 */
	.align	4
	.global	h264bsdPieLumaV
	.type	h264bsdPieLumaV, @function
h264bsdPieLumaV:
	entry	a1, 352
	consts8
	extui	a10, a5, 8, 8
	extui	a5, a5, 0, 8
	addi	a12, a10, 5
	mov.n	a13, a1
	mov.n	a15, a2
.LV_fill:
	uload	q1, q2, a15
	ee.xorq		q1, q1, q7
	ee.vst.128.ip	q1, a13, 16
	add	a15, a15, a3
	addi	a12, a12, -1
	bnez	a12, .LV_fill

	mov.n	a13, a1
	addi	a15, a6, 1		/* integer row: 2 for G, 3 for M, relative to the row */
	slli	a15, a15, 4
	addi	a15, a15, -96
.LV_row:
	ee.mov.s8.qacc	q0
	vtaps	a13, q1
	movi	a11, 5
	ee.srcmb.s8.qacc	q1, a11, 0
	beqz	a6, .LV_store
	add	a12, a13, a15
	ee.vld.128.ip	q2, a12, 0
	avg	q1, q2, q3
.LV_store:
	ee.xorq		q1, q1, q7
	store_w	q1, a4, a5, .LV_next
.LV_next:
	addi	a13, a13, -80
	addi	a10, a10, -1
	bnez	a10, .LV_row
	retw.n
	.size	h264bsdPieLumaV, .-h264bsdPieLumaV

/*
 * void h264bsdPieLumaHV(const u8 *ref, u32 stride, u8 *mb, u32 wh, u32 mode,
 *                       const u8 *k)
 *
 * The diagonal quarter samples e, g, p and r (mode 0..3): the average of a
 * horizontal half sample on row y (modes 0, 1) or y + 1 (2, 3) and a vertical
 * one on column x (0, 2) or x + 1 (1, 3). ref points to (x - 2, y - 2).
 */
	.align	4
	.global	h264bsdPieLumaHV
	.type	h264bsdPieLumaHV, @function
h264bsdPieLumaHV:
	entry	a1, 352
	consts8
	extui	a10, a5, 8, 8
	extui	a5, a5, 0, 8
	/* vertical table from column x + (mode & 1) */
	extui	a12, a6, 0, 1
	add	a15, a2, a12
	addi	a15, a15, 2
	addi	a12, a10, 5
	mov.n	a13, a1
.LHV_fill:
	uload	q1, q2, a15
	ee.xorq		q1, q1, q7
	ee.vst.128.ip	q1, a13, 16
	add	a15, a15, a3
	addi	a12, a12, -1
	bnez	a12, .LHV_fill

	/* horizontal source row: y + (mode >> 1) */
	extui	a12, a6, 1, 1
	addi	a12, a12, 2
	mull	a12, a12, a3
	add	a2, a2, a12
	mov.n	a13, a1
.LHV_row:
	uload32	q4, q5, q1, q2, a2
	ee.mov.s8.qacc	q0
	htaps	q4, q5, q1
	movi	a11, 5
	ee.srcmb.s8.qacc	q2, a11, 0
	ee.mov.s8.qacc	q0
	vtaps	a13, q1
	ee.srcmb.s8.qacc	q1, a11, 0
	avg	q1, q2, q3
	ee.xorq		q1, q1, q7
	store_w	q1, a4, a5, .LHV_next
.LHV_next:
	add	a2, a2, a3
	addi	a13, a13, -80
	addi	a10, a10, -1
	bnez	a10, .LHV_row
	retw.n
	.size	h264bsdPieLumaHV, .-h264bsdPieLumaHV

/*
 * void h264bsdPieLumaMid(const u8 *ref, u32 stride, u8 *mb, u32 wh,
 *                        const u8 *avg, const u8 *k)
 *
 * The centre half sample j: a horizontal 6-tap pass kept at 16 bits, then a
 * vertical one over that. ref points to (x - 2, y - 2). When avg is not
 * NULL it is a w x h block (stride 16) of samples to average j with, for
 * f, q, i and k.
 */
	.align	4
	.global	h264bsdPieLumaMid
	.type	h264bsdPieLumaMid, @function
h264bsdPieLumaMid:
	entry	a1, 704
	addi	a15, a7, K_COEF16
	ee.vld.128.ip	q6, a15, 0	/* s16 coefficients, 512 in lane 6, 1 in lane 7 */
	ee.zero.q	q7
	movi	a14, -16
	extui	a10, a5, 8, 8
	extui	a5, a5, 0, 8
	addi	a12, a10, 5
	mov.n	a13, a1
	mov.n	a15, a2
	/* first pass: 16-bit row sums, 32 bytes per table row */
.LM_fill:
	and	a8, a15, a14
	extui	a9, a15, 0, 4
	wur.sar_byte a9
	ee.vld.128.ip	q1, a8, 16
	ee.vld.128.ip	q2, a8, 16
	ee.vld.128.ip	q3, a8, 0
	ee.src.q	q0, q1, q2		/* pixels 0..15 of the row */
	ee.src.q	q1, q2, q3		/* 16..31 */
	ee.zero.q	q2
	ee.zero.q	q3
	ee.vzip.8	q0, q2			/* q0: 0..7 as u16, q2: 8..15 */
	ee.vzip.8	q1, q3			/* q1: 16..23, q3: 24..31 */
	/* columns 0..7: taps from q0:q2 at 16-bit offsets 0..5 */
	ee.zero.qacc
	ee.vsmulas.s16.qacc	q0, q6, 0
	movi	a11, 2
	wur.sar_byte a11
	ee.src.q	q4, q0, q2
	ee.vsmulas.s16.qacc	q4, q6, 1
	movi	a11, 4
	wur.sar_byte a11
	ee.src.q	q4, q0, q2
	ee.vsmulas.s16.qacc	q4, q6, 2
	movi	a11, 6
	wur.sar_byte a11
	ee.src.q	q4, q0, q2
	ee.vsmulas.s16.qacc	q4, q6, 3
	movi	a11, 8
	wur.sar_byte a11
	ee.src.q	q4, q0, q2
	ee.vsmulas.s16.qacc	q4, q6, 4
	movi	a11, 10
	wur.sar_byte a11
	ee.src.q	q4, q0, q2
	ee.vsmulas.s16.qacc	q4, q6, 5
	movi	a11, 0
	ee.srcmb.s16.qacc	q4, a11, 0
	ee.vst.128.ip	q4, a13, 16
	/* columns 8..15: taps from q2:q1 */
	ee.zero.qacc
	ee.vsmulas.s16.qacc	q2, q6, 0
	movi	a11, 2
	wur.sar_byte a11
	ee.src.q	q4, q2, q1
	ee.vsmulas.s16.qacc	q4, q6, 1
	movi	a11, 4
	wur.sar_byte a11
	ee.src.q	q4, q2, q1
	ee.vsmulas.s16.qacc	q4, q6, 2
	movi	a11, 6
	wur.sar_byte a11
	ee.src.q	q4, q2, q1
	ee.vsmulas.s16.qacc	q4, q6, 3
	movi	a11, 8
	wur.sar_byte a11
	ee.src.q	q4, q2, q1
	ee.vsmulas.s16.qacc	q4, q6, 4
	movi	a11, 10
	wur.sar_byte a11
	ee.src.q	q4, q2, q1
	ee.vsmulas.s16.qacc	q4, q6, 5
	movi	a11, 0
	ee.srcmb.s16.qacc	q4, a11, 0
	ee.vst.128.ip	q4, a13, 16
	add	a15, a15, a3
	addi	a12, a12, -1
	bnez	a12, .LM_fill

	/* second pass: vertical 6-tap over the table, (sum + 512) >> 10, clipped */
	mov.n	a13, a1
	addi	a15, a7, K_C255
	ee.vldbc.16	q5, a15		/* 255 */
	addi	a15, a7, K_C512
	ee.vldbc.16	q0, a15		/* 512 */
	addi	a12, a7, K_COEF16 + 14
	ee.vldbc.16	q3, a12		/* 1 */
	ssai	1
.LM_row:
	ee.mov.s16.qacc	q0		/* rounding */
	ee.vld.128.ip	q1, a13, 32
	ee.vsmulas.s16.qacc	q1, q6, 0
	ee.vld.128.ip	q1, a13, 32
	ee.vsmulas.s16.qacc	q1, q6, 1
	ee.vld.128.ip	q1, a13, 32
	ee.vsmulas.s16.qacc	q1, q6, 2
	ee.vld.128.ip	q1, a13, 32
	ee.vsmulas.s16.qacc	q1, q6, 3
	ee.vld.128.ip	q1, a13, 32
	ee.vsmulas.s16.qacc	q1, q6, 4
	ee.vld.128.ip	q1, a13, -144
	ee.vsmulas.s16.qacc	q1, q6, 5
	movi	a11, 10
	ee.srcmb.s16.qacc	q1, a11, 0
	ee.vmax.s16	q1, q1, q7
	ee.vmin.s16	q1, q1, q5
	ee.mov.s16.qacc	q0
	ee.vld.128.ip	q2, a13, 32
	ee.vsmulas.s16.qacc	q2, q6, 0
	ee.vld.128.ip	q2, a13, 32
	ee.vsmulas.s16.qacc	q2, q6, 1
	ee.vld.128.ip	q2, a13, 32
	ee.vsmulas.s16.qacc	q2, q6, 2
	ee.vld.128.ip	q2, a13, 32
	ee.vsmulas.s16.qacc	q2, q6, 3
	ee.vld.128.ip	q2, a13, 32
	ee.vsmulas.s16.qacc	q2, q6, 4
	ee.vld.128.ip	q2, a13, -144
	ee.vsmulas.s16.qacc	q2, q6, 5
	ee.srcmb.s16.qacc	q2, a11, 0
	ee.vmax.s16	q2, q2, q7
	ee.vmin.s16	q2, q2, q5
	beqz	a6, .LM_pack
	/* (j + avg + 1) >> 1, at 16 bits: q3 is 1 and SAR is 1 */
	ee.vld.128.ip	q4, a6, 16
	ee.zero.q	q7
	ee.vzip.8	q4, q7
	ee.vadds.s16	q1, q1, q4
	ee.vadds.s16	q2, q2, q7
	ee.vadds.s16	q1, q1, q3
	ee.vadds.s16	q2, q2, q3
	ee.vmul.s16	q1, q1, q3
	ee.vmul.s16	q2, q2, q3
	ee.zero.q	q7
.LM_pack:
	ee.vunzip.8	q1, q2
	store_w	q1, a4, a5, .LM_next
.LM_next:
	addi	a10, a10, -1
	bnez	a10, .LM_row
	retw.n
	.size	h264bsdPieLumaMid, .-h264bsdPieLumaMid

/*
 * void h264bsdPieChroma(const u8 *ref, u32 stride, u8 *out, u32 wh,
 *                       const u8 *coef, const u8 *k)
 *
 * Bilinear chroma interpolation: out = (A * cA + B * cB + C * cC + D * cD +
 * 32) >> 6 with A the sample at ref, B its right neighbour, C and D the row
 * below. The four weights are the first bytes of coef; the row below is not
 * read when cC and cD are both 0. out has a stride of 8.
 */
/* \qa = the row at \ap and \qb = the same one pixel right (lane 15 undefined), as signed pixels */
.macro crow qa, qb, ap
	and	a8, \ap, a14
	extui	a9, \ap, 0, 4
	wur.sar_byte a9
	ee.vld.128.ip	\qa, a8, 16
	ee.vld.128.ip	q5, a8, 0
	ee.src.q	\qa, \qa, q5
	wur.sar_byte a13
	ee.src.q	\qb, \qa, \qa
	ee.xorq		\qa, \qa, q7
	ee.xorq		\qb, \qb, q7
.endm

/* The chroma result in QACC: shifted, flipped back and stored with a stride of 8 */
.macro cstore
	movi	a11, 6
	ee.srcmb.s8.qacc	q1, a11, 0
	ee.xorq		q1, q1, q7
	bnei	a5, 8, 1f
	ee.vst.l.64.ip	q1, a4, 8
	j	3f
1:	ee.movi.32.a	q1, a11, 0
	bnei	a5, 4, 2f
	s32i	a11, a4, 0
	j	4f
2:	s16i	a11, a4, 0
4:	addi	a4, a4, 8
3:
.endm

	.align	4
	.global	h264bsdPieChroma
	.type	h264bsdPieChroma, @function
h264bsdPieChroma:
	entry	a1, 32
	ee.vld.128.ip	q6, a6, 0
	addi	a15, a7, K_X80
	ee.vld.128.ip	q7, a15, 0
	addi	a15, a7, K_R32
	ee.vld.128.ip	q0, a15, 0	/* rounding 32 */
	movi	a14, -16
	movi	a13, 1
	extui	a10, a5, 8, 8
	extui	a5, a5, 0, 8
	l8ui	a8, a6, 2
	l8ui	a9, a6, 3
	or	a15, a8, a9		/* non-zero: the row below matters */
	mov.n	a12, a2
	beqz	a15, .LC_1row
	crow	q3, q4, a12
.LC_2rows:
	ee.orq		q1, q3, q3
	ee.orq		q2, q4, q4
	add	a12, a12, a3
	crow	q3, q4, a12
	ee.mov.s8.qacc	q0
	ee.vsmulas.s8.qacc	q1, q6, 0
	ee.vsmulas.s8.qacc	q2, q6, 1
	ee.vsmulas.s8.qacc	q3, q6, 2
	ee.vsmulas.s8.qacc	q4, q6, 3
	cstore
	addi	a10, a10, -1
	bnez	a10, .LC_2rows
	retw.n
.LC_1row:
	crow	q1, q2, a12
	ee.mov.s8.qacc	q0
	ee.vsmulas.s8.qacc	q1, q6, 0
	ee.vsmulas.s8.qacc	q2, q6, 1
	cstore
	add	a12, a12, a3
	addi	a10, a10, -1
	bnez	a10, .LC_1row
	retw.n
	.size	h264bsdPieChroma, .-h264bsdPieChroma

/*
 * void h264bsdPieCopy(const u8 *ref, u32 stride, u8 *out, u32 ostride, u32 wh)
 *
 * Copies a w x h block (w 2, 4, 8 or 16) from anywhere to a destination the
 * width requires the alignment of: 16 for 16, 8 for 8, 4 for 4 and 2 for 2.
 */
	.align	4
	.global	h264bsdPieCopy
	.type	h264bsdPieCopy, @function
h264bsdPieCopy:
	entry	a1, 32
	movi	a14, -16
	extui	a10, a6, 8, 8
	extui	a6, a6, 0, 8
.LCP_row:
	uload	q1, q2, a2
	bnei	a6, 16, 1f
	ee.vst.128.ip	q1, a4, 0
	j	.LCP_next
1:	bnei	a6, 8, 2f
	ee.vst.l.64.ip	q1, a4, 0
	j	.LCP_next
2:	ee.movi.32.a	q1, a11, 0
	bnei	a6, 4, 3f
	s32i	a11, a4, 0
	j	.LCP_next
3:	s16i	a11, a4, 0
.LCP_next:
	add	a2, a2, a3
	add	a4, a4, a5
	addi	a10, a10, -1
	bnez	a10, .LCP_row
	retw.n
	.size	h264bsdPieCopy, .-h264bsdPieCopy

/*
 * void h264bsdPieWriteMb(const u8 *data, u8 *luma, u8 *cb, u8 *cr, u32 stride)
 *
 * Writes a decoded macroblock (256 luma then 2 x 64 chroma bytes) into the
 * picture; luma needs 16-byte alignment, cb and cr 8. stride is the luma
 * row length in bytes, the chroma one is half.
 */
	.align	4
	.global	h264bsdPieWriteMb
	.type	h264bsdPieWriteMb, @function
h264bsdPieWriteMb:
	entry	a1, 32
	srli	a7, a6, 1
	movi	a10, 16
.LW_luma:
	ee.vld.128.ip	q1, a2, 16
	ee.vst.128.xp	q1, a3, a6
	addi	a10, a10, -1
	bnez	a10, .LW_luma
	movi	a10, 8
.LW_cb:
	ee.vld.l.64.ip	q1, a2, 8
	ee.vst.l.64.xp	q1, a4, a7
	addi	a10, a10, -1
	bnez	a10, .LW_cb
	movi	a10, 8
.LW_cr:
	ee.vld.l.64.ip	q1, a2, 8
	ee.vst.l.64.xp	q1, a5, a7
	addi	a10, a10, -1
	bnez	a10, .LW_cr
	retw.n
	.size	h264bsdPieWriteMb, .-h264bsdPieWriteMb
