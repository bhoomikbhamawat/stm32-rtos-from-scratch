/*
 * os.h - A small preemptive RTOS kernel for Cortex-M4.
 *
 * Design summary (be ready to explain each line in an interview):
 *   - Static task pool of OS_MAX_TASKS TCBs, no heap.
 *   - Fixed-priority preemptive scheduling, HIGHER number = HIGHER priority
 *     (0..31). Tasks of equal priority are round-robin time-sliced every tick.
 *   - Scheduler selectable at build time (OS_SCHED_O1):
 *       1 (default) O(1): a 32-bit ready bitmap (bit p = "some task at
 *         priority p is ready") + one circular ready list per priority.
 *         Highest priority = 31 - CLZ(bitmap): one instruction.
 *       0 linear scan over all TCBs, kept for before/after benchmarking.
 *   - SysTick (1 kHz) = time base: wakes sleeping tasks, handles timeouts,
 *     requests a context switch when needed.
 *   - PendSV (lowest exception priority) = the only place a context switch
 *     happens. It saves R4-R11 (+ S16-S31 if the task used the FPU) on the
 *     task's PSP stack, calls the scheduler, and restores the next task.
 *   - SVC #0 launches the very first task.
 *   - Counting semaphores (ISR-safe give) and mutexes with basic priority
 *     inheritance (task-only).
 */
#ifndef OS_H
#define OS_H

#include <stdint.h>
#include <stddef.h>

/* ---------------- configuration ---------------- */
#define OS_MAX_TASKS        8U
#define OS_MAX_PRIO         32U    /* priorities 0..31 (one bit each in the bitmap) */
#ifndef OS_SCHED_O1
#define OS_SCHED_O1         1
#endif
#define OS_TICK_HZ          1000U
#define OS_IDLE_STACK_WORDS 128U
#define OS_WAIT_FOREVER     0xFFFFFFFFUL

/* Exception priorities (STM32F4 implements the top 4 bits).
 * PendSV must be the LOWEST so a context switch never preempts an ISR. */
#define OS_PRIO_PENDSV      0xF0U
#define OS_PRIO_SYSTICK     0xE0U
#define OS_PRIO_SVC         0x00U

/* ---------------- return codes ---------------- */
#define OS_OK           0
#define OS_ERR_TIMEOUT  (-1)
#define OS_ERR_PARAM    (-2)
#define OS_ERR_FULL     (-3)
#define OS_ERR_OWNER    (-4)

/* ---------------- task control block ---------------- */
typedef enum {
    TASK_UNUSED = 0,
    TASK_READY,       /* runnable (includes the running task) */
    TASK_SLEEPING,    /* os_delay()                            */
    TASK_BLOCKED,     /* waiting on a semaphore / mutex        */
} task_state_t;

typedef enum {
    WAKE_NONE = 0,
    WAKE_SIGNAL,      /* object given/unlocked to us */
    WAKE_TIMEOUT,     /* timeout expired             */
} wake_reason_t;

typedef struct tcb {
    uint32_t   *sp;          /* MUST stay the first member: os_port.s uses offset 0 */
    uint8_t     prio;        /* effective priority (may be boosted by a mutex) */
    uint8_t     base_prio;   /* priority the task was created with             */
    uint8_t     state;       /* task_state_t                                   */
    uint8_t     id;
    uint8_t     wake_reason; /* wake_reason_t                                  */
    uint8_t     has_timeout;
    uint16_t    reserved;
    uint32_t    wake_tick;   /* absolute tick to wake / time out at            */
    const void *wait_obj;    /* object we are blocked on (NULL if none)        */
    const char *name;
    uint32_t   *stack_base;  /* lowest address of the stack (for debugging)    */
    uint32_t    stack_words;
    uint32_t    switch_in_count; /* times this task was switched in            */
    struct tcb *next;        /* ready-list links (circular, per priority)       */
    struct tcb *prev;
} tcb_t;

/* Running task. Read by PendSV in assembly, so it must be a plain global. */
extern tcb_t *volatile os_curr;

/* ---------------- kernel API ---------------- */
void     os_init(void);
int      os_task_create(void (*entry)(void *), void *arg, uint8_t prio,
                        uint32_t *stack, uint32_t stack_words, const char *name);
void     os_start(void) __attribute__((noreturn));

void     os_yield(void);
void     os_delay(uint32_t ticks);
void     os_delay_until(uint32_t *last_wake, uint32_t period);
uint32_t os_ticks(void);

/* Pause / resume the tick (used only by the context-switch benchmark so a
 * SysTick interrupt can't land inside a measured switch). */
void     os_tick_suspend(void);
void     os_tick_resume(void);

/* Critical sections: save PRIMASK, disable IRQs, restore on exit.
 * Nesting-safe because we restore the saved value instead of blindly
 * re-enabling. */
static inline uint32_t os_enter_critical(void)
{
    uint32_t primask;
    __asm volatile ("mrs %0, primask\n\tcpsid i" : "=r"(primask) :: "memory");
    return primask;
}

static inline void os_exit_critical(uint32_t primask)
{
    __asm volatile ("msr primask, %0" :: "r"(primask) : "memory");
}

/* ---------------- semaphore ---------------- */
typedef struct {
    volatile uint32_t count;
    uint32_t          max;
} os_sem_t;

void os_sem_init(os_sem_t *s, uint32_t initial, uint32_t max);
int  os_sem_take(os_sem_t *s, uint32_t timeout_ticks);  /* task only      */
int  os_sem_give(os_sem_t *s);                          /* task or ISR    */

/* ---------------- mutex ---------------- */
typedef struct {
    tcb_t *volatile owner;
} os_mutex_t;

void os_mutex_init(os_mutex_t *m);
int  os_mutex_lock(os_mutex_t *m, uint32_t timeout_ticks);  /* task only */
int  os_mutex_unlock(os_mutex_t *m);                        /* task only */

/* ---------------- internal (shared by os.c / os_sync.c / os_port.s) ------ */
tcb_t *os_sched_select(void);              /* called from PendSV          */
/* Every state / priority change MUST go through these two so the ready
 * bitmap and ready lists stay consistent (call with IRQs disabled). */
void   os_task_set_state(tcb_t *t, task_state_t state);
void   os_task_set_prio(tcb_t *t, uint8_t prio);
void   os_block_current(const void *obj, uint32_t timeout_ticks);
tcb_t *os_highest_waiter(const void *obj);
void   os_request_switch(void);            /* pend PendSV                 */

#endif /* OS_H */
