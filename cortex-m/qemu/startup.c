/*
 * Bare-metal startup for Cortex-M33 (QEMU mps2-an505). Provides the vector
 * table, C runtime initialisation, fault reporting through semihosting and
 * the newlib hooks (_sbrk/_exit) needed by malloc().
 */
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include "semihost.h"

extern uint32_t __etext[], __data_start__[], __data_end__[];
extern uint32_t __bss_start__[], __bss_end__[];
extern uint32_t __stack_top__[], __heap_start__[], __heap_limit__[];
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

extern int main(int argc, char **argv);

void Reset_Handler(void);

#define SCB_CPACR  (*(volatile uint32_t *)0xE000ED88)
#define SCB_CCR    (*(volatile uint32_t *)0xE000ED14)
#define SCB_CFSR   (*(volatile uint32_t *)0xE000ED28)
#define SCB_HFSR   (*(volatile uint32_t *)0xE000ED2C)
#define SCB_BFAR   (*(volatile uint32_t *)0xE000ED38)
#define SCB_SFSR   (*(volatile uint32_t *)0xE000EDE4)

static void __attribute__((noreturn)) fault(const char *what)
{
    sh_puts("\n*** ");
    sh_puts(what);
    sh_puts(": CFSR=0x"); sh_put_hex(SCB_CFSR);
    sh_puts(" HFSR=0x");  sh_put_hex(SCB_HFSR);
    sh_puts(" BFAR=0x");  sh_put_hex(SCB_BFAR);
    sh_puts(" SFSR=0x");  sh_put_hex(SCB_SFSR);
    sh_puts("\n");
    sh_exit(3);
}

static void HardFault_Handler(void)   { fault("HardFault"); }
static void MemManage_Handler(void)   { fault("MemManage"); }
static void BusFault_Handler(void)    { fault("BusFault"); }
static void UsageFault_Handler(void)  { fault("UsageFault"); }
static void SecureFault_Handler(void) { fault("SecureFault"); }
static void Default_Handler(void)     { fault("Unexpected exception"); }

__attribute__((section(".isr_vector"), used))
const void *const vector_table[16] = {
    __stack_top__,
    Reset_Handler,
    Default_Handler,        /* NMI */
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    SecureFault_Handler,
    0, 0, 0,
    Default_Handler,        /* SVCall */
    Default_Handler,        /* DebugMonitor */
    0,
    Default_Handler,        /* PendSV */
    Default_Handler,        /* SysTick */
};

static char cmdline[256];
static char *argv_buf[16];

/* stack high-water mark: the stack is filled with a pattern at start-up and
 * scanned at exit, see sh_report_memory() */
#define STACK_FILL 0xA5A5A5A5u
extern uint32_t __stack_limit__[];
static char *heap_high;

static void fill_stack(void)
{
    uint32_t sp;
    uint32_t *p;
    __asm__ volatile("mov %0, sp" : "=r"(sp));
    for (p = __stack_limit__; (uint32_t)p < sp - 64; p++)
        *p = STACK_FILL;
}

void sh_report_memory(void)
{
    uint32_t *p = __stack_limit__;
    while (p < __stack_top__ && *p == STACK_FILL)
        p++;
    sh_puts("stack used: ");
    sh_put_uint((uint32_t)((char *)__stack_top__ - (char *)p));
    sh_puts(" bytes, heap used: ");
    sh_put_uint((uint32_t)(heap_high - (char *)__heap_start__));
    sh_puts(" bytes\n");
}

void Reset_Handler(void)
{
    uint32_t *src, *dst;
    int argc = 0;

    /* Full access to CP10/CP11 (FPU), in case the toolchain uses it. */
    SCB_CPACR |= (0xFu << 20);
    __asm__ volatile("dsb\n isb" ::: "memory");

    fill_stack();

    for (src = __etext, dst = __data_start__; dst < __data_end__; )
        *dst++ = *src++;
    for (dst = __bss_start__; dst < __bss_end__; )
        *dst++ = 0;

    for (void (**f)(void) = __init_array_start; f < __init_array_end; f++)
        (*f)();

    /* Command line from the host: "-semihosting-config ...,arg=file" */
    if (sh_get_cmdline(cmdline, sizeof(cmdline)) == 0) {
        char *p = cmdline;
        while (*p && argc < 15) {
            while (*p == ' ') p++;
            if (!*p) break;
            argv_buf[argc++] = p;
            while (*p && *p != ' ') p++;
            if (*p) *p++ = 0;
        }
    }
    argv_buf[argc] = 0;

    sh_exit(main(argc, argv_buf));
}

/* newlib hooks */
static char *heap_end;

void *_sbrk(ptrdiff_t incr)
{
    char *prev;
    if (!heap_end)
        heap_end = (char *)__heap_start__;
    prev = heap_end;
    if (heap_end + incr > (char *)__heap_limit__) {
        errno = ENOMEM;
        return (void *)-1;
    }
    heap_end += incr;
    if (heap_end > heap_high)
        heap_high = heap_end;
    return prev;
}

void _exit(int code)
{
    sh_exit(code);
}

int _write(int fd, const void *buf, size_t len)
{
    (void)fd;
    return sh_write(2, buf, len);
}
int _close(int fd) { (void)fd; return -1; }
int _read(int fd, void *buf, size_t len) { (void)fd; (void)buf; (void)len; return -1; }
long _lseek(int fd, long off, int whence) { (void)fd; (void)off; (void)whence; return -1; }
int _fstat(int fd, void *st) { (void)fd; (void)st; return -1; }
int _isatty(int fd) { (void)fd; return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
