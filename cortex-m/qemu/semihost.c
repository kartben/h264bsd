/* Minimal Arm semihosting layer for bare-metal tests under QEMU. */
#include "semihost.h"

#define SYS_OPEN         0x01
#define SYS_CLOSE        0x02
#define SYS_WRITE0       0x04
#define SYS_WRITE        0x05
#define SYS_READ         0x06
#define SYS_FLEN         0x0C
#define SYS_GET_CMDLINE  0x15
#define SYS_EXIT_EXTENDED 0x20

#define ADP_Stopped_ApplicationExit 0x20026

static inline int sh_call(int op, void *arg)
{
    register int r0 __asm__("r0") = op;
    register void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

static size_t sh_strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int sh_open(const char *name, int mode)
{
    uintptr_t args[3] = { (uintptr_t)name, (uintptr_t)mode, sh_strlen(name) };
    return sh_call(SYS_OPEN, args);
}

int sh_close(int fd)
{
    uintptr_t args[1] = { (uintptr_t)fd };
    return sh_call(SYS_CLOSE, args);
}

int sh_read(int fd, void *buf, size_t len)
{
    uintptr_t args[3] = { (uintptr_t)fd, (uintptr_t)buf, len };
    int notRead = sh_call(SYS_READ, args);
    if (notRead < 0) return -1;
    return (int)(len - (size_t)notRead);
}

int sh_write(int fd, const void *buf, size_t len)
{
    uintptr_t args[3] = { (uintptr_t)fd, (uintptr_t)buf, len };
    int notWritten = sh_call(SYS_WRITE, args);
    if (notWritten < 0) return -1;
    return (int)(len - (size_t)notWritten);
}

long sh_flen(int fd)
{
    uintptr_t args[1] = { (uintptr_t)fd };
    return sh_call(SYS_FLEN, args);
}

void sh_write0(const char *s)
{
    sh_call(SYS_WRITE0, (void *)s);
}

int sh_get_cmdline(char *buf, size_t len)
{
    uintptr_t args[2] = { (uintptr_t)buf, len };
    return sh_call(SYS_GET_CMDLINE, args);
}

void sh_exit(int code)
{
    uintptr_t args[2] = { ADP_Stopped_ApplicationExit, (uintptr_t)code };
    for (;;) sh_call(SYS_EXIT_EXTENDED, args);
}

void sh_puts(const char *s) { sh_write0(s); }

void sh_put_uint(uint32_t v)
{
    char buf[12];
    int i = 11;
    buf[i] = 0;
    do { buf[--i] = (char)('0' + v % 10); v /= 10; } while (v);
    sh_write0(buf + i);
}

void sh_put_hex(uint32_t v)
{
    static const char hex[] = "0123456789abcdef";
    char buf[9];
    for (int i = 7; i >= 0; i--) { buf[i] = hex[v & 0xF]; v >>= 4; }
    buf[8] = 0;
    sh_write0(buf);
}
