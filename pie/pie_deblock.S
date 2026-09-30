/*
 * Copyright (c) 2026 Benjamin Cabé
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-P4 PIE kernels for h264bsd luma deblocking, bS < 4.
 *
 * void h264bsd_pie_hor_luma16(u8 *data, u32 stride, const i16 k[8])
 *
 * Filters the 16 columns of the edge above the row at data with bS < 4,
 * exactly as FilterHorLuma does. k holds the per-edge constants as s16:
 * alpha, beta, tc, -tc, 1 (the rest unused).
 *
 * The eight rows around the edge are widened to s16 in pie_db_scratch, and
 * each half of eight columns is filtered from there with the mask and the
 * clipping done lane by lane. Right shifts are multiplies by one with SAR
 * set and FLOOR rounding, which is an arithmetic shift. The four rows the
 * filter can change are narrowed back and written with scalar stores, so
 * the lines stay in the cache for the edges that read them next.
 */
	.option norvc

	.section .bss
	.balign 16
pie_db_scratch:
	.space 2 * 256

	.text

/* scratch slots, 16 bytes each, per half */
#define P3 0
#define P2 1
#define P1 2
#define P0 3
#define Q0 4
#define Q1 5
#define Q2 6
#define Q3 7
#define AP 8
#define AQ 9
#define MSK 10
#define OP1 12
#define OP0 13
#define OQ0 14
#define OQ1 15

/* constant indices in k */
#define KA 0
#define KB 1
#define KT 2
#define KNT 3
#define KONE 4

.macro LDS q, slot
	addi	t6, s0, (\slot * 16)
	esp.vld.128.ip	\q, t6, 0
.endm

.macro STS q, slot
	addi	t6, s0, (\slot * 16)
	esp.vst.128.ip	\q, t6, 0
.endm

.macro LDK q, idx
	addi	t6, a2, (\idx * 2)
	esp.vldbc.16.ip	\q, t6, 0
.endm

.macro SAR n
	li	t5, \n
	esp.movx.w.sar	t5
.endm

/* |a - b| < q6 into \d, clobbers nothing else */
.macro ABSLT d, a, b, lim
	esp.vsub.s16	\d, \a, \b
	esp.vabs.16	\d, \d
	esp.vcmp.lt.s16	\d, \d, \lim
.endm

.macro LDKT q
	LDK	\q, KT
.endm
.macro LDKNT q
	LDK	\q, KNT
.endm
.macro NOEN
.endm

/* One half of eight lanes, bS < 4. q7 = 1s. \ldt and \ldnt load tc and -tc
 * into their argument, \en ANDs any per-lane enable into q4. */
.macro HALF_LT4 ldt, ldnt, en
	LDS	q0, P0
	LDS	q1, Q0
	LDS	q2, P1
	LDS	q3, Q1
	/* mask = |p0-q0| < alpha && |p1-p0| < beta && |q1-q0| < beta */
	LDK	q5, KA
	ABSLT	q4, q0, q1, q5
	LDK	q6, KB
	ABSLT	q5, q2, q0, q6
	esp.andq	q4, q4, q5
	ABSLT	q5, q3, q1, q6
	esp.andq	q4, q4, q5
	\en
	STS	q4, MSK
	/* ap, aq */
	LDS	q5, P2
	ABSLT	q5, q5, q0, q6
	esp.andq	q5, q5, q4
	STS	q5, AP
	LDS	q5, Q2
	ABSLT	q5, q5, q1, q6
	esp.andq	q5, q5, q4
	STS	q5, AQ
	/* avg = (p0 + q0 + 1) >> 1 into q4 */
	SAR	1
	esp.vadd.s16	q4, q0, q1
	esp.vsadds.s16	q4, q4, t3
	esp.vmul.s16	q4, q4, q7
	/* p1 += clip3(-tc, tc, (p2 + avg - 2 p1) >> 1) where ap */
	LDS	q5, P2
	esp.vadd.s16	q5, q5, q4
	esp.vsub.s16	q5, q5, q2
	esp.vsub.s16	q5, q5, q2
	esp.vmul.s16	q5, q5, q7
	\ldt	q6
	esp.vmin.s16	q5, q5, q6
	\ldnt	q6
	esp.vmax.s16	q5, q5, q6
	LDS	q6, AP
	esp.andq	q5, q5, q6
	esp.vadd.s16	q5, q2, q5
	STS	q5, OP1
	/* q1 += clip3(-tc, tc, (q2 + avg - 2 q1) >> 1) where aq */
	LDS	q5, Q2
	esp.vadd.s16	q5, q5, q4
	esp.vsub.s16	q5, q5, q3
	esp.vsub.s16	q5, q5, q3
	esp.vmul.s16	q5, q5, q7
	\ldt	q6
	esp.vmin.s16	q5, q5, q6
	\ldnt	q6
	esp.vmax.s16	q5, q5, q6
	LDS	q6, AQ
	esp.andq	q5, q5, q6
	esp.vadd.s16	q5, q3, q5
	STS	q5, OQ1
	/* tcx = tc + ap + aq (the flags are -1) into q4 */
	\ldt	q4
	LDS	q6, AP
	esp.vsub.s16	q4, q4, q6
	LDS	q6, AQ
	esp.vsub.s16	q4, q4, q6
	/* delta = clip3(-tcx, tcx, (4 (q0 - p0) + (p1 - q1) + 4) >> 3) where mask */
	esp.vsub.s16	q5, q1, q0
	esp.vadd.s16	q5, q5, q5
	esp.vadd.s16	q5, q5, q5
	esp.vsub.s16	q6, q2, q3
	esp.vadd.s16	q5, q5, q6
	esp.vsadds.s16	q5, q5, t4
	SAR	3
	esp.vmul.s16	q5, q5, q7
	esp.vmin.s16	q5, q5, q4
	esp.zero.q	q6
	esp.vsub.s16	q6, q6, q4
	esp.vmax.s16	q5, q5, q6
	LDS	q6, MSK
	esp.andq	q5, q5, q6
	/* p0 + delta, q0 - delta, clipped to 0..255 */
	esp.vadd.s16	q6, q0, q5
	esp.vsat.s16	q6, q6, s1, a5
	STS	q6, OP0
	esp.vsub.s16	q6, q1, q5
	esp.vsat.s16	q6, q6, s1, a5
	STS	q6, OQ0
