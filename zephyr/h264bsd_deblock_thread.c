/*
 * Copyright (c) 2026 Benjamin Cabé
 * SPDX-License-Identifier: Apache-2.0
 *
 * Deblocking on a thread of its own, pinned to another CPU than the decoder.
 * The decoder hands over macroblocks as they become safe to filter, a row at
 * a time, and the thread filters them while the decoder goes on with the
 * next rows. It works on one decoder instance at a time.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/sys/atomic.h>

#include "h264bsd_deblocking.h"

static K_THREAD_STACK_DEFINE(deblock_stack, CONFIG_H264BSD_DEBLOCK_THREAD_STACK_SIZE);
static struct k_thread deblock_thread;

/* Given for every hand-over and every sync request */
static K_SEM_DEFINE(deblock_work, 0, K_SEM_MAX_LIMIT);
/* Given once everything handed over is filtered, after a sync request */
static K_SEM_DEFINE(deblock_idle, 0, 1);

static image_t *deblock_image;
static mbStorage_t *deblock_mb;
/* Macroblocks handed over in the current picture */
static atomic_t deblock_end;
static atomic_t deblock_sync;
/* Macroblocks filtered, only touched by the thread */
static u32 deblock_done;

static void deblock_run(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		u32 end;

		k_sem_take(&deblock_work, K_FOREVER);

		end = (u32)atomic_get(&deblock_end);
		if (end > deblock_done) {
			h264bsdFilterMbs(deblock_image, deblock_mb, deblock_done, end);
			deblock_done = end;
		}

		/*
		 * The decoder hands nothing over while it waits, so the picture is
		 * complete: start the next one from its first macroblock.
		 */
		if (atomic_get(&deblock_sync) != 0 && deblock_done == (u32)atomic_get(&deblock_end)) {
			deblock_done = 0U;
			atomic_set(&deblock_end, 0);
			atomic_clear(&deblock_sync);
			k_sem_give(&deblock_idle);
		}
	}
}

void h264bsdDeblockPost(image_t *image, mbStorage_t *mb, u32 first, u32 end)
{
	ARG_UNUSED(first);

	deblock_image = image;
	deblock_mb = mb;
	atomic_set(&deblock_end, (atomic_val_t)end);
	k_sem_give(&deblock_work);
}

void h264bsdDeblockSync(void)
{
	atomic_set(&deblock_sync, 1);
	k_sem_give(&deblock_work);
	k_sem_take(&deblock_idle, K_FOREVER);
}

static int deblock_init(void)
{
	k_thread_create(&deblock_thread, deblock_stack, K_THREAD_STACK_SIZEOF(deblock_stack),
			deblock_run, NULL, NULL, NULL, CONFIG_H264BSD_DEBLOCK_THREAD_PRIORITY, 0,
			K_FOREVER);
	k_thread_name_set(&deblock_thread, "h264bsd_deblock");
	(void)k_thread_cpu_pin(&deblock_thread, CONFIG_H264BSD_DEBLOCK_THREAD_CPU);
	k_thread_start(&deblock_thread);

	return 0;
}

SYS_INIT(deblock_init, APPLICATION, 0);
