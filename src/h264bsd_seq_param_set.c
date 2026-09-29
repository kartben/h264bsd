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
     2. External compiler flags
     3. Module defines
     4. Local function prototypes
     5. Functions
          h264bsdDecodeSeqParamSet
          GetDpbSize
          h264bsdCompareSeqParamSets
          h264bsdDecodeScalingLists
          h264bsdScalingListFallBack

------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "h264bsd_seq_param_set.h"
#include "h264bsd_util.h"
#include "h264bsd_vlc.h"
#include "h264bsd_vui.h"
#include "h264bsd_cfg.h"
#include "h264bsd_transform.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/* enumeration to indicate invalid return value from the GetDpbSize function */
enum {INVALID_DPB_SIZE = 0x7FFFFFFF};

/* Default scaling lists (Tables 7-3 and 7-4) in zig-zag scan order, intra
 * list first */
static const u8 defaultList4x4[2][16] = {
    { 6,13,13,20,20,20,28,28,28,28,32,32,32,37,37,42},
    {10,14,14,20,20,20,24,24,24,24,27,27,27,30,30,34} };

static const u8 defaultList8x8[2][64] = {
    { 6,10,10,13,11,13,16,16,16,16,18,18,18,18,18,23,
     23,23,23,23,23,25,25,25,25,25,25,25,27,27,27,27,
     27,27,27,27,29,29,29,29,29,29,29,31,31,31,31,31,
     31,33,33,33,33,33,36,36,36,36,38,38,38,40,40,42},
    { 9,13,13,15,13,15,17,17,17,17,19,19,19,19,19,21,
     21,21,21,21,21,22,22,22,22,22,22,22,24,24,24,24,
     24,24,24,24,25,25,25,25,25,25,25,27,27,27,27,27,
     27,28,28,28,28,28,30,30,30,30,32,32,32,33,33,35} };

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

static u32 GetDpbSize(u32 picSizeInMbs, u32 levelIdc);
static u32 IsHighProfile(u32 profileIdc);

/*------------------------------------------------------------------------------

    Function name: h264bsdDecodeSeqParamSet

        Functional description:
            Decode sequence parameter set information from the stream.

            Function allocates memory for offsetForRefFrame array if
            picture order count type is 1 and numRefFramesInPicOrderCntCycle
            is greater than zero.

        Inputs:
            pStrmData       pointer to stream data structure

        Outputs:
            pSeqParamSet    decoded information is stored here

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      failure, invalid information or end of stream
            MEMORY_ALLOCATION_ERROR for memory allocation failure

------------------------------------------------------------------------------*/

