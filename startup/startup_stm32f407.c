/*
 * startup_stm32f407.c - Vector table and Reset_Handler, written in C.
 *
 * What happens at power-on (interview favourite: "what runs before main()?"):
 *   1. The core reads word 0 of the vector table (0x0800_0000, aliased at 0x0)
 *      and loads it into MSP.
 *   2. It reads word 1 (Reset_Handler address, Thumb bit set) and jumps there.
 *   3. Reset_Handler enables the FPU, copies .data from flash to RAM, zeroes
 *      .bss, sets up the clock tree, then calls main().
 */
#include <stdint.h>
#include "stm32f407.h"
#include "clock.h"

/* Symbols from the linker script */
extern uint32_t _estack;
extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss;

int main(void);

void Reset_Handler(void);
void Default_Handler(void);

/* Core exception handlers. `weak` + `alias` lets any other file override a
 * handler simply by defining a function with the same name. */
void NMI_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)  __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void) __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)        __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)   __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)     __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)    __attribute__((weak, alias("Default_Handler")));
void TIM2_IRQHandler(void)    __attribute__((weak, alias("Default_Handler")));

#define NUM_IRQS 82   /* STM32F407 has 82 maskable interrupt lines */

typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used))
const vector_t g_vector_table[16 + NUM_IRQS] = {
    (vector_t)(uintptr_t)&_estack,  /*  0: initial MSP           */
    Reset_Handler,                  /*  1: reset                 */
    NMI_Handler,                    /*  2                        */
    HardFault_Handler,              /*  3                        */
    MemManage_Handler,              /*  4                        */
    BusFault_Handler,               /*  5                        */
    UsageFault_Handler,             /*  6                        */
    0, 0, 0, 0,                     /*  7-10: reserved           */
    SVC_Handler,                    /* 11: used to start 1st task*/
    DebugMon_Handler,               /* 12                        */
    0,                              /* 13: reserved              */
    PendSV_Handler,                 /* 14: context switch        */
    SysTick_Handler,                /* 15: RTOS tick             */
    /* 16..97: peripheral IRQs (this project polls I2C, so almost all default) */
    [16 ... (16 + 27)]            = Default_Handler,
    [16 + 28]                     = TIM2_IRQHandler,   /* benchmark load generator */
    [16 + 29 ... (16 + NUM_IRQS - 1)] = Default_Handler,
};

void Reset_Handler(void)
{
    /* 1. Enable the FPU (CP10 + CP11 full access) BEFORE any C code that might
     *    emit floating-point instructions. With -mfloat-abi=hard the compiler
     *    is free to use FPU registers anywhere, and touching them with the
     *    FPU disabled raises a UsageFault (NOCP). */
    SCB_CPACR |= (0xFUL << 20);
    __dsb();
    __isb();

    /* 2. Copy .data initial values from flash to RAM */
    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata; ) {
        *dst++ = *src++;
    }

    /* 3. Zero .bss */
    for (uint32_t *dst = &_sbss; dst < &_ebss; ) {
        *dst++ = 0;
    }

    /* 4. Clock tree: HSE 8 MHz -> PLL -> 168 MHz SYSCLK */
    clock_init_168mhz();

    (void)main();

    for (;;) { }   /* main() must never return on bare metal */
}

void Default_Handler(void)
{
    /* An interrupt fired that nobody handles. Read IPSR in the debugger
     * (`p/x $xpsr & 0x1ff`) to see which exception number it was. */
    for (;;) { }
}
