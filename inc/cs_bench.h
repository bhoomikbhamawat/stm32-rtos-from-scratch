/*
 * cs_bench.h - DWT-based context-switch latency benchmark.
 */
#ifndef CS_BENCH_H
#define CS_BENCH_H

#include <stdint.h>

#ifndef CS_BENCH_SAMPLES
#define CS_BENCH_SAMPLES       10000U
#endif
#ifndef CS_BENCH_FPU
#define CS_BENCH_FPU           0      /* 1: both tasks hold FPU context (extended frames) */
#endif
#ifndef CS_BENCH_LOAD
#define CS_BENCH_LOAD          0      /* 1: SysTick keeps running + TIM2 ISR load */
#endif
#ifndef CS_BENCH_LOAD_HZ
#define CS_BENCH_LOAD_HZ       10000U /* TIM2 interrupt rate in load mode */
#endif
#define CS_BENCH_START_DELAY_MS 100U  /* run early, before the IMU task starts */
#define CS_BENCH_PRIO          4U     /* above every application task */

typedef struct {
    uint32_t done;         /* 1 when all samples are collected                  */
    uint32_t dwt_ok;       /* 0 if CYCCNT is missing / not counting             */
    uint32_t count;        /* samples recorded                                  */
    uint32_t min;          /* cycles                                            */
    uint32_t max;          /* cycles  -> YY (worst case)                        */
    uint32_t avg;          /* cycles, rounded -> XX (average)                   */
    uint32_t avg_x100;     /* average * 100, for two decimals                   */
    uint32_t max_index;    /* which sample was the worst                        */
    uint32_t calib;        /* cost of one CYCCNT read, already subtracted       */
    uint32_t cpu_hz;
    uint32_t avg_ns;
    uint32_t max_ns;
    uint32_t fpu_frames;   /* CS_BENCH_FPU at build time                        */
    uint32_t sched_o1;     /* 1 = bitmap/CLZ scheduler, 0 = linear scan         */
    uint32_t load_mode;    /* CS_BENCH_LOAD at build time                       */
    uint32_t load_hz;      /* TIM2 interrupt rate (load mode)                   */
    uint32_t load_irqs;    /* TIM2 interrupts taken during the run              */
    uint32_t ticks;        /* SysTick interrupts during the run                 */
    uint32_t skipped;      /* resumes after an involuntary (tick) switch: not a sample */
    uint64_t sum;
} cs_stats_t;

extern volatile cs_stats_t g_cs;
extern uint16_t g_cs_samples[CS_BENCH_SAMPLES];

void cs_bench_create_tasks(void);

/* Empty, never-inlined function: GDB sets a breakpoint here to know the
 * benchmark has finished (see tools/cs_bench.gdb). */
void cs_bench_complete(void);

#endif