u32 h264bsdDecodeSeqParamSet(strmData_t *pStrmData, seqParamSet_t *pSeqParamSet)
{

/* Variables */

    u32 tmp, i, value;

/* Code */

    ASSERT(pStrmData);
    ASSERT(pSeqParamSet);

    memset(pSeqParamSet, 0, sizeof(seqParamSet_t));

    /* profile_idc */
    tmp = h264bsdGetBits(pStrmData, 8);
    if (tmp == END_OF_STREAM)
        return(HANTRO_NOK);
    if (tmp != 66)
    {
        DEBUG(("NOT BASELINE PROFILE %d\n", tmp));
    }
    pSeqParamSet->profileIdc = tmp;

    /* constrained_set0_flag */
    tmp = h264bsdGetBits(pStrmData, 1);
    /* constrained_set1_flag */
    tmp = h264bsdGetBits(pStrmData, 1);
    /* constrained_set2_flag */
    tmp = h264bsdGetBits(pStrmData, 1);

    if (tmp == END_OF_STREAM)
        return(HANTRO_NOK);

    /* reserved_zero_5bits, values of these bits shall be ignored */
    tmp = h264bsdGetBits(pStrmData, 5);
    if (tmp == END_OF_STREAM)
        return(HANTRO_NOK);

    tmp = h264bsdGetBits(pStrmData, 8);
    if (tmp == END_OF_STREAM)
        return(HANTRO_NOK);
    pSeqParamSet->levelIdc = tmp;

    tmp = h264bsdDecodeExpGolombUnsigned(pStrmData,
        &pSeqParamSet->seqParameterSetId);
    if (tmp != HANTRO_OK)
        return(tmp);
    if (pSeqParamSet->seqParameterSetId >= MAX_NUM_SEQ_PARAM_SETS)
    {
        EPRINT("seq_param_set_id");
        return(HANTRO_NOK);
    }

    if (IsHighProfile(pSeqParamSet->profileIdc))
    {
        /* chroma_format_idc, only 4:2:0 is decoded, which is also the only
         * format of the profiles without this field */
        tmp = h264bsdDecodeExpGolombUnsigned(pStrmData, &value);
        if (tmp != HANTRO_OK)
            return(tmp);
        if (value != 1)
        {
            EPRINT("chroma_format_idc");
            return(HANTRO_NOK);
        }

        /* bit_depth_luma_minus8 and bit_depth_chroma_minus8, only 8 bits
         * are decoded */
        for (i = 0; i < 2; i++)
        {
            tmp = h264bsdDecodeExpGolombUnsigned(pStrmData, &value);
            if (tmp != HANTRO_OK)
                return(tmp);
            if (value != 0)
            {
                EPRINT("bit_depth_minus8");
                return(HANTRO_NOK);
            }
        }

        /* qpprime_y_zero_transform_bypass_flag, lossless coding is not
         * supported */
        tmp = h264bsdGetBits(pStrmData, 1);
        if (tmp)
        {
            EPRINT("qpprime_y_zero_transform_bypass_flag");
            return(HANTRO_NOK);
        }

        tmp = h264bsdGetBits(pStrmData, 1);
        if (tmp == END_OF_STREAM)
            return(HANTRO_NOK);
        pSeqParamSet->scalingMatrixPresentFlag = tmp;
        if (tmp)
        {
            u8 present[8];

            tmp = h264bsdDecodeScalingLists(pStrmData,
                &pSeqParamSet->scalingLists, present, 8);
            if (tmp != HANTRO_OK)
                return(tmp);
            /* fall-back rule A */
            h264bsdScalingListFallBack(&pSeqParamSet->scalingLists, present,
                NULL);
        }
    }

    /* log2_max_frame_num_minus4 */
    tmp = h264bsdDecodeExpGolombUnsigned(pStrmData, &value);
    if (tmp != HANTRO_OK)
        return(tmp);
    if (value > 12)
    {
        EPRINT("log2_max_frame_num_minus4");
        return(HANTRO_NOK);
    }
    /* maxFrameNum = 2^(log2_max_frame_num_minus4 + 4) */
    pSeqParamSet->maxFrameNum = 1 << (value+4);

    /* valid POC types are 0, 1 and 2 */
    tmp = h264bsdDecodeExpGolombUnsigned(pStrmData, &value);
    if (tmp != HANTRO_OK)
        return(tmp);
    if (value > 2)
    {
        EPRINT("pic_order_cnt_type");
        return(HANTRO_NOK);
    }
    pSeqParamSet->picOrderCntType = value;

    if (pSeqParamSet->picOrderCntType == 0)
    {
        /* log2_max_pic_order_cnt_lsb_minus4 */
        tmp = h264bsdDecodeExpGolombUnsigned(pStrmData, &value);
        if (tmp != HANTRO_OK)
            return(tmp);
        if (value > 12)
        {
            EPRINT("log2_max_pic_order_cnt_lsb_minus4");
            return(HANTRO_NOK);
        }
        /* maxPicOrderCntLsb = 2^(log2_max_pic_order_cnt_lsb_minus4 + 4) */
        pSeqParamSet->maxPicOrderCntLsb = 1 << (value+4);
    }
    else if (pSeqParamSet->picOrderCntType == 1)
    {
        tmp = h264bsdGetBits(pStrmData, 1);
        if (tmp == END_OF_STREAM)
            return(HANTRO_NOK);
        pSeqParamSet->deltaPicOrderAlwaysZeroFlag = (tmp == 1) ?
                                        HANTRO_TRUE : HANTRO_FALSE;

        tmp = h264bsdDecodeExpGolombSigned(pStrmData,
            &pSeqParamSet->offsetForNonRefPic);
        if (tmp != HANTRO_OK)
            return(tmp);

        tmp = h264bsdDecodeExpGolombSigned(pStrmData,
            &pSeqParamSet->offsetForTopToBottomField);
        if (tmp != HANTRO_OK)
            return(tmp);

        tmp = h264bsdDecodeExpGolombUnsigned(pStrmData,
            &pSeqParamSet->numRefFramesInPicOrderCntCycle);
        if (tmp != HANTRO_OK)
            return(tmp);
        if (pSeqParamSet->numRefFramesInPicOrderCntCycle > 255)
        {
            EPRINT("num_ref_frames_in_pic_order_cnt_cycle");
            return(HANTRO_NOK);
        }

        if (pSeqParamSet->numRefFramesInPicOrderCntCycle)
        {
            /* NOTE: This has to be freed somewhere! */
            ALLOCATE(pSeqParamSet->offsetForRefFrame,
                     pSeqParamSet->numRefFramesInPicOrderCntCycle, i32);
            if (pSeqParamSet->offsetForRefFrame == NULL)
                return(MEMORY_ALLOCATION_ERROR);

            for (i = 0; i < pSeqParamSet->numRefFramesInPicOrderCntCycle; i++)
            {
                tmp =  h264bsdDecodeExpGolombSigned(pStrmData,
                    pSeqParamSet->offsetForRefFrame + i);
                if (tmp != HANTRO_OK)
                    return(tmp);
            }
        }
        else
        {
            pSeqParamSet->offsetForRefFrame = NULL;
        }
    }

    tmp = h264bsdDecodeExpGolombUnsigned(pStrmData,
        &pSeqParamSet->numRefFrames);
    if (tmp != HANTRO_OK)
        return(tmp);
    if (pSeqParamSet->numRefFrames > MAX_NUM_REF_PICS)
    {
        EPRINT("num_ref_frames");
        return(HANTRO_NOK);
    }

    tmp = h264bsdGetBits(pStrmData, 1);
    if (tmp == END_OF_STREAM)
        return(HANTRO_NOK);
    pSeqParamSet->gapsInFrameNumValueAllowedFlag = (tmp == 1) ?
                                        HANTRO_TRUE : HANTRO_FALSE;

    tmp = h264bsdDecodeExpGolombUnsigned(pStrmData, &value);
    if (tmp != HANTRO_OK)
        return(tmp);
    pSeqParamSet->picWidthInMbs = value + 1;

    tmp = h264bsdDecodeExpGolombUnsigned(pStrmData, &value);
    if (tmp != HANTRO_OK)
        return(tmp);
    pSeqParamSet->picHeightInMbs = value + 1;

    /* frame_mbs_only_flag, shall be 1 for baseline profile */
    tmp = h264bsdGetBits(pStrmData, 1);
    if (tmp == END_OF_STREAM)
        return(HANTRO_NOK);
    if (!tmp)
    {
        EPRINT("frame_mbs_only_flag");
        return(HANTRO_NOK);
    }

    /* direct_8x8_inference_flag */
    tmp = h264bsdGetBits(pStrmData, 1);
    if (tmp == END_OF_STREAM)
        return(HANTRO_NOK);

    tmp = h264bsdGetBits(pStrmData, 1);
    if (tmp == END_OF_STREAM)
        return(HANTRO_NOK);
    pSeqParamSet->frameCroppingFlag = (tmp == 1) ? HANTRO_TRUE : HANTRO_FALSE;

    if (pSeqParamSet->frameCroppingFlag)
    {
        tmp = h264bsdDecodeExpGolombUnsigned(pStrmData,
            &pSeqParamSet->frameCropLeftOffset);
        if (tmp != HANTRO_OK)
            return(tmp);
        tmp = h264bsdDecodeExpGolombUnsigned(pStrmData,
            &pSeqParamSet->frameCropRightOffset);
        if (tmp != HANTRO_OK)
            return(tmp);
        tmp = h264bsdDecodeExpGolombUnsigned(pStrmData,
            &pSeqParamSet->frameCropTopOffset);
        if (tmp != HANTRO_OK)
            return(tmp);
        tmp = h264bsdDecodeExpGolombUnsigned(pStrmData,
            &pSeqParamSet->frameCropBottomOffset);
        if (tmp != HANTRO_OK)
            return(tmp);

        /* check that frame cropping params are valid, parameters shall
         * specify non-negative area within the original picture */
        if ( ( (i32)pSeqParamSet->frameCropLeftOffset >
               ( 8 * (i32)pSeqParamSet->picWidthInMbs -
                 ((i32)pSeqParamSet->frameCropRightOffset + 1) ) ) ||
             ( (i32)pSeqParamSet->frameCropTopOffset >
               ( 8 * (i32)pSeqParamSet->picHeightInMbs -
                 ((i32)pSeqParamSet->frameCropBottomOffset + 1) ) ) )
        {
            EPRINT("frame_cropping");
            return(HANTRO_NOK);
        }
    }

    /* check that image dimensions and levelIdc match */
    tmp = pSeqParamSet->picWidthInMbs * pSeqParamSet->picHeightInMbs;
    value = GetDpbSize(tmp, pSeqParamSet->levelIdc);
    if (value == INVALID_DPB_SIZE || pSeqParamSet->numRefFrames > value)
    {
        DEBUG(("WARNING! Invalid DPB size based on SPS Level!\n"));
        DEBUG(("WARNING! Using num_ref_frames =%d for DPB size!\n",
                        pSeqParamSet->numRefFrames));
        value = pSeqParamSet->numRefFrames;
    }
    pSeqParamSet->maxDpbSize = value;

    tmp = h264bsdGetBits(pStrmData, 1);
    if (tmp == END_OF_STREAM)
        return(HANTRO_NOK);
    pSeqParamSet->vuiParametersPresentFlag = (tmp == 1) ?
                                HANTRO_TRUE : HANTRO_FALSE;

    /* VUI */
    if (pSeqParamSet->vuiParametersPresentFlag)
    {
        ALLOCATE(pSeqParamSet->vuiParameters, 1, vuiParameters_t);
        if (pSeqParamSet->vuiParameters == NULL)
            return(MEMORY_ALLOCATION_ERROR);
        tmp = h264bsdDecodeVuiParameters(pStrmData,
            pSeqParamSet->vuiParameters);
        if (tmp != HANTRO_OK)
            return(tmp);
        /* check numReorderFrames and maxDecFrameBuffering */
        if (pSeqParamSet->vuiParameters->bitstreamRestrictionFlag)
        {
            if (pSeqParamSet->vuiParameters->numReorderFrames >
                    pSeqParamSet->vuiParameters->maxDecFrameBuffering ||
                pSeqParamSet->vuiParameters->maxDecFrameBuffering <
                    pSeqParamSet->numRefFrames ||
                pSeqParamSet->vuiParameters->maxDecFrameBuffering >
                    pSeqParamSet->maxDpbSize)
            {
                return(HANTRO_NOK);
            }

            /* standard says that "the sequence shall not require a DPB with
             * size of more than max(1, maxDecFrameBuffering) */
            pSeqParamSet->maxDpbSize =
                MAX(1, pSeqParamSet->vuiParameters->maxDecFrameBuffering);
        }
    }

    tmp = h264bsdRbspTrailingBits(pStrmData);

    /* ignore possible errors in trailing bits of parameters sets */
    return(HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: GetDpbSize

        Functional description:
            Get size of the DPB in frames. Size is determined based on the
            picture size and MaxDPB for the specified level. These determine
            how many pictures may fit into to the buffer. However, the size
            is also limited to a maximum of 16 frames and therefore function
            returns the minimum of the determined size and 16.

        Inputs:
            picSizeInMbs    number of macroblocks in the picture
            levelIdc        indicates the level

        Outputs:
            none

        Returns:
            size of the DPB in frames
            INVALID_DPB_SIZE when invalid levelIdc specified or picSizeInMbs
            is higher than supported by the level in question

------------------------------------------------------------------------------*/

u32 GetDpbSize(u32 picSizeInMbs, u32 levelIdc)
{

/* Variables */

    u32 tmp;
    u32 maxPicSizeInMbs;

/* Code */

    ASSERT(picSizeInMbs);

    /* use tmp as the size of the DPB in bytes, computes as 1024 * MaxDPB
     * (from table A-1 in Annex A) */
    switch (levelIdc)
    {
        case 10:
            tmp = 152064;
            maxPicSizeInMbs = 99;
            break;

        case 11:
            tmp = 345600;
            maxPicSizeInMbs = 396;
            break;

        case 12:
            tmp = 912384;
            maxPicSizeInMbs = 396;
            break;

        case 13:
            tmp = 912384;
            maxPicSizeInMbs = 396;
            break;

        case 20:
            tmp = 912384;
            maxPicSizeInMbs = 396;
            break;

        case 21:
            tmp = 1824768;
            maxPicSizeInMbs = 792;
            break;

        case 22:
            tmp = 3110400;
            maxPicSizeInMbs = 1620;
            break;

        case 30:
            tmp = 3110400;
            maxPicSizeInMbs = 1620;
            break;

        case 31:
            tmp = 6912000;
            maxPicSizeInMbs = 3600;
            break;

        case 32:
            tmp = 7864320;
            maxPicSizeInMbs = 5120;
            break;

        case 40:
            tmp = 12582912;
            maxPicSizeInMbs = 8192;
            break;

        case 41:
            tmp = 12582912;
            maxPicSizeInMbs = 8192;
            break;

        case 42:
            tmp = 34816*384;
            maxPicSizeInMbs = 8704;
            break;

        case 50:
            /* standard says 42301440 here, but corrigendum "corrects" this to
             * 42393600 */
            tmp = 42393600;
            maxPicSizeInMbs = 22080;
            break;

        case 51:
            tmp = 70778880;
            maxPicSizeInMbs = 36864;
            break;

        default:
            return(INVALID_DPB_SIZE);
    }

    /* this is not "correct" return value! However, it results in error in
     * decoding and this was easiest place to check picture size */
    if (picSizeInMbs > maxPicSizeInMbs)
        return(INVALID_DPB_SIZE);

    tmp /= (picSizeInMbs*384);

    return(MIN(tmp, 16));

}

/*------------------------------------------------------------------------------

    Function name: h264bsdCompareSeqParamSets

        Functional description:
            Compare two sequence parameter sets.

        Inputs:
            pSps1   pointer to a sequence parameter set
            pSps2   pointer to another sequence parameter set

        Outputs:
            0       sequence parameter sets are equal
            1       otherwise

------------------------------------------------------------------------------*/

u32 h264bsdCompareSeqParamSets(seqParamSet_t *pSps1, seqParamSet_t *pSps2)
{

/* Variables */

    u32 i;

/* Code */

    ASSERT(pSps1);
    ASSERT(pSps2);

    /* first compare parameters whose existence does not depend on other
     * parameters and only compare the rest of the params if these are equal */
    if (pSps1->profileIdc        == pSps2->profileIdc &&
        pSps1->levelIdc          == pSps2->levelIdc &&
        pSps1->maxFrameNum       == pSps2->maxFrameNum &&
        pSps1->picOrderCntType   == pSps2->picOrderCntType &&
        pSps1->numRefFrames      == pSps2->numRefFrames &&
        pSps1->gapsInFrameNumValueAllowedFlag ==
            pSps2->gapsInFrameNumValueAllowedFlag &&
        pSps1->picWidthInMbs     == pSps2->picWidthInMbs &&
        pSps1->picHeightInMbs    == pSps2->picHeightInMbs &&
        pSps1->frameCroppingFlag == pSps2->frameCroppingFlag &&
        pSps1->vuiParametersPresentFlag == pSps2->vuiParametersPresentFlag &&
        pSps1->scalingMatrixPresentFlag == pSps2->scalingMatrixPresentFlag)
    {
        if (pSps1->scalingMatrixPresentFlag &&
            memcmp(&pSps1->scalingLists, &pSps2->scalingLists,
                sizeof(scalingLists_t)))
            return 1;
        if (pSps1->picOrderCntType == 0)
        {
            if (pSps1->maxPicOrderCntLsb != pSps2->maxPicOrderCntLsb)
                return 1;
        }
        else if (pSps1->picOrderCntType == 1)
        {
            if (pSps1->deltaPicOrderAlwaysZeroFlag !=
                    pSps2->deltaPicOrderAlwaysZeroFlag ||
                pSps1->offsetForNonRefPic != pSps2->offsetForNonRefPic ||
                pSps1->offsetForTopToBottomField !=
                    pSps2->offsetForTopToBottomField ||
                pSps1->numRefFramesInPicOrderCntCycle !=
                    pSps2->numRefFramesInPicOrderCntCycle)
            {
                return 1;
            }
            else
            {
                for (i = 0; i < pSps1->numRefFramesInPicOrderCntCycle; i++)
                    if (pSps1->offsetForRefFrame[i] !=
                        pSps2->offsetForRefFrame[i])
                    {
                        return 1;
                    }
            }
        }
        if (pSps1->frameCroppingFlag)
        {
            if (pSps1->frameCropLeftOffset   != pSps2->frameCropLeftOffset ||
                pSps1->frameCropRightOffset  != pSps2->frameCropRightOffset ||
                pSps1->frameCropTopOffset    != pSps2->frameCropTopOffset ||
                pSps1->frameCropBottomOffset != pSps2->frameCropBottomOffset)
            {
                return 1;
            }
        }

        return 0;
    }

    return 1;
}

/*------------------------------------------------------------------------------

    Function: IsHighProfile

        Functional description:
            Tell whether a profile carries the chroma format, bit depth and
            scaling matrix fields in its sequence parameter set.

------------------------------------------------------------------------------*/

u32 IsHighProfile(u32 profileIdc)
{
    switch (profileIdc)
    {
        case 100: case 110: case 122: case 244: case 44: case 83: case 86:
        case 118: case 128: case 138: case 139: case 134: case 135:
            return(HANTRO_TRUE);
        default:
            return(HANTRO_FALSE);
    }
}

/*------------------------------------------------------------------------------

    Function: h264bsdDecodeScalingLists

        Functional description:
            Decode the scaling_list_present_flag and scaling_list() syntax of
            a sequence or picture parameter set. Explicit lists are stored in
            raster order, 'present' tells for each list whether it was sent,
            sent as a request for the default list, or left out.

        Inputs:
            pStrmData       pointer to stream data structure
            numLists        number of lists in the syntax, 6 or 8

        Outputs:
            pLists          explicit lists stored here
            present         SCALING_LIST_* for each list

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      invalid data or end of stream

------------------------------------------------------------------------------*/

u32 h264bsdDecodeScalingLists(strmData_t *pStrmData, scalingLists_t *pLists,
    u8 *present, u32 numLists)
{

/* Variables */

    u32 i, j, tmp, size;
    i32 delta;
    u32 lastScale, nextScale;
    u8 *list;
    const u8 *scan;

/* Code */

    ASSERT(numLists <= 8);

    for (i = 0; i < 8; i++)
        present[i] = SCALING_LIST_ABSENT;

    for (i = 0; i < numLists; i++)
    {
        tmp = h264bsdGetBits(pStrmData, 1);
        if (tmp == END_OF_STREAM)
            return(HANTRO_NOK);
        if (!tmp)
            continue;

        if (i < 6)
        {
            size = 16;
            list = pLists->list4x4[i];
            scan = h264bsdZigZag4x4;
        }
        else
        {
            size = 64;
            list = pLists->list8x8[i-6];
            scan = h264bsdZigZag8x8;
        }

        present[i] = SCALING_LIST_EXPLICIT;
        lastScale = nextScale = 8;
        for (j = 0; j < size; j++)
        {
            if (nextScale)
            {
                tmp = h264bsdDecodeExpGolombSigned(pStrmData, &delta);
                if (tmp != HANTRO_OK)
                    return(tmp);
                if (delta < -128 || delta > 127)
                {
                    EPRINT("delta_scale");
                    return(HANTRO_NOK);
                }
                nextScale = (lastScale + (u32)delta + 256) & 0xFF;
                /* useDefaultScalingMatrixFlag */
                if (!j && !nextScale)
                {
                    present[i] = SCALING_LIST_DEFAULT;
                    break;
                }
            }
            if (nextScale)
                lastScale = nextScale;
            list[scan[j]] = (u8)lastScale;
        }
    }

    return(HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: h264bsdScalingListFallBack

        Functional description:
            Complete a set of scaling lists by filling in the lists that were
            not sent explicitly: defaults where asked for, otherwise fall-back
            rule A (pSeqLists NULL) or B (the sequence-level lists) of Table
            7-2.

        Inputs:
            present         SCALING_LIST_* for each of the 8 lists
            pSeqLists       sequence-level lists for rule B, NULL for rule A

        Outputs:
            pLists          lists completed in place

------------------------------------------------------------------------------*/

void h264bsdScalingListFallBack(scalingLists_t *pLists, const u8 *present,
    const scalingLists_t *pSeqLists)
{

/* Variables */

    u32 i, j;

/* Code */

    for (i = 0; i < 6; i++)
    {
        if (present[i] == SCALING_LIST_EXPLICIT)
            continue;
        if (present[i] == SCALING_LIST_ABSENT && i != 0 && i != 3)
            memcpy(pLists->list4x4[i], pLists->list4x4[i-1], 16);
        else if (present[i] == SCALING_LIST_ABSENT && pSeqLists)
            memcpy(pLists->list4x4[i], pSeqLists->list4x4[i], 16);
        else
            for (j = 0; j < 16; j++)
                pLists->list4x4[i][h264bsdZigZag4x4[j]] =
                    defaultList4x4[i / 3][j];
    }
    for (i = 0; i < 2; i++)
    {
        if (present[6+i] == SCALING_LIST_EXPLICIT)
            continue;
        if (present[6+i] == SCALING_LIST_ABSENT && pSeqLists)
            memcpy(pLists->list8x8[i], pSeqLists->list8x8[i], 64);
        else
            for (j = 0; j < 64; j++)
                pLists->list8x8[i][h264bsdZigZag8x8[j]] = defaultList8x8[i][j];
    }

}
