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
          h264bsdProcessBlock
          h264bsdProcessLumaDc
          h264bsdProcessChromaDc
          h264bsdProcessBlock8x8
          h264bsdProcessBlockScaled
          h264bsdProcessLumaDcScaled
          h264bsdProcessChromaDcScaled

------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include "basetype.h"
#include "h264bsd_transform.h"
#include "h264bsd_util.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/* Switch off the following Lint messages for this file:
 * Info 701: Shift left of signed quantity (int)
 * Info 702: Shift right of signed quantity (int)
 */
/*lint -e701 -e702 */

/* LevelScale function */
static const i32 levelScale[6][3] H264BSD_HOT =
    {
    {10,13,16}, {11,14,18}, {13,16,20}, {14,18,23}, {16,20,25}, {18,23,29}};

/* qp % 6 as a function of qp */
static const u8 qpMod6[52] H264BSD_HOT =
    {0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,
    0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3,4,5,0,1,2,3};

/* qp / 6 as a function of qp */
static const u8 qpDiv6[52] H264BSD_HOT =
    {0,0,0,0,0,0,1,1,1,1,1,1,2,2,2,2,2,2,3,3,3,3,3,3,
    4,4,4,4,4,4,5,5,5,5,5,5,6,6,6,6,6,6,7,7,7,7,7,7,8,8,8,8};

/* raster position of each coefficient in zig-zag scan order */
const u8 h264bsdZigZag4x4[16] H264BSD_HOT =
    {0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11,
    14, 15};

