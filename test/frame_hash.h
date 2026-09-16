/*
 * Small, fast hash of a decoded picture, shared by the posix and Cortex-M
 * test applications so their outputs can be compared line by line.
 * FNV-1a over 32-bit words (picture buffers are always a multiple of 4 bytes).
 */
#ifndef H264BSD_FRAME_HASH_H
#define H264BSD_FRAME_HASH_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

static inline uint32_t frameHash(const uint8_t *data, size_t bytes)
{
    uint32_t h = 0x811C9DC5u;
    size_t words = bytes / 4, i;
    for (i = 0; i < words; i++) {
        uint32_t w;
        memcpy(&w, data + 4 * i, 4);
        h = (h ^ w) * 0x01000193u;
    }
    for (i = words * 4; i < bytes; i++)
        h = (h ^ data[i]) * 0x01000193u;
    return h;
}

#endif
