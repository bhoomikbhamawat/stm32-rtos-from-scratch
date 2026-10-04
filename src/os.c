/*
 * os.c - Kernel core: task creation, scheduler, tick, delays, start-up.
 */
#include "os.h"
#include "stm32f407.h"
#include "clock.h"

#define DBGMCU_CR       (*(volatile uint32_t *)0xE0042004UL)
#define DBGMCU_DBG_SLEEP (1UL << 0)   /* keep debug alive during WFI */
#define SCB_ICSR_PENDSTCLR (1UL << 25)

static tcb_t    s_tasks[OS_MAX_TASKS];
static uint32_t s_task_count;
static uint32_t s_ready_bitmap;                 /* bit p set: a READY task at prio p */
static tcb_t   *s_ready_head[OS_MAX_PRIO];      /* circular list per priority        */
static volatile uint32_t s_tick;
static uint32_t s_idle_stack[OS_IDLE_STACK_WORDS] __attribute__((aligned(8)));

tcb_t *volatile os_curr;
volatile uint32_t os_started;          /* set to 1 by SVC_Handler (os_port.s) */

static void os_task_exit(void);

/* ------------------------------------------------------------------------- */
void os_init(void)
{
    for (uint32_t i = 0; i < OS_MAX_TASKS; i++) {
        s_tasks[i].state = TASK_UNUSED;
    }
    for (uint32_t p = 0; p < OS_MAX_PRIO; p++) {
        s_ready_head[p] = NULL;
    }
    s_ready_bitmap = 0;
    s_task_count = 0;
    s_tick       = 0;
    os_curr      = NULL;
    os_started   = 0;
}

/*
 * Build a fake exception frame so the first PendSV/SVC "return" into the task
 * looks exactly like returning from an interrupt that preempted it.
 *
 *   high addr  xPSR  = 0x0100_0000 (Thumb bit; must be set or the core faults)
 *              PC    = entry (bit0 cleared: frame PCs are halfword addresses)
 *              LR    = os_task_exit (where a task lands if it returns)
 *              R12, R3, R2, R1
 *              R0    = arg         <- hardware-stacked frame ends here
 *              EXC_RETURN = 0xFFFF_FFFD (Thread mode, PSP, no FPU frame)
 *              R11 .. R4           <- software-saved by PendSV
 *   low addr   (tcb->sp points at R4)
 */
int os_task_create(void (*entry)(void *), void *arg, uint8_t prio,
                   uint32_t *stack, uint32_t stack_words, const char *name)
{
    if (entry == NULL || stack == NULL || stack_words < 64U || prio >= OS_MAX_PRIO) {
        return OS_ERR_PARAM;
    }

    uint32_t pm = os_enter_critical();
    if (s_task_count >= OS_MAX_TASKS) {
        os_exit_critical(pm);
        return OS_ERR_FULL;
    }
    tcb_t *t = &s_tasks[s_task_count];
    t->id = (uint8_t)s_task_count;
    s_task_count++;
    os_exit_critical(pm);

    /* AAPCS: stack must be 8-byte aligned at any public interface */
    uint32_t *sp = (uint32_t *)((uintptr_t)(stack + stack_words) & ~(uintptr_t)7U);

    *--sp = 0x01000000UL;                          /* xPSR */
    *--sp = (uint32_t)(uintptr_t)entry & ~1UL;     /* PC   */
    *--sp = (uint32_t)(uintptr_t)os_task_exit;     /* LR   */
    *--sp = 0x12121212UL;                          /* R12  */
    *--sp = 0x03030303UL;                          /* R3   */
    *--sp = 0x02020202UL;                          /* R2   */
    *--sp = 0x01010101UL;                          /* R1   */
    *--sp = (uint32_t)(uintptr_t)arg;              /* R0   */
    *--sp = 0xFFFFFFFDUL;                          /* EXC_RETURN */
    for (uint32_t r = 11; r >= 4; r--) {
        *--sp = 0x01010101UL * r;                  /* R11..R4: recognisable pattern */
    }

    t->sp              = sp;
    t->prio            = prio;
    t->base_prio       = prio;
    t->wake_tick       = 0;
    t->wait_obj        = NULL;
    t->wake_reason     = WAKE_NONE;
    t->has_timeout     = 0;
    t->name            = name;
    t->stack_base      = stack;
    t->stack_words     = stack_words;
    t->switch_in_count = 0;
    t->next            = NULL;
    t->prev            = NULL;

    pm = os_enter_critical();
    os_task_set_state(t, TASK_READY);  /* last: makes the task visible */
    os_exit_critical(pm);
    return OS_OK;
}

