/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Asks the ESP32-S3 data cache to fetch the rows of the pictures a
 * macroblock row is about to read and write while the CPU is busy with the
 * previous one, so that the reads and writes hit instead of waiting for the
 * PSRAM a cache line at a time. Built with H264BSD_ESP32S3_PRELOAD; nothing
 * happens elsewhere.
 */

#ifndef H264BSD_PRELOAD_H
#define H264BSD_PRELOAD_H

#include "basetype.h"
#include "h264bsd_image.h"

#ifdef H264BSD_ESP32S3_PRELOAD

/* Queues the rows of pictures a macroblock row touches: those of the
 * reference (NULL for none) and the current picture at macroblock row row */
void h264bsdPreloadRow(const image_t *image, const u8 *ref, const u8 *cur,
                       u32 row);

/* Starts the next queued fetch once the cache is done with the previous */
void h264bsdPreloadPoll(void);

/* Forgets what is queued */
void h264bsdPreloadFlush(void);

/* Queues any other region, such as the macroblock storage of the row */
void h264bsdPreloadRegion(const void *addr, u32 size);

#else

#define h264bsdPreloadRow(image, ref, cur, row) ((void)0)
#define h264bsdPreloadPoll() ((void)0)
#define h264bsdPreloadFlush() ((void)0)
#define h264bsdPreloadRegion(addr, size) ((void)0)

#endif

#endif /* H264BSD_PRELOAD_H */
