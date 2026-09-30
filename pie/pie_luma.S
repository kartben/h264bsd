/*
 * Copyright (c) 2026 Benjamin Cabé
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-P4 PIE kernels for h264bsd luma interpolation.
 *
 * All kernels share one calling convention:
 *
 *   void k(const u8 *ref, u32 stride, u8 *mb, u32 w, u32 h[, u32 offset])
 *
 * ref is where the C interpolator points after its edge handling, mb is the
 * 16-byte-stride macroblock buffer, w is 4, 8 or 16 and h the partition
 * height. Sixteen pixels are computed per row whatever w is, and w of them
 * are stored; loads may run up to 16 bytes past the last pixel of a row.
 *
 * Pixels are processed as signed bytes: x ^ 0x80 = x - 128. The six taps
 * (1, -5, 20, 20, -5, 1) sum to 32, so a six-tap sum of flipped pixels is
 * the true sum minus 4096, and (sum + 16) >> 5 comes out exactly 128 low.
 * QACC starts at 16 in every lane and is read out with FLOOR rounding. The
 * s8 read-out wraps instead of saturating, so it is taken twice (>> 5 and
 * one bit further) and recombined into a saturated value, which flipped
 * back is exactly clip((true + 16) >> 5). A rounding average of two flipped
 * values, (a + b + 1) >> 1, is exact the same way with QACC starting at 1.
 *
 * Only x8-x15 and x24-x31 may be PIE operands; t0-t2, a6 and a7 are used
 * for plain integer work only.
 */
	.option norvc

	.section .rodata
	.balign 16
pie_k8:
	.byte 1, -5, 20, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
pie_x80:
	.fill 16, 1, 0x80
pie_r16:
	.fill 16, 1, 16
pie_ones:
	.fill 16, 1, 1

	.text

/* CFG: vxrm = FLOOR, hardware handles misaligned loads and stores */
#define PIE_CFG 0x03

/*
 * Register use inside a kernel:
 *   a0 ref row, a1 stride, a2 mb row, a4 rows left, a6 w, t1 offset
 *   q3 = 1s, q6 = taps, q7 = 0x80s, s8 -> 16s, s9 -> 1s
 *   t3..t6 = 1..4, a5 = 5 (byte shifts and read-out shifts)
 *   s0, s1, s10, a3 free for the row macros
 */
.macro PROLOGUE
	addi	sp, sp, -32
	sw	s0, 0(sp)
	sw	s1, 4(sp)
	sw	s8, 8(sp)
	sw	s9, 12(sp)
	sw	s10, 16(sp)
	li	t3, 1
	csrw	0x7f2, t3
	li	t3, PIE_CFG
	esp.movx.w.cfg	t3
	la	t3, pie_k8
	esp.vld.128.ip	q6, t3, 0
	la	t3, pie_x80
	esp.vld.128.ip	q7, t3, 0
	la	t3, pie_ones
	esp.vld.128.ip	q3, t3, 0
	la	s8, pie_r16
	la	s9, pie_ones
	mv	a6, a3
	li	t3, 1
	li	t4, 2
	li	t5, 3
	li	t6, 4
.endm

.macro EPILOGUE
	lw	s0, 0(sp)
	lw	s1, 4(sp)
	lw	s8, 8(sp)
	lw	s9, 12(sp)
	lw	s10, 16(sp)
	addi	sp, sp, 32
	ret
.endm

/* QACC holds a six-tap sum started at 16: clamp it to s8 into q4.
 * Clobbers q4, q5. */
.macro CLAMP8
	esp.srcmb.s8.qacc	q4, a5, 0
	esp.srcmb.s8.qacc	q5, t3, 0
	esp.andq	q4, q4, q3
	esp.vadd.s8	q4, q4, q5
	esp.vadd.s8	q4, q4, q5
.endm

/* Horizontal six-tap of the 16 pixels whose leftmost tap is at \p, into q4
 * (flipped). \p is left unchanged. Clobbers q0-q2, q4, q5, SAR_BYTE. */
