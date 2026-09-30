/*
 * Copyright (c) 2026 Benjamin Cabé
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-P4 PIE block copy for h264bsd skipped macroblocks.
 *
 * void h264bsd_pie_copy_block(u8 *dst, const u8 *src, u32 stride, u32 rows,
 *                             u32 w)
 *
 * Copies rows x w bytes (w = 16 or 8, rows a multiple of 4) between two
 * pictures with the same row stride, four rows in flight.
 */
	.option norvc
	.text
	.balign 4
	.global h264bsd_pie_copy_block
	.type h264bsd_pie_copy_block, @function
h264bsd_pie_copy_block:
	li	t3, 1
	csrw	0x7f2, t3
	li	t3, 0x03
	esp.movx.w.cfg	t3
	srli	a3, a3, 2
	beqz	a3, 9f
	li	t0, 8
	beq	a4, t0, 2f
1:	esp.vld.128.xp	q0, a1, a2
	esp.vld.128.xp	q1, a1, a2
	esp.vld.128.xp	q2, a1, a2
	esp.vld.128.xp	q3, a1, a2
	esp.vst.128.xp	q0, a0, a2
	esp.vst.128.xp	q1, a0, a2
	esp.vst.128.xp	q2, a0, a2
	esp.vst.128.xp	q3, a0, a2
	addi	a3, a3, -1
	bnez	a3, 1b
	ret
2:	esp.vld.l.64.xp	q0, a1, a2
	esp.vld.l.64.xp	q1, a1, a2
	esp.vld.l.64.xp	q2, a1, a2
	esp.vld.l.64.xp	q3, a1, a2
	esp.vst.l.64.xp	q0, a0, a2
	esp.vst.l.64.xp	q1, a0, a2
	esp.vst.l.64.xp	q2, a0, a2
	esp.vst.l.64.xp	q3, a0, a2
	addi	a3, a3, -1
	bnez	a3, 2b
9:	ret
	.size h264bsd_pie_copy_block, . - h264bsd_pie_copy_block
