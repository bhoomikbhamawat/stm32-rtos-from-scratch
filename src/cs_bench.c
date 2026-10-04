/*
 * cs_bench.c - Measures task-to-task context-switch latency with the DWT
 *              cycle counter, over CS_BENCH_SAMPLES switches.
 *
 * Method ("ping-pong"):
 *   Two tasks, A and B, at the same top priority, take turns:
 *
 *       A: t0 = CYCCNT; os_yield();  ──────────►  B: t1 = CYCCNT (resumes)
 *                                                 record(t1 - t0 - calib)
 *                                                 t0 = CYCCNT; os_yield();
 *       A: t1 = CYCCNT (resumes)     ◄──────────
 *          record(t1 - t0 - calib) ...
 *
 *   Each sample therefore covers the FULL switch as a task experiences it:
 *     os_yield() setting PENDSVSET + DSB/ISB
 *     -> exception entry (hardware stacks 8 words)
 *     -> PendSV: save R4-R11/EXC_RETURN, run the scheduler, restore
 *     -> exception return (hardware unstacks) -> back in the other task.
 *   `calib` = cost of one CYCCNT read, measured at start-up and subtracted.
 *
 * Isolated mode (default): SysTick is paused for the run (os_tick_suspend),
 * and both tasks sit above every other task, so no tick or other task can
 * land inside a measured window. Result: the cost of the switch path itself.
 *
 * Load mode (CS_BENCH_LOAD=1): SysTick keeps running (1 kHz, scans tasks and
 * time-slices A/B) and TIM2 fires an extra interrupt at CS_BENCH_LOAD_HZ.
 * Interrupts that arrive during a switch are held off by PendSV's CPSID and
 * then tail-chain in before the task resumes, so they show up in the worst
 * case. Because ticks can also switch A/B involuntarily, each task records
 * WHO yielded last (s_yielder) and only counts a resume that followed a
 * yield by the other task.
 *
 * Results land in g_cs (min / avg / max) and the raw samples in
 * g_cs_samples[] for percentile analysis on the host (tools/analyze_cs.py).
 */
#include "cs_bench.h"
#include "os.h"
#include "dwt.h"
#include "clock.h"
#include "stm32f407.h"

volatile cs_stats_t g_cs;
__attribute__((section(".ccmram"))) uint16_t g_cs_samples[CS_BENCH_SAMPLES];

static uint32_t s_stack_a[256] __attribute__((aligned(8)));
static uint32_t s_stack_b[256] __attribute__((aligned(8)));

static volatile uint32_t s_t_yield;   /* timestamp written just before yielding */
static volatile uint32_t s_arrived;
static volatile uint32_t s_armed;
static volatile uint32_t s_finished;
#if CS_BENCH_LOAD
static volatile uint32_t s_yielder;   /* id of the task that wrote s_t_yield */
static uint32_t          s_tick0;
#endif

/* ---------------- load generator (TIM2 update interrupt) ---------------- */
#if CS_BENCH_LOAD
void TIM2_IRQHandler(void)
{
    /* Clear the flag FIRST: the write takes a few cycles to reach the
     * peripheral; clearing it as the last instruction can make the ISR
     * re-enter immediately (classic STM32 gotcha). */
    TIM2->SR = ~TIM_SR_UIF;
    (void)TIM2->SR;   /* read-back: forces the write through the APB1 bridge
                       * before exception return. Without it this short ISR
                       * can exit while UIF is still set and fire again
                       * (seen on hardware: ~157 IRQs counted in ~9 ms at 10 kHz). */
    g_cs.load_irqs++;
}

static void load_start(void)
{
    /* APB1 prescaler != 1 -> timer clock = 2 x PCLK1 (84 MHz at 168 MHz SYSCLK) */
    uint32_t timclk = (g_pclk1_hz == SystemCoreClock) ? g_pclk1_hz : 2U * g_pclk1_hz;
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    (void)RCC->APB1ENR;
    TIM2->CR1  = 0;
    TIM2->PSC  = 0;
    TIM2->ARR  = (timclk / CS_BENCH_LOAD_HZ) - 1U;
    TIM2->CNT  = 0;
    TIM2->EGR  = TIM_EGR_UG;          /* load PSC/ARR now */
    TIM2->SR   = 0;
    TIM2->DIER = TIM_DIER_UIE;
    NVIC_IPR(TIM2_IRQn)   = 0xD0U;    /* above SysTick (0xE0) and PendSV (0xF0) */
    NVIC_ICPR(0)          = (1UL << TIM2_IRQn);
    NVIC_ISER(0)          = (1UL << TIM2_IRQn);
    TIM2->CR1  = TIM_CR1_CEN;
}

static void load_stop(void)
{
    TIM2->CR1  = 0;
    TIM2->DIER = 0;
    NVIC_ICER(0) = (1UL << TIM2_IRQn);
}
#endif

void __attribute__((noinline)) cs_bench_complete(void)
{
    __asm volatile ("" ::: "memory");   /* GDB: break cs_bench_complete */
}

static uint32_t measure_read_overhead(void)
{
    uint32_t best = 0xFFFFFFFFUL;
    for (uint32_t i = 0; i < 32U; i++) {
        uint32_t a = dwt_cycles();
        uint32_t b = dwt_cycles();
        if ((b - a) < best) {
            best = b - a;
        }
    }
    return best;
}

