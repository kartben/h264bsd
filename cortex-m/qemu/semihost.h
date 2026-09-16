/* Minimal Arm semihosting layer for bare-metal tests under QEMU. */
#ifndef H264BSD_SEMIHOST_H
#define H264BSD_SEMIHOST_H

#include <stddef.h>
#include <stdint.h>

int  sh_open(const char *name, int mode);   /* mode: 0 = "r", 1 = "rb", 4 = "w", 5 = "wb" */
int  sh_close(int fd);
int  sh_read(int fd, void *buf, size_t len);   /* returns bytes read */
int  sh_write(int fd, const void *buf, size_t len); /* returns bytes written */
long sh_flen(int fd);
void sh_write0(const char *s);              /* string to the host console */
int  sh_get_cmdline(char *buf, size_t len);
void sh_exit(int code) __attribute__((noreturn));

void sh_puts(const char *s);
void sh_put_uint(uint32_t v);
void sh_put_hex(uint32_t v);

/* provided by startup.c: stack/heap high-water marks */
void sh_report_memory(void);

#endif
