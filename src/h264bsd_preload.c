/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * The ESP32-S3 data cache can be told to fetch a region on its own (a
 * "manual preload", a function of the ROM). One fetch runs at a time and
 * takes as long as the PSRAM needs, so the regions are queued and started one
 * after the other from the decoding loop, which is what makes them overlap
 * with the CPU's work. A queued region is at most 8 KB: the rows of luma or
 * of a chroma plane that one macroblock row of a 320-pixel-wide picture
 * touches, reference margins included, so half of the 32 KB cache at most is
 * in flight for the two pictures together.
 */

#include <stdint.h>

#include "h264bsd_preload.h"

#ifdef H264BSD_ESP32S3_PRELOAD

/* The ROM's cache functions, see esp32s3/rom/cache.h */
u32 Cache_Start_DCache_Preload(u32 addr, u32 size, u32 order);
u32 Cache_DCache_Preload_Done(void);

#define QUEUE_SIZE 8
#define MAX_REGION 8192

static struct
{
    u32 addr[QUEUE_SIZE];
    u32 size[QUEUE_SIZE];
    u32 head, tail;
    u32 started;
} queue;

static void Queue(const u8 *addr, u32 size)
{
    if (queue.tail - queue.head >= QUEUE_SIZE || size == 0 || size > MAX_REGION)
        return;
    queue.addr[queue.tail % QUEUE_SIZE] = (u32)(uintptr_t)addr;
    queue.size[queue.tail % QUEUE_SIZE] = size;
    queue.tail++;
}

/* The rows [first, first + count) of a plane of height rows, clamped */
static void QueueRows(const u8 *plane, u32 stride, u32 rows, i32 first,
                      u32 count)
{
    i32 last = first + (i32)count;

    if (first < 0)
        first = 0;
    if (last > (i32)rows)
        last = (i32)rows;
    if (last > first)
        Queue(plane + (u32)first * stride, (u32)(last - first) * stride);
}

void h264bsdPreloadRow(const image_t *image, const u8 *ref, const u8 *cur,
                       u32 row)
{
    u32 width = image->width * 16;
    u32 height = image->height * 16;
    u32 lumaSize = width * height;

    /* the reference is read with the margins of the interpolation filters
     * and of small motion vectors, the current picture is written whole */
    if (ref != NULL)
        QueueRows(ref, width, height, (i32)row * 16 - 3, 16 + 6);
    if (cur != NULL)
        QueueRows(cur, width, height, (i32)row * 16, 16);
    if (ref != NULL)
    {
        QueueRows(ref + lumaSize, width / 2, height / 2, (i32)row * 8 - 1,
                  8 + 2);
        QueueRows(ref + lumaSize + lumaSize / 4, width / 2, height / 2,
                  (i32)row * 8 - 1, 8 + 2);
    }
    if (cur != NULL)
    {
        QueueRows(cur + lumaSize, width / 2, height / 2, (i32)row * 8, 8);
        QueueRows(cur + lumaSize + lumaSize / 4, width / 2, height / 2,
                  (i32)row * 8, 8);
    }
    h264bsdPreloadPoll();
}

void h264bsdPreloadPoll(void)
{
    if (queue.head == queue.tail)
        return;
    if (queue.started && !Cache_DCache_Preload_Done())
        return;
    Cache_Start_DCache_Preload(queue.addr[queue.head % QUEUE_SIZE],
                               queue.size[queue.head % QUEUE_SIZE], 0);
    queue.started = 1;
    queue.head++;
}

void h264bsdPreloadFlush(void)
{
    queue.head = queue.tail = 0;
}

void h264bsdPreloadRegion(const void *addr, u32 size)
{
    Queue((const u8 *)addr, size);
}

#endif /* H264BSD_ESP32S3_PRELOAD */