static void record(uint32_t delta)
{
    if (g_cs.count >= CS_BENCH_SAMPLES) {
        return;
    }
    uint32_t c = (delta > g_cs.calib) ? (delta - g_cs.calib) : 0U;
    g_cs_samples[g_cs.count] = (c > 0xFFFFU) ? 0xFFFFU : (uint16_t)c;
    g_cs.sum += c;
    if (c < g_cs.min) {
        g_cs.min = c;
    }
    if (c > g_cs.max) {
        g_cs.max       = c;
        g_cs.max_index = g_cs.count;
    }
    g_cs.count++;
}

static void finalize(void)
{
    uint32_t n = (g_cs.count != 0U) ? g_cs.count : 1U;
    g_cs.avg_x100 = (uint32_t)((g_cs.sum * 100ULL + n / 2U) / n);
    g_cs.avg      = (g_cs.avg_x100 + 50U) / 100U;
    g_cs.cpu_hz   = SystemCoreClock;
    g_cs.avg_ns   = (uint32_t)(((uint64_t)g_cs.avg_x100 * 10000000ULL) / SystemCoreClock);
    g_cs.max_ns   = (uint32_t)(((uint64_t)g_cs.max * 1000000000ULL) / SystemCoreClock);
    g_cs.done     = 1;
}

static void bench_task(void *arg)
{
#if CS_BENCH_LOAD
    const uint32_t me = (uint32_t)(uintptr_t)arg;
#else
    (void)arg;
#endif
#if CS_BENCH_FPU
    /* Touch the FPU once: CONTROL.FPCA = 1 for this task from now on, so every
     * switch in/out of it uses the extended frame (S0-S15 by hardware,
     * S16-S31 by PendSV). Compare with CS_BENCH_FPU=0 to see the FPU cost. */
    volatile float f = 1.5f;
    f = f * 2.25f;
    (void)f;
#endif

    os_delay(CS_BENCH_START_DELAY_MS);

    /* Barrier: the second task to arrive starts the run */
    uint32_t pm = os_enter_critical();
    if (++s_arrived == 2U) {
        g_cs.calib = measure_read_overhead();
#if CS_BENCH_LOAD
        s_yielder = 0xFFU;
        s_tick0   = os_ticks();
        load_start();
#else
        os_tick_suspend();
#endif
        s_armed = 1;
    }
    os_exit_critical(pm);

    while (!s_armed) {
        os_yield();                  /* hand over to the other task */
    }

    while (g_cs.count < CS_BENCH_SAMPLES) {
#if CS_BENCH_LOAD
        s_t_yield = dwt_cycles();
        s_yielder = me;              /* written AFTER the timestamp (see header) */
        os_yield();
        uint32_t t1  = dwt_cycles();
        uint32_t t0  = s_t_yield;
        uint32_t who = s_yielder;
        /* Two ways a resume is NOT a valid sample:
         *  - who == me: we were switched back in after a tick preempted the
         *    other task (involuntary), not after it yielded to us.
         *  - t0 newer than t1: a tick preempted us between reading t1 and
         *    reading s_t_yield, and the other task overwrote it meanwhile
         *    (seen on hardware as one 2^32-403 cycle sample). */
        if (who != me && (int32_t)(t1 - t0) >= 0) {
            record(t1 - t0);         /* the other task yielded to us */
        } else {
            g_cs.skipped++;
        }
#else
        s_t_yield = dwt_cycles();
        os_yield();                  /* <-- the measured context switch */
        uint32_t t1 = dwt_cycles();
        record(t1 - s_t_yield);      /* s_t_yield was written by the OTHER task */
#endif
    }

    pm = os_enter_critical();
    uint32_t first_to_finish = !s_finished;
    s_finished = 1;
    if (first_to_finish) {
#if CS_BENCH_LOAD
        load_stop();
        g_cs.ticks = os_ticks() - s_tick0;
#else
        os_tick_resume();
#endif
        finalize();
    }
    os_exit_critical(pm);

    if (first_to_finish) {
        cs_bench_complete();
    }
    /* returning retires this task (os_task_exit) */
}

void cs_bench_create_tasks(void)
{
    g_cs.done       = 0;
    g_cs.count      = 0;
    g_cs.sum        = 0;
    g_cs.min        = 0xFFFFFFFFUL;
    g_cs.max        = 0;
    g_cs.max_index  = 0;
    g_cs.fpu_frames = CS_BENCH_FPU;
    g_cs.sched_o1   = OS_SCHED_O1;
    g_cs.load_mode  = CS_BENCH_LOAD;
    g_cs.load_hz    = CS_BENCH_LOAD ? CS_BENCH_LOAD_HZ : 0U;
    g_cs.load_irqs  = 0;
    g_cs.ticks      = 0;
    g_cs.skipped    = 0;

    /* Is CYCCNT really counting? (it isn't in most emulators) */
    uint32_t a = dwt_cycles();
    for (volatile uint32_t i = 0; i < 10U; i++) { }
    g_cs.dwt_ok = (dwt_cycles() != a) ? 1U : 0U;

    (void)os_task_create(bench_task, (void *)0, CS_BENCH_PRIO, s_stack_a, 256, "bench_a");
    (void)os_task_create(bench_task, (void *)1, CS_BENCH_PRIO, s_stack_b, 256, "bench_b");
}
