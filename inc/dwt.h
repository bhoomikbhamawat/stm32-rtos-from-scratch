/*
 * dwt.h - DWT cycle counter (CYCCNT): a free-running 32-bit counter that
 * increments once per CPU clock. At 168 MHz it wraps every ~25.6 s.
 *
 * Why it's the right tool for context-switch profiling:
 *   - 1-cycle resolution (5.95 ns at 168 MHz), no peripheral timer needed
 *   - reading it is a single LDR from the PPB, so measurement overhead is tiny
 *     and can be calibrated out (see cs_bench.c)
 *   - unsigned subtraction (t1 - t0) is correct across one wrap-around
 */
#ifndef DWT_H
#define DWT_H

#include <stdint.h>
#include "stm32f407.h"

/* Returns 1 if the cycle counter exists and is running. */
static inline int dwt_init(void)
{
    COREDEBUG_DEMCR |= DEMCR_TRCENA;   /* power up DWT/ITM */
    if (DWT_CTRL & DWT_CTRL_NOCYCCNT) {
        return 0;                       /* implementation without CYCCNT */
    }
    DWT_CYCCNT = 0;
    DWT_CTRL  |= DWT_CTRL_CYCCNTENA;
    return 1;
}

static inline uint32_t dwt_cycles(void)
{
    return DWT_CYCCNT;
}

#endif