/* ------------------------------------------------------------------------- */
/* Ready lists: one circular doubly-linked list per priority + a bitmap      */
/* ------------------------------------------------------------------------- */
static void rq_insert(tcb_t *t)          /* append at the tail of its level */
{
    uint32_t p = t->prio;
    tcb_t *h = s_ready_head[p];
    if (h == NULL) {
        t->next = t;
        t->prev = t;
        s_ready_head[p] = t;
        s_ready_bitmap |= (1UL << p);
    } else {
        t->next       = h;
        t->prev       = h->prev;
        h->prev->next = t;
        h->prev       = t;
    }
}

static void rq_remove(tcb_t *t)
{
    uint32_t p = t->prio;
    if (t->next == t) {                   /* last task at this level */
        s_ready_head[p] = NULL;
        s_ready_bitmap &= ~(1UL << p);
    } else {
        t->prev->next = t->next;
        t->next->prev = t->prev;
        if (s_ready_head[p] == t) {
            s_ready_head[p] = t->next;
        }
    }
    t->next = NULL;
    t->prev = NULL;
}

void os_task_set_state(tcb_t *t, task_state_t state)
{
    if (t->state == (uint8_t)state) {
        return;
    }
    if (t->state == TASK_READY) {
        rq_remove(t);
    }
    t->state = (uint8_t)state;
    if (state == TASK_READY) {
        rq_insert(t);
    }
}

void os_task_set_prio(tcb_t *t, uint8_t prio)
{
    if (t->prio == prio) {
        return;
    }
    if (t->state == TASK_READY) {
        rq_remove(t);
        t->prio = prio;
        rq_insert(t);
    } else {
        t->prio = prio;
    }
}

static inline uint32_t highest_ready_prio(void)
{
    /* CLZ = count leading zeros: a single Cortex-M4 instruction */
    return 31U - (uint32_t)__builtin_clz(s_ready_bitmap);
}

/* ------------------------------------------------------------------------- */
#if OS_SCHED_O1
/*
 * O(1) scheduler. Runs inside PendSV with interrupts masked.
 *
 * s_ready_head[p] is the task most recently chosen at level p. If that is the
 * task being switched out and others are ready at the same level, advance to
 * the next one (round-robin on yield / time slice). If a higher-priority task
 * had preempted it instead, the head is still the preempted task, so it
 * resumes first when the higher-priority task blocks.
 * Cost is constant: one CLZ, a few loads - independent of the task count.
 */
tcb_t *os_sched_select(void)
{
    uint32_t p = highest_ready_prio();   /* idle keeps the bitmap non-zero */
    tcb_t   *t = s_ready_head[p];
    if (t == os_curr && t->next != t) {
        t = t->next;
    }
    s_ready_head[p] = t;
    if (t != os_curr) {
        t->switch_in_count++;
    }
    return t;
}
#else
/*
 * Linear scheduler (original version, kept to benchmark against): scan all
 * TCBs starting just after the current task; only a STRICTLY higher priority
 * replaces the best candidate, so equal priorities rotate. O(n).
 */
tcb_t *os_sched_select(void)
{
    uint32_t n     = s_task_count;
    uint32_t start = (os_curr != NULL) ? (uint32_t)os_curr->id + 1U : 0U;
    tcb_t   *best  = NULL;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = start + i;
        if (idx >= n) {
            idx -= n;
        }
        tcb_t *t = &s_tasks[idx];
        if (t->state == TASK_READY && (best == NULL || t->prio > best->prio)) {
            best = t;
        }
    }
    if (best != os_curr) {
        best->switch_in_count++;
    }
    return best;
}
#endif

void os_request_switch(void)
{
    SCB->ICSR = SCB_ICSR_PENDSVSET;
    __dsb();
    __isb();   /* with IRQs enabled, PendSV is taken right here */
}

void os_yield(void)
{
    os_request_switch();
}

/* ------------------------------------------------------------------------- */
uint32_t os_ticks(void)
{
    return s_tick;
}

void os_delay(uint32_t ticks)
{
    if (ticks == 0U) {
        os_yield();
        return;
    }
    uint32_t pm = os_enter_critical();
    os_curr->wake_tick = s_tick + ticks;
    os_task_set_state(os_curr, TASK_SLEEPING);
    os_request_switch();            /* pended; taken when IRQs re-enabled */
    os_exit_critical(pm);
}

/* Fixed-rate periodic wake-up with no cumulative drift (unlike os_delay). */
void os_delay_until(uint32_t *last_wake, uint32_t period)
{
    uint32_t pm   = os_enter_critical();
    uint32_t next = *last_wake + period;
    *last_wake    = next;
    if ((int32_t)(next - s_tick) > 0) {
        os_curr->wake_tick = next;
        os_task_set_state(os_curr, TASK_SLEEPING);
        os_request_switch();
    }
    /* else: we overran the period; return immediately to catch up */
    os_exit_critical(pm);
}

