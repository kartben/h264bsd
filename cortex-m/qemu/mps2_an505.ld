/*
 * Linker script for the QEMU "mps2-an505" board (Arm Cortex-M33, SSE-200).
 *
 * The CPU boots in Secure state with the vector table at 0x10000000, which
 * is the Secure alias of the 4 MB code SRAM (SSRAM1). The 16 MB PSRAM at
 * 0x80000000 is used for .data/.bss, the heap (decoded picture buffers)
 * and the stack. This is only a test/benchmark environment; a real product
 * would use the SoC's own linker script (e.g. Zephyr's).
 */
MEMORY
{
    CODE  (rx)  : ORIGIN = 0x10000000, LENGTH = 4M
    PSRAM (rwx) : ORIGIN = 0x80000000, LENGTH = 16M
}

ENTRY(Reset_Handler)

SECTIONS
{
    .text :
    {
        KEEP(*(.isr_vector))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        __preinit_array_start = .;
        KEEP(*(.preinit_array))
        __preinit_array_end = .;
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
        . = ALIGN(4);
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
        . = ALIGN(4);
        __etext = .;
    } > CODE

    .data : AT (__etext)
    {
        . = ALIGN(8);
        __data_start__ = .;
        *(.data*)
        . = ALIGN(8);
        __data_end__ = .;
    } > PSRAM

    .bss (NOLOAD) :
    {
        . = ALIGN(8);
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end__ = .;
    } > PSRAM

    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        __heap_start__ = .;
        end = .;
        _end = .;
    } > PSRAM

    __stack_size__ = 64K;
    __stack_top__  = ORIGIN(PSRAM) + LENGTH(PSRAM);
    __stack_limit__ = __stack_top__ - __stack_size__;
    __heap_limit__ = __stack_limit__;

    /DISCARD/ : { *(.ARM.attributes) }
}
