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

#ifndef H264SWDEC_IMAGE_H
#define H264SWDEC_IMAGE_H

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "basetype.h"

/*------------------------------------------------------------------------------
    2. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Data types
------------------------------------------------------------------------------*/

typedef struct
{
    u8 *data;
    u32 width;
    u32 height;
    /* current MB's components */
    u8 *luma;
    u8 *cb;
    u8 *cr;
    /* In-loop deblocking state of the picture being decoded: macroblock
     * rows [0, deblockedRows) have already been filtered. Intra prediction
     * of the row below a filtered row must use the samples as they were
     * before filtering, so the bottom luma/chroma line of the most recently
     * filtered row is kept in unfilteredLine (see h264bsd_deblocking.c). */
    u32 deblockedRows;
    u8 *unfilteredLine;
} image_t;

/* layout of image_t.unfilteredLine for a picture widthInMbs wide: a byte
 * of padding before each component (for the above-left sample) and four
 * bytes after (above-right samples of the right-most macroblock) */
#define UNFILTERED_LINE_LUMA(width)   1
#define UNFILTERED_LINE_CB(width)     (1 + 16 * (width) + 4 + 1)
#define UNFILTERED_LINE_CR(width)     (1 + 16 * (width) + 4 + 1 + 8 * (width) + 4 + 1)
#define UNFILTERED_LINE_SIZE(width)   (3 * 5 + 32 * (width))

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/

void h264bsdWriteMacroblock(image_t *image, u8 *data);

void h264bsdCopyMacroblock(image_t *image, const u8 *refData,
    u32 x, u32 y, i32 dx, i32 dy);

#ifndef H264DEC_OMXDL
void h264bsdWriteOutputBlocks(image_t *image, u32 mbNum, u8 *data,
    i32 residual[][16]);
#endif

#endif /* #ifdef H264SWDEC_IMAGE_H */

