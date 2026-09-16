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
          h264bsdWriteMacroblock
          h264bsdWriteOutputBlocks

------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include <stdint.h>
#include "h264bsd_image.h"
#include "h264bsd_util.h"
#include "h264bsd_neighbour.h"
#include "h264bsd_platform.h"

/*------------------------------------------------------------------------------
    2. External compiler flags
--------------------------------------------------------------------------------

--------------------------------------------------------------------------------
    3. Module defines
------------------------------------------------------------------------------*/

/* x- and y-coordinates for each block, defined in h264bsd_intra_prediction.c */
extern const u32 h264bsdBlockX[];
extern const u32 h264bsdBlockY[];

/* clipping table, defined in h264bsd_intra_prediction.c */
extern const u8 h264bsdClip[];

/*------------------------------------------------------------------------------
    4. Local function prototypes
------------------------------------------------------------------------------*/



/*------------------------------------------------------------------------------

    Function: h264bsdWriteMacroblock

        Functional description:
            Write one macroblock into the image. Both luma and chroma
            components will be written at the same time.

        Inputs:
            data    pointer to macroblock data to be written, 256 values for
                    luma followed by 64 values for both chroma components

        Outputs:
            image   pointer to the image where the macroblock will be written

        Returns:
            none

------------------------------------------------------------------------------*/
#ifndef H264DEC_NEON
H264BSD_FAST_CODE
void h264bsdWriteMacroblock(image_t *image, u8 *data)
{

/* Variables */

    u32 i;
    u32 width;
    u32 *lum, *cb, *cr;
    const u32 *ptr;

/* Code */

    ASSERT(image);
    ASSERT(data);
    ASSERT(!((uintptr_t)data&0x3));

    width = image->width;

    /*lint -save -e826 lum, cb and cr used to copy 4 bytes at the time, disable
     * "area too small" info message */
    lum = (u32*)image->luma;
    cb = (u32*)image->cb;
    cr = (u32*)image->cr;
    ASSERT(!((uintptr_t)lum&0x3));
    ASSERT(!((uintptr_t)cb&0x3));
    ASSERT(!((uintptr_t)cr&0x3));

    ptr = (const u32*)data;

    /* one 16-byte row per iteration; the compiler turns the four word
     * copies into LDM/STM or LDRD/STRD pairs */
    width *= 4;
    for (i = 16; i ; i--)
    {
        u32 t0 = ptr[0], t1 = ptr[1], t2 = ptr[2], t3 = ptr[3];
        lum[0] = t0; lum[1] = t1; lum[2] = t2; lum[3] = t3;
        ptr += 4;
        lum += width;
    }

    width >>= 1;
    for (i = 8; i ; i--)
    {
        u32 t0 = ptr[0], t1 = ptr[1];
        cb[0] = t0; cb[1] = t1;
        ptr += 2;
        cb += width;
    }

    for (i = 8; i ; i--)
    {
        u32 t0 = ptr[0], t1 = ptr[1];
        cr[0] = t0; cr[1] = t1;
        ptr += 2;
        cr += width;
    }

}
#endif
/*------------------------------------------------------------------------------

    Function: h264bsdCopyMacroblock

        Functional description:
            Copy one macroblock (luma and chroma) from a reference picture
            straight into the current picture, for skipped macroblocks whose
            motion vector points to full samples. This avoids the round trip
            through the 384-byte macroblock buffer.

        Inputs:
            image       current picture; luma/cb/cr must point to the
                        current macroblock (h264bsdSetCurrImageMbPointers)
            refData     reference picture data
            x, y        position of the macroblock in pixels
            dx, dy      integer motion vector in luma pixels, both even and
                        such that the source block lies inside the picture

------------------------------------------------------------------------------*/
H264BSD_FAST_CODE
void h264bsdCopyMacroblock(image_t *image, const u8 *refData,
    u32 x, u32 y, i32 dx, i32 dy)
{
    u32 width = image->width * 16;
    u32 picSize = width * image->height * 16;
    const u8 *src;
    u8 *dst;
    u32 i;

    ASSERT(!(dx & 1) && !(dy & 1));

    src = refData + ((i32)y + dy) * (i32)width + (i32)x + dx;
    dst = image->luma;
    for (i = 16; i; i--)
    {
        u32 t0 = h264bsdLoadU32(src);
        u32 t1 = h264bsdLoadU32(src + 4);
        u32 t2 = h264bsdLoadU32(src + 8);
        u32 t3 = h264bsdLoadU32(src + 12);
        h264bsdStoreU32(dst, t0);
        h264bsdStoreU32(dst + 4, t1);
        h264bsdStoreU32(dst + 8, t2);
        h264bsdStoreU32(dst + 12, t3);
        src += width;
        dst += width;
    }

    width >>= 1;
    src = refData + picSize + ((i32)(y >> 1) + (dy >> 1)) * (i32)width +
          (i32)(x >> 1) + (dx >> 1);
    dst = image->cb;
    for (i = 8; i; i--)
    {
        u32 t0 = h264bsdLoadU32(src);
        u32 t1 = h264bsdLoadU32(src + 4);
        h264bsdStoreU32(dst, t0);
        h264bsdStoreU32(dst + 4, t1);
        src += width;
        dst += width;
    }

    src += (picSize >> 2) - 8 * width;
    dst = image->cr;
    for (i = 8; i; i--)
    {
        u32 t0 = h264bsdLoadU32(src);
        u32 t1 = h264bsdLoadU32(src + 4);
        h264bsdStoreU32(dst, t0);
        h264bsdStoreU32(dst + 4, t1);
        src += width;
        dst += width;
    }
}

