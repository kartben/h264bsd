/*
 * h264bsd test application for a bare-metal Cortex-M33 under QEMU.
 *
 * Reads an Annex-B .h264 file from the host through semihosting, decodes
 * it and prints one hash line per output picture. The lines are identical
 * to those printed by "posix/test_h264bsd -h", so the two can be diffed
 * to prove the Cortex-M build is bit-exact.
 */
#include <stdlib.h>
#include <string.h>
#include "semihost.h"
#include "h264bsd_decoder.h"
#include "h264bsd_util.h"
#include "frame_hash.h"

static u8 *loadFile(const char *path, u32 *size)
{
    int fd = sh_open(path, 1);
    long len;
    u8 *buf;
    int got, off = 0;

    if (fd < 0) return NULL;
    len = sh_flen(fd);
    if (len <= 0) { sh_close(fd); return NULL; }
    buf = (u8 *)malloc((size_t)len);
    if (!buf) { sh_close(fd); return NULL; }
    while (off < len) {
        got = sh_read(fd, buf + off, (size_t)(len - off));
        if (got <= 0) break;
        off += got;
    }
    sh_close(fd);
    *size = (u32)off;
    return buf;
}

int main(int argc, char **argv)
{
    const char *path = argc > 0 ? argv[argc - 1] : NULL;
    u8 *stream, *byteStrm, *pic;
    u32 len, readBytes, picId, isIdr, numErrMbs;
    u32 width = 0, height = 0, numPics = 0;
    storage_t *dec;

    if (!path) {
        sh_puts("usage: pass the input file with -semihosting-config ...,arg=<file.h264>\n");
        return 2;
    }

    stream = loadFile(path, &len);
    if (!stream) {
        sh_puts("failed to load input file\n");
        return 2;
    }
    sh_puts("loaded "); sh_put_uint(len); sh_puts(" bytes\n");

    dec = h264bsdAlloc();
    if (!dec || h264bsdInit(dec, HANTRO_FALSE) != HANTRO_OK) {
        sh_puts("h264bsdInit failed\n");
        return 1;
    }

    byteStrm = stream;
    while (len > 0) {
        u32 result = h264bsdDecode(dec, byteStrm, len, 0, &readBytes);
        len -= readBytes;
        byteStrm += readBytes;

        switch (result) {
        case H264BSD_PIC_RDY:
            pic = h264bsdNextOutputPicture(dec, &picId, &isIdr, &numErrMbs);
            numPics++;
            sh_puts("pic "); sh_put_uint(numPics);
            sh_puts(" hash 0x"); sh_put_hex(frameHash(pic, width * height * 3 / 2));
            sh_puts("\n");
            break;
        case H264BSD_HDRS_RDY:
            width = h264bsdPicWidth(dec) * 16;
            height = h264bsdPicHeight(dec) * 16;
            sh_puts("headers: "); sh_put_uint(width); sh_puts("x");
            sh_put_uint(height); sh_puts("\n");
            break;
        case H264BSD_RDY:
            break;
        case H264BSD_ERROR:
            sh_puts("decode error\n");
            return 1;
        case H264BSD_PARAM_SET_ERROR:
            sh_puts("param set error\n");
            return 1;
        case H264BSD_MEMALLOC_ERROR:
            sh_puts("memory allocation error\n");
            return 1;
        }
    }

    h264bsdShutdown(dec);
    h264bsdFree(dec);
    free(stream);

    sh_puts("done: "); sh_put_uint(numPics); sh_puts(" pictures\n");
    sh_report_memory();
    return 0;
}
