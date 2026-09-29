/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * The constants of the PIE kernels, and the check that turns them on: every
 * kernel is run against the C code it replaces, on a picture of noise and
 * extremes, over all the block sizes, fractional positions and picture
 * borders the decoder can hand it. A wrong guess about an instruction then
 * costs the speed, not the picture.
 */

#include "h264bsd_pie.h"

#ifdef H264BSD_HAS_PIE

#include <stdlib.h>
#include <string.h>

#include "h264bsd_image.h"
#include "h264bsd_macroblock_layer.h"
#include "h264bsd_reconstruct.h"
#include "h264bsd_util.h"

u32 h264bsdPieOn;

/* The layout is shared with h264bsd_pie.S (the K_* offsets there) */
const u8 h264bsdPieK[112] __attribute__((aligned(16))) = {
    /* 0: the 6-tap coefficients as signed byte lanes */
    1, (u8)-5, 20, 20, (u8)-5, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    /* 16: the sign flip of a pixel */
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
    /* 32: rounding for a shift by 5 */
    16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
    /* 48: ones */
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    /* 64: rounding for a shift by 6 */
    32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32,
    /* 80: the coefficients as 16-bit lanes, then 512 and 1 */
    1, 0, (u8)-5, 0xFF, 20, 0, 20, 0, (u8)-5, 0xFF, 1, 0, 0, 2, 1, 0,
    /* 96: 255 and 512 as 16-bit values */
    255, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

/* A 3 x 3 macroblock test picture */
#define TEST_MBS   3
#define TEST_SIZE  (TEST_MBS * TEST_MBS * 384)

static u32 testSeed;

static u8 testValue(u32 mode)
{
    testSeed ^= testSeed << 13;
    testSeed ^= testSeed >> 17;
    testSeed ^= testSeed << 5;
    switch (mode)
    {
        case 0:  return (u8)(testSeed >> 24);
        case 1:  return (testSeed >> 24) & 1 ? 255 : 0;
        default: return (testSeed >> 24) & 3 ? 0 : 255;
    }
}

/* Runs one prediction with the kernels on and off, into out[0] and out[1] */
static u32 predictBoth(u8 *out[2], mv_t *mv, image_t *ref, u32 xA, u32 yA,
                       u32 partX, u32 partY, u32 w, u32 h)
{
    u32 i;

    for (i = 0; i < 2; i++)
    {
        h264bsdPieOn = i == 0;
        memset(out[i], 0, 384);
        h264bsdPredictSamples(out[i], mv, ref, xA, yA, partX, partY, w, h);
    }
    h264bsdPieOn = 0;

    return memcmp(out[0], out[1], 384) == 0;
}

static u32 pieCheck(u8 *pic, u8 *out[2], u8 *pic2)
{
    static const u8 sizes[7][2] = {
        {16, 16}, {16, 8}, {8, 16}, {8, 8}, {8, 4}, {4, 8}, {4, 4}};
    /* motion vectors in quarter pixels, some reaching past the borders */
    static const i16 offsets[6] = {0, 5, -13, 43, -101, 137};
    image_t ref, cur;
    mv_t mv;
    u32 mode, s, ox, oy, fx, fy, i, ok = 1;

    ref.width = TEST_MBS;
    ref.height = TEST_MBS;
    ref.data = pic;

    for (mode = 0; mode < 3 && ok; mode++)
    {
        for (i = 0; i < TEST_SIZE; i++)
            pic[i] = testValue(mode);

        for (s = 0; s < 7 && ok; s++)
        {
            u32 w = sizes[s][0], h = sizes[s][1];
            u32 partX = (testSeed >> 8) % (16 / w) * w;
            u32 partY = (testSeed >> 16) % (16 / h) * h;

            for (ox = 0; ox < 6 && ok; ox++)
            {
                for (oy = 0; oy < 6 && ok; oy++)
                {
                    for (fx = 0; fx < 4 && ok; fx++)
                    {
                        for (fy = 0; fy < 4 && ok; fy++)
                        {
                            mv.hor = (i16)((offsets[ox] & ~3) + (i16)fx);
                            mv.ver = (i16)((offsets[oy] & ~3) + (i16)fy);
                            ok = predictBoth(out, &mv, &ref, 16, 16, partX,
                                             partY, w, h);
                        }
                    }
                }
            }
        }
    }

    /* the write-out of a macroblock */
    for (i = 0; i < 384 && ok; i++)
        out[0][i] = testValue(0);
    cur.width = TEST_MBS;
    cur.height = TEST_MBS;
    for (i = 0; i < 2 && ok; i++)
    {
        cur.data = pic2 + i * TEST_SIZE;
        memset(cur.data, 0x55, TEST_SIZE);
        cur.luma = cur.data + 16 * TEST_MBS * 16 + 16;
        cur.cb = cur.data + TEST_MBS * TEST_MBS * 256 + 8 * TEST_MBS * 8 + 8;
        cur.cr = cur.cb + TEST_MBS * TEST_MBS * 64;
        h264bsdPieOn = i == 0;
        h264bsdWriteMacroblock(&cur, out[0]);
    }
    h264bsdPieOn = 0;
    if (ok)
        ok = memcmp(pic2, pic2 + TEST_SIZE, TEST_SIZE) == 0;

    return ok;
}

void h264bsdPieInit(void)
{
    static u32 checked;
    u8 *buf, *pic, *out[2], *pic2;

    if (checked)
        return;
    checked = 1;
    h264bsdPieOn = 0;

    /* the picture, two prediction outputs and two written pictures */
    buf = (u8*)malloc(TEST_SIZE + 2 * 384 + 2 * TEST_SIZE + 4 * 16);
    if (buf == NULL)
        return;
    pic = (u8*)ALIGN(buf, 16);
    out[0] = (u8*)ALIGN(pic + TEST_SIZE, 16);
    out[1] = (u8*)ALIGN(out[0] + 384, 16);
    pic2 = (u8*)ALIGN(out[1] + 384, 16);

    testSeed = 0x2545F491;
    H264BSD_SIMD_ENTER();
    h264bsdPieOn = pieCheck(pic, out, pic2);
    H264BSD_SIMD_LEAVE();

    free(buf);
}

#endif /* H264BSD_HAS_PIE */
