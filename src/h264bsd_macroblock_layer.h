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

#ifndef H264SWDEC_MACROBLOCK_LAYER_H
#define H264SWDEC_MACROBLOCK_LAYER_H

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "basetype.h"
#include "h264bsd_stream.h"
#include "h264bsd_image.h"
#include "h264bsd_dpb.h"

/*------------------------------------------------------------------------------
    2. Module defines
------------------------------------------------------------------------------*/

/* Macro to determine if a mb is an intra mb */
#define IS_INTRA_MB(a) ((a).mbType > 5)

/* Macro to determine if a mb is an I_PCM mb */
#define IS_I_PCM_MB(a) ((a).mbType == 31)

typedef enum {
    P_Skip          = 0,
    P_L0_16x16      = 1,
    P_L0_L0_16x8    = 2,
    P_L0_L0_8x16    = 3,
    P_8x8           = 4,
    P_8x8ref0       = 5,
    I_4x4           = 6,
    I_16x16_0_0_0   = 7,
    I_16x16_1_0_0   = 8,
    I_16x16_2_0_0   = 9,
    I_16x16_3_0_0   = 10,
    I_16x16_0_1_0   = 11,
    I_16x16_1_1_0   = 12,
    I_16x16_2_1_0   = 13,
    I_16x16_3_1_0   = 14,
    I_16x16_0_2_0   = 15,
    I_16x16_1_2_0   = 16,
    I_16x16_2_2_0   = 17,
    I_16x16_3_2_0   = 18,
    I_16x16_0_0_1   = 19,
    I_16x16_1_0_1   = 20,
    I_16x16_2_0_1   = 21,
    I_16x16_3_0_1   = 22,
    I_16x16_0_1_1   = 23,
    I_16x16_1_1_1   = 24,
    I_16x16_2_1_1   = 25,
    I_16x16_3_1_1   = 26,
    I_16x16_0_2_1   = 27,
    I_16x16_1_2_1   = 28,
    I_16x16_2_2_1   = 29,
    I_16x16_3_2_1   = 30,
    I_PCM           = 31
} mbType_e;

typedef enum {
    P_L0_8x8 = 0,
    P_L0_8x4 = 1,
    P_L0_4x8 = 2,
    P_L0_4x4 = 3
} subMbType_e;

typedef enum {
    MB_P_16x16 = 0,
    MB_P_16x8,
    MB_P_8x16,
    MB_P_8x8
} mbPartMode_e;

typedef enum {
    MB_SP_8x8 = 0,
    MB_SP_8x4,
    MB_SP_4x8,
    MB_SP_4x4
} subMbPartMode_e;

typedef enum {
    PRED_MODE_INTRA4x4 = 0,
    PRED_MODE_INTRA16x16  ,
    PRED_MODE_INTER
} mbPartPredMode_e;

/*------------------------------------------------------------------------------
    3. Data types
------------------------------------------------------------------------------*/

typedef struct
{
    /* MvPrediction16x16 assumes that MVs are 16bits */
    i16 hor;
    i16 ver;
} mv_t;

/* Field widths are chosen to keep the structures (and the per-macroblock
 * memset/memcpy of them) small: flags and modes fit in a byte. */
typedef struct
{
    u8 prevIntra4x4PredModeFlag[16];
    u8 remIntra4x4PredMode[16];
    u8 refIdxL0[4];
    u32 intraChromaPredMode;
    mv_t mvdL0[4];
} mbPred_t;

typedef struct
{
    u8 subMbType[4];    /* subMbType_e */
    u8 refIdxL0[4];
    mv_t mvdL0[4][4];
} subMbPred_t;

typedef struct
{
#ifdef H264DEC_OMXDL
    u8 posCoefBuf[27*16*3];
#endif
    u8 totalCoeff[27];
    u32 coeffMap[24];
    /* level must stay the last member: it is not cleared per macroblock,
     * see h264bsdDecodeMacroblockLayer() */
    i32 level[26][16];
} residual_t;

typedef struct
{
    mbType_e mbType;
    u32 codedBlockPattern;
    i32 mbQpDelta;
    mbPred_t mbPred;
    subMbPred_t subMbPred;
    residual_t residual;
} macroblockLayer_t;

/* Per-macroblock state kept for the whole picture (one per macroblock,
 * i.e. 920 of them for 640x360). Kept compact: 156 bytes on a 32-bit
 * target, so that the neighbour look-ups of CAVLC, motion vector
 * prediction and deblocking touch as little memory as possible. */
typedef struct mbStorage
{
    u8 mbType;                      /* mbType_e */
    u8 disableDeblockingFilterIdc;  /* 0..2 */
    i8 filterOffsetA;               /* -12..12 */
    i8 filterOffsetB;               /* -12..12 */
    u8 qpY;                         /* 0..51 */
    i8 chromaQpIndexOffset;         /* -12..12 */
    u16 decoded;                    /* number of times decoded */
    u32 sliceId;
    u8 totalCoeff[27];
    u8 intra4x4PredMode[16];
    u8 refPic[4];                   /* reference index per 8x8 partition */
    u8* refAddr[4];
    mv_t mv[16];
    struct mbStorage *mbA;
    struct mbStorage *mbB;
    struct mbStorage *mbC;
    struct mbStorage *mbD;
} mbStorage_t;

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/

u32 h264bsdDecodeMacroblockLayer(strmData_t *pStrmData,
    macroblockLayer_t *pMbLayer, mbStorage_t *pMb, u32 sliceType,
    u32 numRefIdxActive);

u32 h264bsdNumMbPart(mbType_e mbType);
u32 h264bsdNumSubMbPart(subMbType_e subMbType);

subMbPartMode_e h264bsdSubMbPartMode(subMbType_e subMbType);

u32 h264bsdDecodeMacroblock(mbStorage_t *pMb, macroblockLayer_t *pMbLayer,
    image_t *currImage, dpbStorage_t *dpb, i32 *qpY, u32 mbNum,
    u32 constrainedIntraPredFlag, u8* data);

u32 h264bsdPredModeIntra16x16(mbType_e mbType);

mbPartPredMode_e h264bsdMbPartPredMode(mbType_e mbType);
#ifdef H264DEC_NEON
u32 h264bsdClearMbLayer(macroblockLayer_t *pMbLayer, u32 size);
#endif

#endif /* #ifdef H264SWDEC_MACROBLOCK_LAYER_H */


