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

#ifndef H264SWDEC_UTIL_H
#define H264SWDEC_UTIL_H

/*------------------------------------------------------------------------------
    1. Include headers
------------------------------------------------------------------------------*/

#include <stdint.h>
#include "basetype.h"
#include "h264bsd_cfg.h"
#include "h264bsd_stream.h"
#include "h264bsd_image.h"

#ifdef _ASSERT_USED
#include <assert.h>
#endif

#if defined(_RANGE_CHECK) || defined(_DEBUG_PRINT) || defined(_ERROR_PRINT)
#include <stdio.h>
#endif

/*------------------------------------------------------------------------------
    2. Module defines
------------------------------------------------------------------------------*/

#define HANTRO_OK   0
#define HANTRO_NOK  1

#define HANTRO_TRUE     (1)
#define HANTRO_FALSE    (0)

#ifndef NULL
#define NULL 0
#endif

#define MEMORY_ALLOCATION_ERROR 0xFFFF
#define PARAM_SET_ERROR 0xFFF0

/* value to be returned by GetBits if stream buffer is empty */
#define END_OF_STREAM 0xFFFFFFFFU

#define EMPTY_RESIDUAL_INDICATOR 0xFFFFFF

/* macro to mark a residual block empty, i.e. contain zero coefficients */
#define MARK_RESIDUAL_EMPTY(residual) ((residual)[0] = EMPTY_RESIDUAL_INDICATOR)
/* macro to check if residual block is empty */
#define IS_RESIDUAL_EMPTY(residual) ((residual)[0] == EMPTY_RESIDUAL_INDICATOR)

/* Clears or copies whole 32-bit words, for the small aligned pieces of state
 * a macroblock's decoding starts from: the C library's memset and memcpy are
 * byte loops in the ROM of some parts, and the compiler is not told the
 * alignment. A word type that may alias anything, and volatile stores so
 * that the loop is not turned back into a call. */
#if defined(__GNUC__)
typedef u32 __attribute__((may_alias)) h264bsdWord_t;
#else
typedef u32 h264bsdWord_t;
#endif

static inline void h264bsdClearWords(void *p, u32 words)
{
    volatile h264bsdWord_t *d = (volatile h264bsdWord_t *)p;

    while (words--)
        *d++ = 0;
}

static inline void h264bsdCopyWords(void *dst, const void *src, u32 words)
{
    volatile h264bsdWord_t *d = (volatile h264bsdWord_t *)dst;
    const h264bsdWord_t *s = (const h264bsdWord_t *)src;

    while (words--)
        *d++ = *s++;
}

/* Optional cycle profile of the decoding stages (H264BSD_PROFILE on an
 * Xtensa target): each stage accumulates its cycles and its calls. */
#if defined(H264BSD_PROFILE) && defined(__XTENSA__)
enum {
    H264BSD_PROF_PARSE = 0, /* macroblock layer parsing, residual included */
    H264BSD_PROF_CAVLC,     /* the residual part of it */
    H264BSD_PROF_TRANSFORM, /* inverse quantisation and transforms */
    H264BSD_PROF_INTRA,     /* intra prediction and write-out */
    H264BSD_PROF_INTER,     /* inter prediction and write-out */
    H264BSD_PROF_DEBLOCK,   /* the loop filter of a picture */
    H264BSD_PROF_TOTAL,     /* h264bsdDecode */
    H264BSD_PROF_COUNT
};
extern u32 h264bsdProfCycles[H264BSD_PROF_COUNT];
extern u32 h264bsdProfCalls[H264BSD_PROF_COUNT];
static inline u32 h264bsdProfClock(void)
{
    u32 c;
    __asm__ volatile("rsr.ccount %0" : "=a"(c));
    return c;
}
#define H264BSD_PROF_START(v) u32 v = h264bsdProfClock()
#define H264BSD_PROF_STOP(v, id) \
    do { \
        h264bsdProfCycles[id] += h264bsdProfClock() - (v); \
        h264bsdProfCalls[id]++; \
    } while (0)
#else
#define H264BSD_PROF_START(v) ((void)0)
#define H264BSD_PROF_STOP(v, id) ((void)0)
#endif

/* macro for assertion, used only if compiler flag _ASSERT_USED is defined */
#ifdef _ASSERT_USED
#define ASSERT(expr) assert(expr)
#else
#define ASSERT(expr)
#endif