.macro HOR16 p
	esp.vld.128.ip	q0, \p, 16
	esp.vld.128.ip	q1, \p, -16
	esp.xorq	q0, q0, q7
	esp.xorq	q1, q1, q7
	esp.ldqa.s8.128.ip	s8, 0
	esp.vsmulas.s8.qacc	q0, q6, 0
	esp.movx.w.sar.bytes	t3
	esp.src.q	q2, q0, q1
	esp.vsmulas.s8.qacc	q2, q6, 1
	esp.movx.w.sar.bytes	t4
	esp.src.q	q2, q0, q1
	esp.vsmulas.s8.qacc	q2, q6, 2
	esp.movx.w.sar.bytes	t5
	esp.src.q	q2, q0, q1
	esp.vsmulas.s8.qacc	q2, q6, 2
	esp.movx.w.sar.bytes	t6
	esp.src.q	q2, q0, q1
	esp.vsmulas.s8.qacc	q2, q6, 1
	esp.movx.w.sar.bytes	a5
	esp.src.q	q2, q0, q1
	esp.vsmulas.s8.qacc	q2, q6, 0
	CLAMP8
.endm

.macro VTAP sel
	esp.vld.128.xp	q0, a3, a1
	esp.xorq	q0, q0, q7
	esp.vsmulas.s8.qacc	q0, q6, \sel
.endm

/* Vertical six-tap of the 16 pixels whose top tap row is at \p, into q4
 * (flipped). Clobbers a3, q0, q4, q5. */
.macro VER16 p
	mv	a3, \p
	esp.ldqa.s8.128.ip	s8, 0
	VTAP	0
	VTAP	1
	VTAP	2
	VTAP	2
	VTAP	1
	VTAP	0
	CLAMP8
.endm

/* Rounding average of two flipped vectors into q4 (flipped) */
.macro AVG qa, qb
	esp.ldqa.s8.128.ip	s9, 0
	esp.vsmulas.s8.qacc	\qa, q3, 0
	esp.vsmulas.s8.qacc	\qb, q3, 0
	esp.srcmb.s8.qacc	q4, t3, 0
.endm

/* 16 plain pixels at \p into \q, flipped */
.macro LOADF q, p
	esp.vld.128.ip	\q, \p, 0
	esp.xorq	\q, \q, q7
.endm

/* Flip q4 back and store w (a6) bytes of it as the next mb row */
.macro STORE
	esp.xorq	q4, q4, q7
	li	t0, 8
	blt	a6, t0, 81f
	beq	a6, t0, 82f
	esp.vst.128.ip	q4, a2, 16
	j	83f
81:	esp.movi.32.a	q4, a3, 0
	sw	a3, 0(a2)
	addi	a2, a2, 16
	j	83f
82:	esp.vst.l.64.ip	q4, a2, 16
83:
.endm

/* Kernel frame: \setup runs once after the prologue, \row once per row */
.macro KERNEL name, setup, row
	.balign 4
	.global \name
	.type \name, @function
\name:
	mv	t1, a5
	PROLOGUE
	li	a5, 5
	\setup
	beqz	a4, 99f
90:	\row
	STORE
	add	a0, a0, a1
	addi	a4, a4, -1
	bnez	a4, 90b
99:	EPILOGUE
	.size \name, . - \name
.endm

.macro NOSETUP
.endm

/* b: horizontal half sample */
.macro ROW_HOR_HALF
	HOR16	a0
.endm
KERNEL h264bsd_pie_hor_half, NOSETUP, ROW_HOR_HALF

/* h: vertical half sample */
.macro ROW_VER_HALF
	VER16	a0
.endm
KERNEL h264bsd_pie_ver_half, NOSETUP, ROW_VER_HALF

/* a, c: b averaged with the full sample at column 2 + offset */
.macro SETUP_HOR_QPEL
	addi	s0, t1, 2
.endm
.macro ROW_HOR_QPEL
	HOR16	a0
	esp.orq	q2, q4, q4
	add	s1, a0, s0
	LOADF	q0, s1
	AVG	q2, q0
.endm
KERNEL h264bsd_pie_hor_qpel, SETUP_HOR_QPEL, ROW_HOR_QPEL

/* d, n: h averaged with the full sample at row 2 + offset */
.macro SETUP_VER_QPEL
	addi	s0, t1, 2
	mul	s0, s0, a1
.endm
.macro ROW_VER_QPEL
	VER16	a0
	esp.orq	q2, q4, q4
	add	s1, a0, s0
	LOADF	q0, s1
	AVG	q2, q0
.endm
KERNEL h264bsd_pie_ver_qpel, SETUP_VER_QPEL, ROW_VER_QPEL