.endm

/* Narrow slot \slot of both halves and store its 16 bytes at \addr */
.macro OUTROW slot, addr
	addi	t6, a3, (\slot * 16)
	esp.vld.128.ip	q0, t6, 0
	addi	t6, a3, (256 + \slot * 16)
	esp.vld.128.ip	q1, t6, 0
	esp.vunzip.8	q0, q1
	esp.movi.32.a	q0, t6, 0
	sw	t6, 0(\addr)
	esp.movi.32.a	q0, t6, 1
	sw	t6, 4(\addr)
	esp.movi.32.a	q0, t6, 2
	sw	t6, 8(\addr)
	esp.movi.32.a	q0, t6, 3
	sw	t6, 12(\addr)
.endm

	.balign 4
	.global h264bsd_pie_hor_luma16
	.type h264bsd_pie_hor_luma16, @function
h264bsd_pie_hor_luma16:
	addi	sp, sp, -16
	sw	s0, 0(sp)
	sw	s1, 4(sp)
	li	t3, 1
	csrw	0x7f2, t3
	li	t3, 0x03
	esp.movx.w.cfg	t3
	la	a3, pie_db_scratch
	/* widen rows p3..q3 into both halves of the scratch */
	slli	t0, a1, 2
	sub	a4, a0, t0
	mv	t4, a3
	addi	t5, a3, 256
	li	t0, 8
1:	esp.vld.128.xp	q0, a4, a1
	esp.vext.u8	q1, q2, q0
	esp.vst.128.ip	q1, t4, 16
	esp.vst.128.ip	q2, t5, 16
	addi	t0, t0, -1
	bnez	t0, 1b
	/* constants: t3 = 1, t4 = 4, s1 = 0, a5 = 255, q7 = 1s */
	li	t3, 1
	li	t4, 4
	li	s1, 0
	li	a5, 255
	LDK	q7, KONE
	mv	s0, a3
	HALF_LT4	LDKT, LDKNT, NOEN
	addi	s0, a3, 256
	HALF_LT4	LDKT, LDKNT, NOEN
	/* rows p1, p0, q0, q1 */
	sub	t0, a0, a1
	sub	t1, t0, a1
	OUTROW	OP1, t1
	OUTROW	OP0, t0
	OUTROW	OQ0, a0
	add	t0, a0, a1
	OUTROW	OQ1, t0
	lw	s0, 0(sp)
	lw	s1, 4(sp)
	addi	sp, sp, 16
	ret
	.size h264bsd_pie_hor_luma16, . - h264bsd_pie_hor_luma16

/*
 * void h264bsd_pie_ver_luma16(u8 *data, u32 stride, const i16 *k)
 *
 * Filters the vertical edge left of column data over the 16 rows below it,
 * bS < 4, as FilterVerLumaEdge does for each group of four rows. k holds
 * alpha, beta at k[0], k[1], 1 at k[4], then from byte 16 six
 * vectors of eight s16: tc for rows 0-7, tc for rows 8-15, -tc likewise,
 * and the per-row enable (0 or -1, 0 for rows whose bS is 0) likewise.
 *
 * Rows 0-7 and 8-15 are each transposed as an 8x8 block of bytes by three
 * rounds of zips, which leaves two columns in every register; widening
 * those gives the scratch slots p3..q3 directly. The four columns the
 * filter can change are put back as 4 bytes per row, stored as two
 * halfwords.
 */