void os_tick_suspend(void)
{
    SYSTICK->CTRL &= ~SYSTICK_CTRL_ENABLE;
    SCB->ICSR = SCB_ICSR_PENDSTCLR;          /* drop a tick that is already pending */
}

void os_tick_resume(void)
{
    SYSTICK->CTRL |= SYSTICK_CTRL_ENABLE;
}

/* ------------------------------------------------------------------------- */
/* Blocking helpers used by os_sync.c (call with interrupts disabled) */
void os_block_current(const void *obj, uint32_t timeout_ticks)
{
    os_task_set_state(os_curr, TASK_BLOCKED);
    os_curr->wait_obj    = obj;
    os_curr->wake_reason = WAKE_NONE;
    os_curr->has_timeout = (timeout_ticks != OS_WAIT_FOREVER) ? 1U : 0U;
    os_curr->wake_tick   = s_tick + timeout_ticks;
    os_request_switch();
}

tcb_t *os_highest_waiter(const void *obj)
{
    tcb_t *best = NULL;
    for (uint32_t i = 0; i < s_task_count; i++) {
        tcb_t *t = &s_tasks[i];
        if (t->state == TASK_BLOCKED && t->wait_obj == obj &&
            (best == NULL || t->prio > best->prio)) {
            best = t;
        }
    }
    return best;
}

/* ------------------------------------------------------------------------- */
/*
 * 1 kHz tick: advance time, wake sleepers / expire timeouts, and request a
 * switch if a ready task should run instead of (or alongside, round-robin)
 * the current one. The switch itself is deferred to PendSV.
 */
void SysTick_Handler(void)
{
    if (!os_started) {
        return;
    }

    uint32_t now = ++s_tick;

    for (uint32_t i = 0; i < s_task_count; i++) {
        tcb_t *t = &s_tasks[i];
        if ((t->state == TASK_SLEEPING ||
             (t->state == TASK_BLOCKED && t->has_timeout)) &&
            (int32_t)(now - t->wake_tick) >= 0) {
            if (t->state == TASK_BLOCKED) {
                t->wake_reason = WAKE_TIMEOUT;
                t->wait_obj    = NULL;
            }
            os_task_set_state(t, TASK_READY);
        }
    }

    /* Switch if a higher priority is ready, or another task shares the
     * current level (time slice). Both answered from the bitmap in O(1). */
    uint32_t top = highest_ready_prio();
    if (top > os_curr->prio ||
        (top == os_curr->prio && s_ready_head[top]->next != s_ready_head[top])) {
        SCB->ICSR = SCB_ICSR_PENDSVSET;
    }
}

/* ------------------------------------------------------------------------- */
static void os_task_exit(void)
{
    /* A task function returned: retire its TCB and never come back */
    uint32_t pm = os_enter_critical();
    os_task_set_state(os_curr, TASK_UNUSED);
    os_request_switch();
    os_exit_critical(pm);
    for (;;) { }
}

static void idle_task(void *arg)
{
    (void)arg;
    for (;;) {
        __wfi();   /* sleep until the next interrupt */
    }
}

void os_start(void)
{
    (void)os_task_create(idle_task, NULL, 0, s_idle_stack, OS_IDLE_STACK_WORDS, "idle");

    __disable_irq();

    DBGMCU_CR |= DBGMCU_DBG_SLEEP;

    SCB->SHP[SHP_IDX_PENDSV]  = OS_PRIO_PENDSV;
    SCB->SHP[SHP_IDX_SYSTICK] = OS_PRIO_SYSTICK;
    SCB->SHP[SHP_IDX_SVCALL]  = OS_PRIO_SVC;

    os_curr = os_sched_select();      /* highest-priority task runs first */

    SYSTICK->LOAD = (SystemCoreClock / OS_TICK_HZ) - 1U;
    SYSTICK->VAL  = 0;
    SYSTICK->CTRL = SYSTICK_CTRL_CLKSOURCE | SYSTICK_CTRL_TICKINT | SYSTICK_CTRL_ENABLE;

    /*
     * - Reset MSP to the top of RAM: main()'s stack is never needed again and
     *   handlers get the full MSP region.
     * - CONTROL = 0 clears FPCA, so SVC stacks a basic (non-FPU) frame and no
     *   lazy FP state is left pointing into the abandoned main stack.
     * - SVC with PRIMASK set would escalate to HardFault, so enable IRQs first.
     *   SysTick ignores ticks until SVC_Handler sets os_started.
     */
    __asm volatile (
        "ldr   r0, =_estack     \n"
        "msr   msp, r0          \n"
        "movs  r0, #0           \n"
        "msr   control, r0      \n"
        "isb                    \n"
        "cpsie i                \n"
        "cpsie f                \n"
        "dsb                    \n"
        "isb                    \n"
        "svc   #0               \n"
        "nop                    \n"
        ::: "r0", "memory");

    for (;;) { }   /* never reached */
}
