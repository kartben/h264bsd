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

#ifndef H264SWDEC_TRANSFORM_H
#define H264SWDEC_TRANSFORM_H

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

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/

/* raster position of each coefficient in zig-zag scan order */
extern const u8 h264bsdZigZag4x4[16];
extern const u8 h264bsdZigZag8x8[64];
/* weightScale8x8 of Flat_8x8_16 */
extern const u8 h264bsdFlatList8x8[64];

u32 h264bsdProcessBlock(i32 *data, u32 qp, u32 skip, u32 coeffMap);
void h264bsdProcessLumaDc(i32 *data, u32 qp);
void h264bsdProcessChromaDc(i32 *data, u32 qp, u32 qpCr);

u32 h264bsdProcessBlock8x8(i32 (*data)[16], u32 qp, const u8 *weights);
u32 h264bsdProcessBlockScaled(i32 *data, u32 qp, u32 skip, const u8 *weights);
void h264bsdProcessLumaDcScaled(i32 *data, u32 qp, u32 weight);
void h264bsdProcessChromaDcScaled(i32 *data, u32 qp, u32 weight);

#endif /* #ifdef H264SWDEC_TRANSFORM_H */

