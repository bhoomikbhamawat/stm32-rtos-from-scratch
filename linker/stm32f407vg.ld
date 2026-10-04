/*
 * stm32f407vg.ld - Linker script for STM32F407VG (1 MB flash, 128 KB SRAM, 64 KB CCM)
 *
 * Memory map (RM0090 section 2.3):
 *   FLASH  0x0800_0000  1024 KB  code + read-only data + .data load image
 *   SRAM   0x2000_0000   128 KB  .data, .bss, task stacks, main stack (MSP)
 *   CCM    0x1000_0000    64 KB  core-coupled RAM: CPU-only (no DMA), zero wait state
 *
 * Interview note: know the difference between VMA (where a section runs) and
 * LMA (where it is stored). .data has its VMA in SRAM but its LMA in flash;
 * the startup code copies it across before main().
 */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    RAM    (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
    CCMRAM (rw)  : ORIGIN = 0x10000000, LENGTH = 64K
}

/* Main stack (MSP) lives at the top of SRAM. After the RTOS starts, only
 * exception handlers use the MSP; tasks run on their own stacks (PSP). */
_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Stack_Size = 0x1000;   /* 4 KB reserved for MSP */

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.glue_7)
        *(.glue_7t)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata*)
        . = ALIGN(4);
    } > FLASH

    .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
    .ARM.exidx :
    {
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > FLASH

    /* Initialised data: runs from RAM, stored in FLASH */
    _sidata = LOADADDR(.data);
    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH

    /* Zero-initialised data */
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        __bss_start__ = _sbss;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = _ebss;
    } > RAM

    /* Not touched by startup code: survives a warm reset.
     * The HardFault handler writes its crash record here so the next boot
     * can report what happened. */
    .noinit (NOLOAD) :
    {
        . = ALIGN(4);
        *(.noinit)
        *(.noinit*)
        . = ALIGN(4);
    } > RAM

    /* Benchmark sample buffer lives in CCM so it doesn't eat main SRAM.
     * NOLOAD: contents are undefined at boot; the code clears what it uses. */
    .ccmram (NOLOAD) :
    {
        . = ALIGN(4);
        *(.ccmram)
        *(.ccmram*)
        . = ALIGN(4);
    } > CCMRAM

    /* newlib's sbrk (if ever pulled in) needs these symbols */
    . = ALIGN(8);
    end = .;
    _end = .;

    /* Fail the link if .data + .bss + MSP no longer fit in SRAM */
    ._msp_check (NOLOAD) :
    {
        . = ALIGN(8);
        . = . + _Min_Stack_Size;
        . = ALIGN(8);
    } > RAM

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