.macro LDV q, off
	addi	t6, a2, \off
	esp.vld.128.ip	\q, t6, 0
.endm
.macro LDVT q
	LDV	\q, 16
.endm
.macro LDVT1 q
	LDV	\q, 32
.endm
.macro LDVNT q
	LDV	\q, 48
.endm
.macro LDVNT1 q
	LDV	\q, 64
.endm
.macro EN0
	LDV	q5, 80
	esp.andq	q4, q4, q5
.endm
.macro EN1
	LDV	q5, 96
	esp.andq	q4, q4, q5
.endm

/* Transpose the 8 rows of 8 bytes at a4 (a4 advances 8 rows) into scratch
 * slots P3..Q3 of the half at \dst. Clobbers q0-q7, t6. */
.macro TRANS8 dst
	esp.vld.l.64.xp	q0, a4, a1
	esp.vld.l.64.xp	q1, a4, a1
	esp.vld.l.64.xp	q2, a4, a1
	esp.vld.l.64.xp	q3, a4, a1
	esp.vld.l.64.xp	q4, a4, a1
	esp.vld.l.64.xp	q5, a4, a1
	esp.vld.l.64.xp	q6, a4, a1
	esp.vld.l.64.xp	q7, a4, a1
	esp.vzip.8	q0, q1
	esp.vzip.8	q2, q3
	esp.vzip.8	q4, q5
	esp.vzip.8	q6, q7
	esp.vzip.16	q0, q2
	esp.vzip.16	q4, q6
	esp.vzip.32	q0, q4
	esp.vzip.32	q2, q6
	mv	t6, \dst
	esp.vext.u8	q1, q3, q0
	esp.vst.128.ip	q1, t6, 16
	esp.vst.128.ip	q3, t6, 16
	esp.vext.u8	q1, q3, q4
	esp.vst.128.ip	q1, t6, 16
	esp.vst.128.ip	q3, t6, 16
	esp.vext.u8	q1, q3, q2
	esp.vst.128.ip	q1, t6, 16
	esp.vst.128.ip	q3, t6, 16
	esp.vext.u8	q1, q3, q6
	esp.vst.128.ip	q1, t6, 16
	esp.vst.128.ip	q3, t6, 16
.endm

/* Put the changed columns p1, p0, q0, q1 of the half at s0 back into eight
 * rows starting at a4 - 2 (a4 advances 8 rows). Narrowing leaves the p1 and
 * p0 columns in the halves of q0, q0 and q1 in those of q2; two rounds of
 * byte zips turn that into one word per row. */
.macro UNTRANS8
	LDS	q0, OP1
	LDS	q1, OP0
	LDS	q2, OQ0
	LDS	q3, OQ1
	esp.vunzip.8	q0, q1
	esp.vunzip.8	q2, q3
	esp.vzip.8	q0, q2
	esp.vzip.8	q0, q2
	.irp r, 0, 1, 2, 3
	esp.movi.32.a	q0, t6, \r
	sh	t6, -2(a4)
	srli	t6, t6, 16
	sh	t6, 0(a4)
	add	a4, a4, a1
	.endr
	.irp r, 0, 1, 2, 3
	esp.movi.32.a	q2, t6, \r
	sh	t6, -2(a4)
	srli	t6, t6, 16
	sh	t6, 0(a4)
	add	a4, a4, a1
	.endr
.endm

	.balign 4
	.global h264bsd_pie_ver_luma16
	.type h264bsd_pie_ver_luma16, @function
h264bsd_pie_ver_luma16:
	addi	sp, sp, -16
	sw	s0, 0(sp)
	sw	s1, 4(sp)
	li	t3, 1
	csrw	0x7f2, t3
	li	t3, 0x03
	esp.movx.w.cfg	t3
	la	a3, pie_db_scratch
	addi	a4, a0, -4
	TRANS8	a3
	addi	t0, a3, 256
	TRANS8	t0
	li	t3, 1
	li	t4, 4
	li	s1, 0
	li	a5, 255
	LDK	q7, KONE
	mv	s0, a3
	HALF_LT4	LDVT, LDVNT, EN0
	addi	s0, a3, 256
	HALF_LT4	LDVT1, LDVNT1, EN1
	mv	a4, a0
	mv	s0, a3
	UNTRANS8
	addi	s0, a3, 256
	UNTRANS8
	lw	s0, 0(sp)
	lw	s1, 4(sp)
	addi	sp, sp, 16
	ret
	.size h264bsd_pie_ver_luma16, . - h264bsd_pie_ver_luma16
