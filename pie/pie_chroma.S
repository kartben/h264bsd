/*
 * Copyright (c) 2026 Benjamin Cabé
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-P4 PIE kernel for h264bsd chroma interpolation.
 *
 * void h264bsd_pie_chroma(const u8 *ref, u32 stride, u32 plane, u8 *out,
 *                         u32 w, u32 h, u32 xf, u32 yf)
 *
 * Both components: Cb at ref, Cr at ref + plane, written to out and
 * out + 64 with a row stride of 8. Every case is the bilinear
 * (wA A + wB B + wC C + wD D + 32) >> 6 with wA = (8 - xf)(8 - yf),
 * wB = xf (8 - yf), wC = (8 - xf) yf, wD = xf yf; weights that are zero
 * still read their row, so h + 1 rows of w + 1 pixels are read (loads are
 * 16 bytes wide). Pixels are flipped to s8: the weights sum to 64, so the
 * sum is exactly 128 * 64 low and the read-out exactly 128 low, and a
 * bilinear average cannot leave 0..255, so nothing needs clamping.
 */
	.option norvc

	.section .rodata
	.balign 16
pie_c_x80:
	.fill 16, 1, 0x80
pie_c_r32:
	.fill 16, 1, 32

	.text

	.balign 4
	.global h264bsd_pie_chroma
	.type h264bsd_pie_chroma, @function
h264bsd_pie_chroma:
	addi	sp, sp, -16
	sw	s0, 0(sp)
	sw	s1, 4(sp)
	sw	s8, 8(sp)
	li	t3, 1
	csrw	0x7f2, t3
	li	t3, 0x03
	esp.movx.w.cfg	t3
	li	t3, 1
	esp.movx.w.sar.bytes	t3
	la	t3, pie_c_x80
	esp.vld.128.ip	q7, t3, 0
	la	s8, pie_c_r32
	/* weights into q6 bytes 0..3 */
	li	t0, 8
	sub	t1, t0, a6
	sub	t2, t0, a7
	mul	t3, t1, t2
	esp.movi.8.q	q6, t3, 0
	mul	t3, a6, t2
	esp.movi.8.q	q6, t3, 1
	mul	t3, t1, a7
	esp.movi.8.q	q6, t3, 2
	mul	t3, a6, a7
	esp.movi.8.q	q6, t3, 3
	li	t4, 6
	beqz	a5, 9f
	li	t1, 2
1:	mv	s0, a0
	mv	s1, a3
	esp.vld.128.xp	q0, s0, a1
	esp.xorq	q0, q0, q7
	esp.src.q	q1, q0, q0
	mv	t0, a5
2:	esp.vld.128.xp	q2, s0, a1
	esp.xorq	q2, q2, q7
	esp.src.q	q3, q2, q2
	esp.ldqa.s8.128.ip	s8, 0
	esp.vsmulas.s8.qacc	q0, q6, 0
	esp.vsmulas.s8.qacc	q1, q6, 1
	esp.vsmulas.s8.qacc	q2, q6, 2
	esp.vsmulas.s8.qacc	q3, q6, 3
	esp.srcmb.s8.qacc	q4, t4, 0
	esp.xorq	q4, q4, q7
	li	t2, 4
	blt	a4, t2, 3f
	beq	a4, t2, 4f
	esp.vst.l.64.ip	q4, s1, 8
	j	5f
3:	esp.movi.16.a	q4, t5, 0
	sh	t5, 0(s1)
	addi	s1, s1, 8
	j	5f
4:	esp.movi.32.a	q4, t5, 0
	sw	t5, 0(s1)
	addi	s1, s1, 8
5:	esp.orq	q0, q2, q2
	esp.orq	q1, q3, q3
	addi	t0, t0, -1
	bnez	t0, 2b
	add	a0, a0, a2
	addi	a3, a3, 64
	addi	t1, t1, -1
	bnez	t1, 1b
9:	lw	s0, 0(sp)
	lw	s1, 4(sp)
	lw	s8, 8(sp)
	addi	sp, sp, 16
	ret
	.size h264bsd_pie_chroma, . - h264bsd_pie_chroma