/* e, g, p, r: b of row 2 + (offset >> 1) averaged with h of column
 * 2 + (offset & 1) */
.macro SETUP_HV_QPEL
	srli	s0, t1, 1
	addi	s0, s0, 2
	mul	s0, s0, a1
	andi	s1, t1, 1
	addi	s1, s1, 2
.endm
.macro ROW_HV_QPEL
	add	s10, a0, s0
	HOR16	s10
	esp.orq	q2, q4, q4
	add	s10, a0, s1
	VER16	s10
	AVG	q2, q4
.endm
KERNEL h264bsd_pie_hv_qpel, SETUP_HV_QPEL, ROW_HV_QPEL

/*
 * Two-dimensional half sample (j). Pass 1 keeps the unrounded horizontal
 * six-tap of h + 5 rows as s16 in pie_tbuf, flipped (T - 4096): the s8
 * accumulation is exact in QACC, and its low byte and next byte, zipped,
 * are the s16 value. Pass 2 runs the vertical six-tap over those in s16
 * lanes: sum - 131072 = J - 4096 * 32, so with QACC started at 512,
 * (sum + 512) >> 10 is (J + 512) >> 10 exactly 128 low. It is clamped to
 * s8, narrowed and flipped back.
 */
	.section .rodata
	.balign 16
pie_k16:
	.hword 1, -5, 20, 0, 0, 0, 0, 0
pie_r512:
	.hword 512, 512, 512, 512, 512, 512, 512, 512
pie_r16w:
	.hword 16, 16, 16, 16, 16, 16, 16, 16

	.section .bss
	.balign 16
pie_tbuf:
	.space 21 * 32
pie_hbuf:
	.space 16 * 16

	.text

/* Flipped unrounded horizontal six-tap of 16 pixels at \p, stored as 16
 * s16 at \dst (advanced by 32). Needs s11 = 0. Clobbers a3, q0-q2, q4, q5. */
.macro HORT p, dst
	esp.vld.128.ip	q0, \p, 16
	esp.vld.128.ip	q1, \p, -16
	esp.xorq	q0, q0, q7
	esp.xorq	q1, q1, q7
	esp.zero.qacc
	esp.vsmulas.s8.qacc	q0, q6, 0
	esp.movx.w.sar.bytes	t3
	esp.src.q	q2, q0, q1
	esp.vsmulas.s8.qacc	q2, q6, 1
	esp.movx.w.sar.bytes	t4
	esp.src.q	q2, q0, q1
	esp.vsmulas.s8.qacc	q2, q6, 2
	esp.movx.w.sar.bytes	t5
	esp.src.q	q2, q0, q1
	esp.vsmulas.s8.qacc	q2, q6, 2
	esp.movx.w.sar.bytes	t6
	esp.src.q	q2, q0, q1
	esp.vsmulas.s8.qacc	q2, q6, 1
	esp.movx.w.sar.bytes	a5
	esp.src.q	q2, q0, q1
	esp.vsmulas.s8.qacc	q2, q6, 0
	esp.srcmb.s8.qacc	q4, s11, 0
	li	a3, 8
	esp.srcmb.s8.qacc	q5, a3, 0
	esp.vzip.8	q4, q5
	esp.vst.128.ip	q4, \dst, 16
	esp.vst.128.ip	q5, \dst, 16
.endm

/* Vertical six-tap over eight s16 intermediates at \p (row stride s1 = 32)
 * into \q as s16 clamped to s8. Needs q5 = s16 taps, s10 -> 512s,
 * t3 = 10, t4 = -128, t5 = 127. Clobbers a3, q0. */
.macro VERT8 p, q
	mv	a3, \p
	esp.ldqa.s16.128.ip	s10, 0
	esp.vld.128.xp	q0, a3, s1
	esp.vsmulas.s16.qacc	q0, q5, 0
	esp.vld.128.xp	q0, a3, s1
	esp.vsmulas.s16.qacc	q0, q5, 1
	esp.vld.128.xp	q0, a3, s1
	esp.vsmulas.s16.qacc	q0, q5, 2
	esp.vld.128.xp	q0, a3, s1
	esp.vsmulas.s16.qacc	q0, q5, 2
	esp.vld.128.xp	q0, a3, s1
	esp.vsmulas.s16.qacc	q0, q5, 1
	esp.vld.128.xp	q0, a3, s1
	esp.vsmulas.s16.qacc	q0, q5, 0
	esp.srcmb.s16.qacc	\q, t3, 0
	esp.vsat.s16	\q, \q, t4, t5
