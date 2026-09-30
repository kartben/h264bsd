/*
 * Copyright (C) 2009 The Android Open Source Project
 * Modified for use by h264bsd standalone library
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*------------------------------------------------------------------------------

    Table of contents

    1. Include headers
    2. Module defines
    3. Data types
    4. Function prototypes

------------------------------------------------------------------------------*/

#ifndef H264SWDEC_DEBLOCKING_H
#define H264SWDEC_DEBLOCKING_H

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "basetype.h"
#include "h264bsd_image.h"
#include "h264bsd_macroblock_layer.h"

/*------------------------------------------------------------------------------
    2. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Data types
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/

void h264bsdFilterPicture(
  image_t *image,
  mbStorage_t *mb);

#ifndef H264DEC_OMXDL
void h264bsdFilterMbs(
  image_t *image,
  mbStorage_t *mb,
  u32 first,
  u32 end);
#endif

/* Deblocking that follows the decoding: h264bsdDeblockPost() hands over
 * macroblocks first to end - 1 for filtering, in raster order after those
 * handed over before, and h264bsdDeblockSync() returns once everything handed
 * over is filtered. Built with H264BSD_DEBLOCK_THREAD a thread of the
 * platform glue does the filtering, otherwise it happens right away. */
#ifdef H264BSD_DEBLOCK_THREAD
void h264bsdDeblockPost(image_t *image, mbStorage_t *mb, u32 first, u32 end);
void h264bsdDeblockSync(void);
#else
#define h264bsdDeblockPost(image, mb, first, end) \
    h264bsdFilterMbs(image, mb, first, end)
#define h264bsdDeblockSync() do { } while (0)
#endif

#endif /* #ifdef H264SWDEC_DEBLOCKING_H */

