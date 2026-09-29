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

#ifndef H264SWDEC_SEQ_PARAM_SET_H
#define H264SWDEC_SEQ_PARAM_SET_H

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "basetype.h"
#include "h264bsd_stream.h"
#include "h264bsd_vui.h"

/*------------------------------------------------------------------------------
    2. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Data types
------------------------------------------------------------------------------*/

/* Scaling lists of the High profiles in raster order: the 4x4 lists for
 * Intra Y, Intra Cb, Intra Cr, Inter Y, Inter Cb and Inter Cr, then the 8x8
 * lists for Intra Y and Inter Y. A flat list holds 16 everywhere. */
typedef struct
{
    u8 list4x4[6][16];
    u8 list8x8[2][64];
} scalingLists_t;

/* how a list of a picture parameter set was sent */
enum {
    SCALING_LIST_ABSENT = 0,
    SCALING_LIST_EXPLICIT,
    SCALING_LIST_DEFAULT
};

/* structure to store sequence parameter set information decoded from the
 * stream */
typedef struct
{
    u32 profileIdc;
    u32 levelIdc;
    u32 seqParameterSetId;
    u32 maxFrameNum;
    u32 picOrderCntType;
    u32 maxPicOrderCntLsb;
    u32 deltaPicOrderAlwaysZeroFlag;
    i32 offsetForNonRefPic;
    i32 offsetForTopToBottomField;
    u32 numRefFramesInPicOrderCntCycle;
    i32 *offsetForRefFrame;
    u32 numRefFrames;
    u32 gapsInFrameNumValueAllowedFlag;
    u32 picWidthInMbs;
    u32 picHeightInMbs;
    u32 frameCroppingFlag;
    u32 frameCropLeftOffset;
    u32 frameCropRightOffset;
    u32 frameCropTopOffset;
    u32 frameCropBottomOffset;
    u32 vuiParametersPresentFlag;
    vuiParameters_t *vuiParameters;
    u32 maxDpbSize;
    u32 scalingMatrixPresentFlag;
    /* after fall-back rule A, only valid if scalingMatrixPresentFlag */
    scalingLists_t scalingLists;
} seqParamSet_t;

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/

u32 h264bsdDecodeSeqParamSet(strmData_t *pStrmData,
    seqParamSet_t *pSeqParamSet);

u32 h264bsdCompareSeqParamSets(seqParamSet_t *pSps1, seqParamSet_t *pSps2);

u32 h264bsdDecodeScalingLists(strmData_t *pStrmData, scalingLists_t *pLists,
    u8 *present, u32 numLists);
void h264bsdScalingListFallBack(scalingLists_t *pLists, const u8 *present,
    const scalingLists_t *pSeqLists);

#endif /* #ifdef H264SWDEC_SEQ_PARAM_SET_H */