.endm

/* j of the 16 pixels whose T rows start at \p into q4 (flipped s8).
 * Clobbers a3, s11-free, q0, q2, q4. */
.macro MID16 p
	VERT8	\p, q4
	addi	a7, \p, 16
	mv	s11, a7
	VERT8	s11, q2
	li	s11, 0
	esp.vunzip.8	q4, q2
.endm

.macro MID_PROLOGUE
	PROLOGUE
	addi	sp, sp, -16
	sw	s11, 0(sp)
	li	a5, 5
	li	s11, 0
.endm

.macro MID_EPILOGUE
	lw	s11, 0(sp)
	addi	sp, sp, 16
	EPILOGUE
.endm

/* Pass 1: T rows for h + 5 rows starting at a0 into pie_tbuf */
.macro MID_PASS1
	la	s0, pie_tbuf
	addi	a7, a4, 5
	mv	s1, a0
91:	HORT	s1, s0
	add	s1, s1, a1
	addi	a7, a7, -1
	bnez	a7, 91b
.endm

/* Registers for pass 2: T rows 32 bytes apart, s16 taps, 512s, clamp
 * bounds; a5 = 1 for the averages, t6 -> pie_hbuf */
.macro MID_PASS2_SETUP
	la	s0, pie_tbuf
	li	s1, 32
	la	t3, pie_k16
	esp.vld.128.ip	q5, t3, 0
	la	s10, pie_r512
	li	t3, 10
	li	t4, -128
	li	t5, 127
	li	a5, 1
	la	t6, pie_hbuf
.endm

	.balign 4
	.global h264bsd_pie_mid_half
	.type h264bsd_pie_mid_half, @function
h264bsd_pie_mid_half:
	MID_PROLOGUE
	beqz	a4, 99f
	MID_PASS1
	MID_PASS2_SETUP
90:	MID16	s0
	STORE
	addi	s0, s0, 32
	addi	a4, a4, -1
	bnez	a4, 90b
99:	MID_EPILOGUE
	.size h264bsd_pie_mid_half, . - h264bsd_pie_mid_half

/*
 * f, q (MidVerQuarter): j averaged with b of row 2 + offset; i, k
 * (MidHorQuarter): j averaged with h of column 2 + offset. The b or h rows
 * are computed first, with the one-dimensional kernels' registers, into
 * pie_hbuf, then pass 2 averages each j row with its row there.
 */
.macro MID_QPEL name, row1b
	.balign 4
	.global \name
	.type \name, @function
\name:
	mv	t1, a5
	MID_PROLOGUE
	beqz	a4, 99f
	MID_PASS1
	\row1b
	MID_PASS2_SETUP
90:	MID16	s0
	esp.vld.128.ip	q1, t6, 16
	esp.ldqa.s8.128.ip	s9, 0
	esp.vsmulas.s8.qacc	q4, q3, 0
	esp.vsmulas.s8.qacc	q1, q3, 0
	esp.srcmb.s8.qacc	q4, a5, 0
	STORE
	addi	s0, s0, 32
	addi	a4, a4, -1
	bnez	a4, 90b
99:	MID_EPILOGUE
	.size \name, . - \name
.endm

/* b rows (y + 2 + offset) into pie_hbuf. Uses s1, s10, a7. */
.macro ROWS_B
	addi	s1, t1, 2
	mul	s1, s1, a1
	add	s1, s1, a0
	la	s10, pie_hbuf
	mv	a7, a4
92:	HOR16	s1
	esp.vst.128.ip	q4, s10, 16
	add	s1, s1, a1
	addi	a7, a7, -1
	bnez	a7, 92b
.endm

/* h rows at column 2 + offset into pie_hbuf. Uses s1, s10, a7, a3. */
.macro ROWS_H
	addi	s1, t1, 2
	add	s1, s1, a0
	la	s10, pie_hbuf
	mv	a7, a4
92:	VER16	s1
	esp.vst.128.ip	q4, s10, 16
	add	s1, s1, a1
	addi	a7, a7, -1
	bnez	a7, 92b
.endm

MID_QPEL h264bsd_pie_mid_ver_qpel, ROWS_B
MID_QPEL h264bsd_pie_mid_hor_qpel, ROWS_H
