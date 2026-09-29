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

#ifndef H264SWDEC_PIC_PARAM_SET_H
#define H264SWDEC_PIC_PARAM_SET_H

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "basetype.h"
#include "h264bsd_stream.h"
#include "h264bsd_seq_param_set.h"

/*------------------------------------------------------------------------------
    2. Module defines
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    3. Data types
------------------------------------------------------------------------------*/

/* data structure to store PPS information decoded from the stream */
typedef struct
{
    u32 picParameterSetId;
    u32 seqParameterSetId;
    u32 picOrderPresentFlag;
    u32 numSliceGroups;
    u32 sliceGroupMapType;
    u32 *runLength;
    u32 *topLeft;
    u32 *bottomRight;
    u32 sliceGroupChangeDirectionFlag;
    u32 sliceGroupChangeRate;
    u32 picSizeInMapUnits;
    u32 *sliceGroupId;
    u32 numRefIdxL0Active;
    u32 picInitQp;
    i32 chromaQpIndexOffset;
    u32 deblockingFilterControlPresentFlag;
    u32 constrainedIntraPredFlag;
    u32 redundantPicCntPresentFlag;
    /* second_chroma_qp_index_offset, used for Cr */
    i32 chromaQpIndexOffset2;
    u32 transform8x8Flag;
    u32 scalingMatrixPresentFlag;
    /* SCALING_LIST_* of each list and the explicit ones, the fall-back
     * rule depends on the sequence parameter set */
    u8 scalingListPresent[8];
    scalingLists_t scalingLists;
} picParamSet_t;

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/

u32 h264bsdDecodePicParamSet(strmData_t *pStrmData,
    picParamSet_t *pPicParamSet);

u32 h264bsdGetScalingLists(const seqParamSet_t *pSps,
    const picParamSet_t *pPps, scalingLists_t *pLists);

#endif /* #ifdef H264SWDEC_PIC_PARAM_SET_H */