/* macro for range checking an value, used only if compiler flag _RANGE_CHECK
 * is defined */
#ifdef _RANGE_CHECK
#define RANGE_CHECK(value, minBound, maxBound) \
{ \
    if ((value) < (minBound) || (value) > (maxBound)) \
        fprintf(stderr, "Warning: Value exceeds given limit(s)!\n"); \
}
#else
#define RANGE_CHECK(value, minBound, maxBound)
#endif

/* macro for range checking an array, used only if compiler flag _RANGE_CHECK
 * is defined */
#ifdef _RANGE_CHECK
#define RANGE_CHECK_ARRAY(array, minBound, maxBound, length) \
{ \
    i32 i; \
    for (i = 0; i < (length); i++) \
        if ((array)[i] < (minBound) || (array)[i] > (maxBound)) \
            fprintf(stderr,"Warning: Value [%d] exceeds given limit(s)!\n",i); \
}
#else
#define RANGE_CHECK_ARRAY(array, minBound, maxBound, length)
#endif

/* macro for debug printing, used only if compiler flag _DEBUG_PRINT is
 * defined */
#ifdef _DEBUG_PRINT
#define DEBUG(args) printf args
#else
#define DEBUG(args)
#endif

/* macro for error printing, used only if compiler flag _ERROR_PRINT is
 * defined */
#ifdef _ERROR_PRINT
#define EPRINT(msg) fprintf(stderr,"ERROR: %s\n",msg)
#else
#define EPRINT(msg)
#endif

/* macro to get smaller of two values */
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/* macro to get greater of two values */
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

/* macro to get absolute value */
#define ABS(a) (((a) < 0) ? -(a) : (a))

/* macro to clip a value z, so that x <= z =< y */
#define CLIP3(x,y,z) (((z) < (x)) ? (x) : (((z) > (y)) ? (y) : (z)))

/* macro to clip a value z, so that 0 <= z =< 255 */
#define CLIP1(z) (((z) < 0) ? 0 : (((z) > 255) ? 255 : (z)))

/* macro to allocate memory */
#define ALLOCATE(ptr, count, type) \
{ \
    (ptr) = malloc((count) * sizeof(type)); \
}

/* macro to free allocated memory */
#define FREE(ptr) \
{ \
    free((ptr)); (ptr) = NULL; \
}

/* The decoded pictures are far larger than everything else the decoder holds
 * put together, and on a part whose fast memory cannot take them they are the
 * one thing that has to live somewhere else. Name a pair of functions of your
 * own to keep them apart; by default they go where the rest goes. */
#ifndef H264BSD_PICTURE_MALLOC
#define H264BSD_PICTURE_MALLOC malloc
#endif
#ifndef H264BSD_PICTURE_FREE
#define H264BSD_PICTURE_FREE free
#endif

#define ALLOCATE_PICTURE(ptr, count, type) \
{ \
    (ptr) = H264BSD_PICTURE_MALLOC((count) * sizeof(type)); \
}

#define FREE_PICTURE(ptr) \
{ \
    H264BSD_PICTURE_FREE((ptr)); (ptr) = NULL; \
}

/* Named rather than included, so a function of the caller's own needs no
 * header of ours. Where the names are still malloc and free this repeats what
 * stdlib.h already says. */
void *H264BSD_PICTURE_MALLOC(size_t size);
void H264BSD_PICTURE_FREE(void *ptr);

#define ALIGN(ptr, bytePos) \
        (ptr + ( ((bytePos - (uintptr_t)ptr) & (bytePos - 1)) / sizeof(*ptr) ))

extern const u32 h264bsdQpC[52];

/*------------------------------------------------------------------------------
    3. Data types
------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------
    4. Function prototypes
------------------------------------------------------------------------------*/
#ifndef H264DEC_NEON
u32 h264bsdCountLeadingZeros(u32 value, u32 length);
#else
u32 h264bsdCountLeadingZeros(u32 value);
#endif
u32 h264bsdRbspTrailingBits(strmData_t *strmData);

u32 h264bsdMoreRbspData(strmData_t *strmData);

u32 h264bsdNextMbAddress(u32 *pSliceGroupMap, u32 picSizeInMbs, u32 currMbAddr);

void h264bsdSetCurrImageMbPointers(image_t *image, u32 mbNum);

i32 abs(i32 a);
i32 clip(i32 x, i32 y, i32 z);

#endif /* #ifdef H264SWDEC_UTIL_H */