#ifndef H264DEC_OMXDL
/*------------------------------------------------------------------------------

    Function: h264bsdWriteOutputBlocks

        Functional description:
            Write one macroblock into the image. Prediction for the macroblock
            and the residual are given separately and will be combined while
            writing the data to the image

        Inputs:
            data        pointer to macroblock prediction data, 256 values for
                        luma followed by 64 values for both chroma components
            mbNum       number of the macroblock
            residual    pointer to residual data, 16 16-element arrays for luma
                        followed by 4 16-element arrays for both chroma
                        components

        Outputs:
            image       pointer to the image where the data will be written

        Returns:
            none

------------------------------------------------------------------------------*/

H264BSD_FAST_CODE
void h264bsdWriteOutputBlocks(image_t *image, u32 mbNum, u8 *data,
        i32 residual[][16])
{

/* Variables */

    u32 i;
    u32 picWidth, picSize;
    u8 *lum, *cb, *cr;
    u8 *imageBlock;
    u8 *tmp;
    u32 row, col;
    u32 block;
    u32 x, y;
    i32 *pRes;
#if !H264BSD_PACKED_KERNELS
    const u8 *clp = h264bsdClip + 512;
#endif

/* Code */

    ASSERT(image);
    ASSERT(data);
    ASSERT(mbNum < image->width * image->height);
    ASSERT(!((uintptr_t)data&0x3));

    /* Image size in macroblocks */
    picWidth = image->width;
    picSize = picWidth * image->height;
    row = mbNum / picWidth;
    col = mbNum % picWidth;

    /* Output macroblock position in output picture */
    lum = (image->data + row * picWidth * 256 + col * 16);
    cb = (image->data + picSize * 256 + row * picWidth * 64 + col * 8);
    cr = (cb + picSize * 64);

    picWidth *= 16;

    for (block = 0; block < 16; block++)
    {
        x = h264bsdBlockX[block];
        y = h264bsdBlockY[block];

        pRes = residual[block];

        ASSERT(pRes);

        tmp = data + y*16 + x;
        imageBlock = lum + y*picWidth + x;

        ASSERT(!((uintptr_t)tmp&0x3));
        ASSERT(!((uintptr_t)imageBlock&0x3));

        if (IS_RESIDUAL_EMPTY(pRes))
        {
            /*lint -e826 */
            const u32 *in32 = (const u32*)tmp;
            u32 *out32 = (u32*)imageBlock;
            u32 t0, t1;

            /* Residual is zero => copy prediction block to output */
            t0 = in32[0]; t1 = in32[4];
            out32[0] = t0; out32 += picWidth/4;
            out32[0] = t1; out32 += picWidth/4;
            t0 = in32[8]; t1 = in32[12];
            out32[0] = t0; out32 += picWidth/4;
            out32[0] = t1;
        }
        else
        {

            RANGE_CHECK_ARRAY(pRes, -512, 511, 16);

            /* Calculate image = prediction + residual
             * Process four pixels in a loop */
            for (i = 4; i; i--)
            {
#if H264BSD_PACKED_KERNELS
                h264bsdStoreU32(imageBlock,
                    h264bsdAddResidualWord(h264bsdLoadU32(tmp), pRes));
                pRes += 4;
#else
                i32 tmp1, tmp2, tmp3, tmp4;
                tmp1 = tmp[0];
                tmp2 = *pRes++;
                tmp3 = tmp[1];
                tmp1 = clp[tmp1 + tmp2];
                tmp4 = *pRes++;
                imageBlock[0] = (u8)tmp1;
                tmp3 = clp[tmp3 + tmp4];
                tmp1 = tmp[2];
                tmp2 = *pRes++;
                imageBlock[1] = (u8)tmp3;
                tmp1 = clp[tmp1 + tmp2];
                tmp3 = tmp[3];
                tmp4 = *pRes++;
                imageBlock[2] = (u8)tmp1;
                tmp3 = clp[tmp3 + tmp4];
                imageBlock[3] = (u8)tmp3;
#endif
                tmp += 16;
                imageBlock += picWidth;
            }
        }

    }

    picWidth /= 2;

    for (block = 16; block <= 23; block++)
    {
        x = h264bsdBlockX[block & 0x3];
        y = h264bsdBlockY[block & 0x3];

        pRes = residual[block];

        ASSERT(pRes);

        tmp = data + 256;
        imageBlock = cb;

        if (block >= 20)
        {
            imageBlock = cr;
            tmp += 64;
        }

        tmp += y*8 + x;
        imageBlock += y*picWidth + x;

        ASSERT(!((uintptr_t)tmp&0x3));
        ASSERT(!((uintptr_t)imageBlock&0x3));

        if (IS_RESIDUAL_EMPTY(pRes))
        {
            /*lint -e826 */
            const u32 *in32 = (const u32*)tmp;
            u32 *out32 = (u32*)imageBlock;
            u32 t0, t1;

            /* Residual is zero => copy prediction block to output */
            t0 = in32[0]; t1 = in32[2];
            out32[0] = t0; out32 += picWidth/4;
            out32[0] = t1; out32 += picWidth/4;
            t0 = in32[4]; t1 = in32[6];
            out32[0] = t0; out32 += picWidth/4;
            out32[0] = t1;
        }
        else
        {

            RANGE_CHECK_ARRAY(pRes, -512, 511, 16);

            for (i = 4; i; i--)
            {
#if H264BSD_PACKED_KERNELS
                h264bsdStoreU32(imageBlock,
                    h264bsdAddResidualWord(h264bsdLoadU32(tmp), pRes));
                pRes += 4;
#else
                i32 tmp1, tmp2, tmp3, tmp4;
                tmp1 = tmp[0];
                tmp2 = *pRes++;
                tmp3 = tmp[1];
                tmp1 = clp[tmp1 + tmp2];
                tmp4 = *pRes++;
                imageBlock[0] = (u8)tmp1;
                tmp3 = clp[tmp3 + tmp4];
                tmp1 = tmp[2];
                tmp2 = *pRes++;
                imageBlock[1] = (u8)tmp3;
                tmp1 = clp[tmp1 + tmp2];
                tmp3 = tmp[3];
                tmp4 = *pRes++;
                imageBlock[2] = (u8)tmp1;
                tmp3 = clp[tmp3 + tmp4];
                imageBlock[3] = (u8)tmp3;
#endif
                tmp += 8;
                imageBlock += picWidth;
            }
        }
    }

}
#endif /* H264DEC_OMXDL */

