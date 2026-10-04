/*
 * clock.h - System clock configuration (register level).
 */
#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

/* Core clock in Hz after clock_init_168mhz(). 16 MHz (HSI) if HSE failed. */
extern uint32_t SystemCoreClock;
/* APB1 clock (feeds I2C1). */
extern uint32_t g_pclk1_hz;
/* 1 if the external crystal did not start and we stayed on the 16 MHz HSI. */
extern volatile uint32_t g_clock_hse_failed;

void clock_init_168mhz(void);

#endif