const u8 h264bsdZigZag8x8[64] H264BSD_HOT =
    {
     0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

const u8 h264bsdFlatList8x8[64] H264BSD_HOT =
    {
    16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,
    16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,
    16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16,
    16,16,16,16,16,16,16,16, 16,16,16,16,16,16,16,16};

/* column of levelScale used for each raster position of a 4x4 block */
static const u8 posClass4x4[16] = {0,1,0,1, 1,2,1,2, 0,1,0,1, 1,2,1,2};

/* normAdjust8x8 values of Table 8-16 (v) and the one of the six used for each
 * raster position of an 8x8 block */
static const u8 normAdjust8x8[6][6] H264BSD_HOT =
    {
    {20,18,32,19,25,24}, {22,19,35,21,28,26}, {26,23,42,24,33,31},
    {28,25,45,26,35,33}, {32,28,51,30,40,38}, {36,32,58,34,46,43}};

static const u8 posClass8x8[64] H264BSD_HOT =
    {
    0,3,4,3,0,3,4,3, 3,1,5,1,3,1,5,1, 4,5,2,5,4,5,2,5, 3,1,5,1,3,1,5,1,
    0,3,4,3,0,3,4,3, 3,1,5,1,3,1,5,1, 4,5,2,5,4,5,2,5, 3,1,5,1,3,1,5,1};

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/

static u32 InverseTransform4x4(i32 *data, const i32 *coeffs);

/*------------------------------------------------------------------------------

    Function: h264bsdProcessBlock

        Functional description:
            Function performs inverse zig-zag scan, inverse scaling and
            inverse transform for a luma or a chroma residual block

        Inputs:
            data            pointer to data to be processed
            qp              quantization parameter
            skip            skip processing of data[0], set to non-zero value
                            if dc coeff hanled separately
            coeffMap        16 lsb's indicate which coeffs are non-zero,
                            bit 0 (lsb) for coeff 0, bit 1 for coeff 1 etc.

        Outputs:
            data            processed data

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      processed data not in valid range [-512, 511]

------------------------------------------------------------------------------*/
u32 h264bsdProcessBlock(i32 *data, u32 qp, u32 skip, u32 coeffMap)
{

/* Variables */

    i32 tmp0, tmp1, tmp2, tmp3;
    i32 d1, d2, d3;
    u32 row,col;
    u32 qpDiv;
    i32 *ptr;

/* Code */

    qpDiv = qpDiv6[qp];
    tmp1 = levelScale[qpMod6[qp]][0] << qpDiv;
    tmp2 = levelScale[qpMod6[qp]][1] << qpDiv;
    tmp3 = levelScale[qpMod6[qp]][2] << qpDiv;

    if (!skip)
        data[0] = (data[0] * tmp1);

    /* at least one of the rows 1, 2 or 3 contain non-zero coeffs, mask takes
     * the scanning order into account */
    if (coeffMap & 0xFF9C)
    {
        /* do the zig-zag scan and inverse quantization */
        d1 = data[1];
        d2 = data[14];
        d3 = data[15];
        data[1] = (d1 * tmp2);
        data[14] = (d2 * tmp2);
        data[15] = (d3 * tmp3);

        d1 = data[2];
        d2 = data[5];
        d3 = data[4];
        data[4] = (d1 * tmp2);
        data[2]  = (d2 * tmp1);
        data[5] = (d3 * tmp3);

        d1 = data[8];
        d2 = data[3];
        d3 = data[6];
        tmp0 = (d1 * tmp2);
        data[8] = (d2 * tmp1);
        data[3]  = (d3 * tmp2);
        d1 = data[7];
        d2 = data[12];
        d3 = data[9];
        data[6]  = (d1 * tmp2);
        data[7]  = (d2 * tmp3);
        data[12] = (d3 * tmp2);
        data[9]  = tmp0;

        d1 = data[10];
        d2 = data[11];
        d3 = data[13];
        data[13] = (d1 * tmp3);
        data[10] = (d2 * tmp1);
        data[11] = (d3 * tmp2);

        /* horizontal transform */
        for (row = 4, ptr = data; row--; ptr += 4)
        {
            tmp0 = ptr[0] + ptr[2];
            tmp1 = ptr[0] - ptr[2];
            tmp2 = (ptr[1] >> 1) - ptr[3];
            tmp3 = ptr[1] + (ptr[3] >> 1);
            ptr[0] = tmp0 + tmp3;
            ptr[1] = tmp1 + tmp2;
            ptr[2] = tmp1 - tmp2;
            ptr[3] = tmp0 - tmp3;
        }

        /*lint +e661 +e662*/
        /* then vertical transform */
        for (col = 4; col--; data++)
        {
            tmp0 = data[0] + data[8];
            tmp1 = data[0] - data[8];
            tmp2 = (data[4] >> 1) - data[12];
            tmp3 = data[4] + (data[12] >> 1);
            data[0 ] = (tmp0 + tmp3 + 32)>>6;
            data[4 ] = (tmp1 + tmp2 + 32)>>6;
            data[8 ] = (tmp1 - tmp2 + 32)>>6;
            data[12] = (tmp0 - tmp3 + 32)>>6;
            /* check that each value is in the range [-512,511] */
            if (((u32)(data[0] + 512) > 1023) ||
                ((u32)(data[4] + 512) > 1023) ||
                ((u32)(data[8] + 512) > 1023) ||
                ((u32)(data[12] + 512) > 1023) )
                return(HANTRO_NOK);
        }
    }
    else /* rows 1, 2 and 3 are zero */
    {
        /* only dc-coeff is non-zero, i.e. coeffs at original positions
         * 1, 5 and 6 are zero */
        if ((coeffMap & 0x62) == 0)
        {
            tmp0 = (data[0] + 32) >> 6;
            /* check that value is in the range [-512,511] */
            if ((u32)(tmp0 + 512) > 1023)
                return(HANTRO_NOK);
            data[0] = data[1]  = data[2]  = data[3]  = data[4]  = data[5]  =
                      data[6]  = data[7]  = data[8]  = data[9]  = data[10] =
                      data[11] = data[12] = data[13] = data[14] = data[15] =
                      tmp0;
        }
        else /* at least one of the coeffs 1, 5 or 6 is non-zero */
        {
            data[1] = (data[1] * tmp2);
            data[2] = (data[5] * tmp1);
            data[3] = (data[6] * tmp2);
            tmp0 = data[0] + data[2];
            tmp1 = data[0] - data[2];
            tmp2 = (data[1] >> 1) - data[3];
            tmp3 = data[1] + (data[3] >> 1);
            data[0] = (tmp0 + tmp3 + 32)>>6;
            data[1] = (tmp1 + tmp2 + 32)>>6;
            data[2] = (tmp1 - tmp2 + 32)>>6;
            data[3] = (tmp0 - tmp3 + 32)>>6;
            data[4] = data[8] = data[12] = data[0];
            data[5] = data[9] = data[13] = data[1];
            data[6] = data[10] = data[14] = data[2];
            data[7] = data[11] = data[15] = data[3];
            /* check that each value is in the range [-512,511] */
            if (((u32)(data[0] + 512) > 1023) ||
                ((u32)(data[1] + 512) > 1023) ||
                ((u32)(data[2] + 512) > 1023) ||
                ((u32)(data[3] + 512) > 1023) )
                return(HANTRO_NOK);
        }
    }

    return(HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: h264bsdProcessLumaDc

        Functional description:
            Function performs inverse zig-zag scan, inverse transform and
            inverse scaling for a luma DC coefficients block

        Inputs:
            data            pointer to data to be processed
            qp              quantization parameter

        Outputs:
            data            processed data

        Returns:
            none

------------------------------------------------------------------------------*/
void h264bsdProcessLumaDc(i32 *data, u32 qp)
{

/* Variables */

    i32 tmp0, tmp1, tmp2, tmp3;
    u32 row,col;
    u32 qpMod, qpDiv;
    i32 levScale;
    i32 *ptr;

/* Code */

    qpMod = qpMod6[qp];
    qpDiv = qpDiv6[qp];

    /* zig-zag scan */
    tmp0 = data[2];
    data[2]  = data[5];
    data[5] = data[4];
    data[4] = tmp0;

    tmp0 = data[8];
    data[8] = data[3];
    data[3]  = data[6];
    data[6]  = data[7];
    data[7]  = data[12];
    data[12] = data[9];
    data[9]  = tmp0;

    tmp0 = data[10];
    data[10] = data[11];
    data[11] = data[13];
    data[13] = tmp0;

    /* horizontal transform */
    for (row = 4, ptr = data; row--; ptr += 4)
    {
        tmp0 = ptr[0] + ptr[2];
        tmp1 = ptr[0] - ptr[2];
        tmp2 = ptr[1] - ptr[3];
        tmp3 = ptr[1] + ptr[3];
        ptr[0] = tmp0 + tmp3;
        ptr[1] = tmp1 + tmp2;
        ptr[2] = tmp1 - tmp2;
        ptr[3] = tmp0 - tmp3;
    }

    /*lint +e661 +e662*/
    /* then vertical transform and inverse scaling */
    levScale = levelScale[ qpMod ][0];
    if (qp >= 12)
    {
        levScale <<= (qpDiv-2);
        for (col = 4; col--; data++)
        {
            tmp0 = data[0] + data[8 ];
            tmp1 = data[0] - data[8 ];
            tmp2 = data[4] - data[12];
            tmp3 = data[4] + data[12];
            data[0 ] = ((tmp0 + tmp3)*levScale);
            data[4 ] = ((tmp1 + tmp2)*levScale);
            data[8 ] = ((tmp1 - tmp2)*levScale);
            data[12] = ((tmp0 - tmp3)*levScale);
        }
    }
    else
    {
        i32 tmp;
        tmp = ((1 - qpDiv) == 0) ? 1 : 2;
        for (col = 4; col--; data++)
        {
            tmp0 = data[0] + data[8 ];
            tmp1 = data[0] - data[8 ];
            tmp2 = data[4] - data[12];
            tmp3 = data[4] + data[12];
            data[0 ] = ((tmp0 + tmp3)*levScale+tmp) >> (2-qpDiv);
            data[4 ] = ((tmp1 + tmp2)*levScale+tmp) >> (2-qpDiv);
            data[8 ] = ((tmp1 - tmp2)*levScale+tmp) >> (2-qpDiv);
            data[12] = ((tmp0 - tmp3)*levScale+tmp) >> (2-qpDiv);
        }
    }

}

/*------------------------------------------------------------------------------

    Function: h264bsdProcessChromaDc

        Functional description:
            Function performs inverse transform and inverse scaling for a
            chroma DC coefficients block

        Inputs:
            data            pointer to data to be processed, Cb then Cr
            qp              quantization parameter of Cb
            qpCr            quantization parameter of Cr

        Outputs:
            data            processed data

        Returns:
            none

------------------------------------------------------------------------------*/
void h264bsdProcessChromaDc(i32 *data, u32 qp, u32 qpCr)
{

/* Variables */

    i32 tmp0, tmp1, tmp2, tmp3;
    u32 qpDiv;
    i32 levScale;
    u32 levShift;

/* Code */

    qpDiv = qpDiv6[qp];
    levScale = levelScale[ qpMod6[qp] ][0];

    if (qp >= 6)
    {
        levScale <<= (qpDiv-1);
        levShift = 0;
    }
    else
    {
        levShift = 1;
    }

    tmp0 = data[0] + data[2];
    tmp1 = data[0] - data[2];
    tmp2 = data[1] - data[3];
    tmp3 = data[1] + data[3];
    data[0] = ((tmp0 + tmp3) * levScale) >> levShift;
    data[1] = ((tmp0 - tmp3) * levScale) >> levShift;
    data[2] = ((tmp1 + tmp2) * levScale) >> levShift;
    data[3] = ((tmp1 - tmp2) * levScale) >> levShift;

    /* Cr has its own offset in the High profiles */
    if (qpCr != qp)
    {
        qpDiv = qpDiv6[qpCr];
        levScale = levelScale[ qpMod6[qpCr] ][0];
        if (qpCr >= 6)
        {
            levScale <<= (qpDiv-1);
            levShift = 0;
        }
        else
        {
            levShift = 1;
        }
    }

    tmp0 = data[4] + data[6];
    tmp1 = data[4] - data[6];
    tmp2 = data[5] - data[7];
    tmp3 = data[5] + data[7];
    data[4] = ((tmp0 + tmp3) * levScale) >> levShift;
    data[5] = ((tmp0 - tmp3) * levScale) >> levShift;
    data[6] = ((tmp1 + tmp2) * levScale) >> levShift;
    data[7] = ((tmp1 - tmp2) * levScale) >> levShift;

}

/*------------------------------------------------------------------------------

    Function: h264bsdProcessBlock8x8

        Functional description:
            Inverse scan, inverse scaling and inverse transform of an 8x8
            luma residual block (8.5.13). CAVLC sends the 64 levels as four
            interleaved 4x4 blocks, the one of 8x8 scan position k being
            element k/4 of block k%4. The result is written back as the four
            4x4 blocks of the 8x8 block, each in raster order.

        Inputs:
            data            the four 4x4 blocks of levels
            qp              quantization parameter
            weights         weightScale8x8 in raster order

        Outputs:
            data            residual of the four 4x4 blocks

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      residual not in valid range [-512, 511]

------------------------------------------------------------------------------*/

u32 h264bsdProcessBlock8x8(i32 (*data)[16], u32 qp, const u8 *weights)
{

/* Variables */

    i32 d[64];
    u32 k, pos, qpDiv, last;
    i32 c, round;
    const u8 *v;
    i32 e0, e1, e2, e3, e4, e5, e6, e7;
    i32 f0, f1, f2, f3, f4, f5, f6, f7;
    i32 *p;

/* Code */

    qpDiv = qpDiv6[qp];
    v = normAdjust8x8[qpMod6[qp]];
    round = qpDiv < 6 ? 1 << (5 - qpDiv) : 0;

    memset(d, 0, sizeof(d));
    last = 0;
    for (k = 0; k < 64; k++)
    {
        c = data[k & 3][k >> 2];
        if (!c)
            continue;
        last = k;
        pos = h264bsdZigZag8x8[k];
        c *= (i32)(weights[pos] * v[posClass8x8[pos]]);
        if (qpDiv >= 6)
            d[pos] = c * (1 << (qpDiv - 6));
        else
            d[pos] = (c + round) >> (6 - qpDiv);
    }

    if (last == 0)
    {
        /* dc only, every sample of the block gets the same residual */
        c = (d[0] + 32) >> 6;
        if ((u32)(c + 512) > 1023)
            return(HANTRO_NOK);
        for (k = 0; k < 16; k++)
            data[0][k] = data[1][k] = data[2][k] = data[3][k] = c;
        return(HANTRO_OK);
    }

    /* rows, those without coefficients stay zero */
    for (k = 8, p = d; k--; p += 8)
    {
        if (!(p[0] | p[1] | p[2] | p[3] | p[4] | p[5] | p[6] | p[7]))
            continue;
        e0 = p[0] + p[4];
        e1 = -p[3] + p[5] - p[7] - (p[7] >> 1);
        e2 = p[0] - p[4];
        e3 = p[1] + p[7] - p[3] - (p[3] >> 1);
        e4 = (p[2] >> 1) - p[6];
        e5 = -p[1] + p[7] + p[5] + (p[5] >> 1);
        e6 = p[2] + (p[6] >> 1);
        e7 = p[3] + p[5] + p[1] + (p[1] >> 1);
        f0 = e0 + e6;
        f1 = e1 + (e7 >> 2);
        f2 = e2 + e4;
        f3 = e3 + (e5 >> 2);
        f4 = e2 - e4;
        f5 = (e3 >> 2) - e5;
        f6 = e0 - e6;
        f7 = e7 - (e1 >> 2);
        p[0] = f0 + f7;
        p[1] = f2 + f5;
        p[2] = f4 + f3;
        p[3] = f6 + f1;
        p[4] = f6 - f1;
        p[5] = f4 - f3;
        p[6] = f2 - f5;
        p[7] = f0 - f7;
    }

    /* columns, then store as four 4x4 blocks */
    for (k = 0, p = d; k < 8; k++, p++)
    {
        i32 *top = data[k >> 2] + (k & 3);
        i32 *bot = data[2 + (k >> 2)] + (k & 3);

        e0 = p[0] + p[32];
        e1 = -p[24] + p[40] - p[56] - (p[56] >> 1);
        e2 = p[0] - p[32];
        e3 = p[8] + p[56] - p[24] - (p[24] >> 1);
        e4 = (p[16] >> 1) - p[48];
        e5 = -p[8] + p[56] + p[40] + (p[40] >> 1);
        e6 = p[16] + (p[48] >> 1);
        e7 = p[24] + p[40] + p[8] + (p[8] >> 1);
        f0 = e0 + e6 + 32;
        f1 = e1 + (e7 >> 2);
        f2 = e2 + e4 + 32;
        f3 = e3 + (e5 >> 2);
        f4 = e2 - e4 + 32;
        f5 = (e3 >> 2) - e5;
        f6 = e0 - e6 + 32;
        f7 = e7 - (e1 >> 2);
        top[0]  = (f0 + f7) >> 6;
        top[4]  = (f2 + f5) >> 6;
        top[8]  = (f4 + f3) >> 6;
        top[12] = (f6 + f1) >> 6;
        bot[0]  = (f6 - f1) >> 6;
        bot[4]  = (f4 - f3) >> 6;
        bot[8]  = (f2 - f5) >> 6;
        bot[12] = (f0 - f7) >> 6;
        if (((u32)(top[0] + 512) > 1023) || ((u32)(top[4] + 512) > 1023) ||
            ((u32)(top[8] + 512) > 1023) || ((u32)(top[12] + 512) > 1023) ||
            ((u32)(bot[0] + 512) > 1023) || ((u32)(bot[4] + 512) > 1023) ||
            ((u32)(bot[8] + 512) > 1023) || ((u32)(bot[12] + 512) > 1023))
            return(HANTRO_NOK);
    }

    return(HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: InverseTransform4x4

        Functional description:
            Inverse transform of a 4x4 block of scaled coefficients in raster
            order (8.5.12.2), result written to data.

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      residual not in valid range [-512, 511]

------------------------------------------------------------------------------*/

static u32 InverseTransform4x4(i32 *data, const i32 *coeffs)
{

/* Variables */

    i32 tmp0, tmp1, tmp2, tmp3;
    i32 t[16];
    u32 row, col;
    const i32 *ptr;
    i32 *out;

/* Code */

    for (row = 4, ptr = coeffs, out = t; row--; ptr += 4, out += 4)
    {
        tmp0 = ptr[0] + ptr[2];
        tmp1 = ptr[0] - ptr[2];
        tmp2 = (ptr[1] >> 1) - ptr[3];
        tmp3 = ptr[1] + (ptr[3] >> 1);
        out[0] = tmp0 + tmp3;
        out[1] = tmp1 + tmp2;
        out[2] = tmp1 - tmp2;
        out[3] = tmp0 - tmp3;
    }
    for (col = 4, ptr = t; col--; ptr++, data++)
    {
        tmp0 = ptr[0] + ptr[8];
        tmp1 = ptr[0] - ptr[8];
        tmp2 = (ptr[4] >> 1) - ptr[12];
        tmp3 = ptr[4] + (ptr[12] >> 1);
        data[0 ] = (tmp0 + tmp3 + 32)>>6;
        data[4 ] = (tmp1 + tmp2 + 32)>>6;
        data[8 ] = (tmp1 - tmp2 + 32)>>6;
        data[12] = (tmp0 - tmp3 + 32)>>6;
        if (((u32)(data[0] + 512) > 1023) ||
            ((u32)(data[4] + 512) > 1023) ||
            ((u32)(data[8] + 512) > 1023) ||
            ((u32)(data[12] + 512) > 1023) )
            return(HANTRO_NOK);
    }

    return(HANTRO_OK);

}

/*------------------------------------------------------------------------------

    Function: h264bsdProcessBlockScaled

        Functional description:
            h264bsdProcessBlock for a non-flat scaling matrix: inverse
            zig-zag scan, inverse scaling with weightScale4x4 (8.5.12.1) and
            inverse transform of a 4x4 block.

        Inputs:
            data            levels in scan order
            qp              quantization parameter
            skip            data[0] is a dc coefficient already scaled
            weights         weightScale4x4 in raster order

        Outputs:
            data            residual in raster order

        Returns:
            HANTRO_OK       success
            HANTRO_NOK      residual not in valid range [-512, 511]

------------------------------------------------------------------------------*/

u32 h264bsdProcessBlockScaled(i32 *data, u32 qp, u32 skip, const u8 *weights)
{

/* Variables */

    i32 d[16];
    u32 k, pos, qpDiv;
    i32 c, round;
    const i32 *ls;

/* Code */

    qpDiv = qpDiv6[qp];
    ls = levelScale[qpMod6[qp]];
    round = qpDiv < 4 ? 1 << (3 - qpDiv) : 0;

    d[0] = data[0];
    for (k = skip; k < 16; k++)
    {
        pos = h264bsdZigZag4x4[k];
        c = data[k] * (i32)weights[pos] * ls[posClass4x4[pos]];
        if (qpDiv >= 4)
            d[pos] = c * (1 << (qpDiv - 4));
        else
            d[pos] = (c + round) >> (4 - qpDiv);
    }

    return(InverseTransform4x4(data, d));

}

/*------------------------------------------------------------------------------

    Function: h264bsdProcessLumaDcScaled

        Functional description:
            h264bsdProcessLumaDc for a non-flat scaling matrix.

        Inputs:
            data            Intra16x16 dc levels in scan order
            qp              quantization parameter
            weight          weightScale4x4(0,0) of the Intra Y list

        Outputs:
            data            dc coefficients in raster order

------------------------------------------------------------------------------*/

void h264bsdProcessLumaDcScaled(i32 *data, u32 qp, u32 weight)
{

/* Variables */

    i32 c[16];
    i32 tmp0, tmp1, tmp2, tmp3;
    u32 k, qpDiv;
    i32 ls, round;
    i32 *ptr;

/* Code */

    for (k = 0; k < 16; k++)
        c[h264bsdZigZag4x4[k]] = data[k];

    for (k = 4, ptr = c; k--; ptr += 4)
    {
        tmp0 = ptr[0] + ptr[2];
        tmp1 = ptr[0] - ptr[2];
        tmp2 = ptr[1] - ptr[3];
        tmp3 = ptr[1] + ptr[3];
        ptr[0] = tmp0 + tmp3;
        ptr[1] = tmp1 + tmp2;
        ptr[2] = tmp1 - tmp2;
        ptr[3] = tmp0 - tmp3;
    }
    for (k = 4, ptr = c; k--; ptr++)
    {
        tmp0 = ptr[0] + ptr[8];
        tmp1 = ptr[0] - ptr[8];
        tmp2 = ptr[4] - ptr[12];
        tmp3 = ptr[4] + ptr[12];
        ptr[0] = tmp0 + tmp3;
        ptr[4] = tmp1 + tmp2;
        ptr[8] = tmp1 - tmp2;
        ptr[12] = tmp0 - tmp3;
    }

    qpDiv = qpDiv6[qp];
    ls = (i32)weight * levelScale[qpMod6[qp]][0];
    round = qpDiv < 6 ? 1 << (5 - qpDiv) : 0;
    for (k = 0; k < 16; k++)
    {
        if (qpDiv >= 6)
            data[k] = c[k] * ls * (1 << (qpDiv - 6));
        else
            data[k] = (c[k] * ls + round) >> (6 - qpDiv);
    }

}

/*------------------------------------------------------------------------------

    Function: h264bsdProcessChromaDcScaled

        Functional description:
            h264bsdProcessChromaDc for a non-flat scaling matrix, for the four
            dc coefficients of one chroma component.

        Inputs:
            data            dc levels
            qp              chroma quantization parameter
            weight          weightScale4x4(0,0) of the chroma list

        Outputs:
            data            dc coefficients

------------------------------------------------------------------------------*/

void h264bsdProcessChromaDcScaled(i32 *data, u32 qp, u32 weight)
{

/* Variables */

    i32 tmp0, tmp1, tmp2, tmp3;
    i32 ls;
    u32 qpDiv;

/* Code */

    qpDiv = qpDiv6[qp];
    ls = (i32)weight * levelScale[qpMod6[qp]][0];

    tmp0 = data[0] + data[2];
    tmp1 = data[0] - data[2];
    tmp2 = data[1] - data[3];
    tmp3 = data[1] + data[3];
    data[0] = ((tmp0 + tmp3) * ls * (1 << qpDiv)) >> 5;
    data[1] = ((tmp0 - tmp3) * ls * (1 << qpDiv)) >> 5;
    data[2] = ((tmp1 + tmp2) * ls * (1 << qpDiv)) >> 5;
    data[3] = ((tmp1 - tmp2) * ls * (1 << qpDiv)) >> 5;

}

/*lint +e701 +e702 */
