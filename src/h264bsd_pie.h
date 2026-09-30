/*
 * Copyright (c) 2026 Benjamin Cabé
 * SPDX-License-Identifier: Apache-2.0
 *
 * ESP32-P4 PIE kernels, assembled from pie/ when the build is given a
 * compiler for the extension, which then defines H264BSD_HAS_PIE.
 */

#ifndef H264BSD_PIE_H
#define H264BSD_PIE_H

#include "basetype.h"

/* Luma interpolation into the 16-byte-stride macroblock buffer */
void h264bsd_pie_hor_half(const u8 *ref, u32 stride, u8 *mb, u32 w, u32 h);
void h264bsd_pie_ver_half(const u8 *ref, u32 stride, u8 *mb, u32 w, u32 h);
void h264bsd_pie_mid_half(const u8 *ref, u32 stride, u8 *mb, u32 w, u32 h);
void h264bsd_pie_hor_qpel(const u8 *ref, u32 stride, u8 *mb, u32 w, u32 h,
                          u32 offset);
void h264bsd_pie_ver_qpel(const u8 *ref, u32 stride, u8 *mb, u32 w, u32 h,
                          u32 offset);
void h264bsd_pie_hv_qpel(const u8 *ref, u32 stride, u8 *mb, u32 w, u32 h,
                         u32 offset);
void h264bsd_pie_mid_ver_qpel(const u8 *ref, u32 stride, u8 *mb, u32 w, u32 h,
                              u32 offset);
void h264bsd_pie_mid_hor_qpel(const u8 *ref, u32 stride, u8 *mb, u32 w, u32 h,
                              u32 offset);

/* Chroma interpolation of both components, Cr at ref + plane and out + 64 */
void h264bsd_pie_chroma(const u8 *ref, u32 stride, u32 plane, u8 *out,
                        u32 w, u32 h, u32 xf, u32 yf);

/* Copy of rows x w bytes (w 8 or 16) between pictures of the same stride */
void h264bsd_pie_copy_block(u8 *dst, const u8 *src, u32 stride, u32 rows,
                            u32 w);

#endif /* H264BSD_PIE_H */
