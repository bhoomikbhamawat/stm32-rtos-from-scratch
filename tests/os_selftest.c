/*
 * os_selftest.c - On-target kernel self-test (built by `make selftest`,
 * replaces main.c). Checks priority inheritance and semaphore timeouts.
 *
 * Priority-inversion scenario:
 *   t=0   L (prio 1) locks the mutex and computes for 50 ticks
 *   t=10  H (prio 3) wants the mutex -> blocks; L inherits prio 3
 *   t=20  M (prio 2) becomes ready and wants the CPU for 200 ticks
 *  Without inheritance M would preempt L, and H would wait ~250 ticks.
 *  With inheritance L (now prio 3) keeps running, unlocks at ~t=50, H runs.
 *
 * Result: g_selftest.pass == 1 (read it with GDB, see tools/selftest.gdb).
 */
#include "os.h"
#include "dwt.h"
#include "board.h"
#include "fault.h"

typedef struct {
    uint32_t done;
    uint32_t pass;
    uint32_t L_prio_while_holding;   /* expect 3 (inherited from H)   */
    uint32_t L_prio_after_unlock;    /* expect 1 (back to base)       */
    uint32_t L_unlock_tick;
    uint32_t H_lock_tick;            /* expect right after L unlocks  */
    uint32_t M_done_tick;            /* expect AFTER H got the mutex  */
    int32_t  H_lock_rc;              /* expect OS_OK                  */
    int32_t  sem_rc;                 /* expect OS_ERR_TIMEOUT         */
    uint32_t sem_wait_ticks;         /* expect 7                      */
    int32_t  sem_give_rc;            /* expect OS_OK, then take OK    */
    int32_t  sem_take_rc;
} selftest_t;

volatile selftest_t g_selftest;

static uint32_t s_stk_l[256] __attribute__((aligned(8)));
static uint32_t s_stk_m[256] __attribute__((aligned(8)));
static uint32_t s_stk_h[256] __attribute__((aligned(8)));
static uint32_t s_stk_t[256] __attribute__((aligned(8)));
static uint32_t s_stk_c[256] __attribute__((aligned(8)));

static os_mutex_t s_mtx;
static os_sem_t   s_sem;

static void busy(uint32_t ticks)          /* burn CPU without blocking */
{
    uint32_t t0 = os_ticks();
    while ((os_ticks() - t0) < ticks) { }
}

static void task_l(void *a)
{
    (void)a;
    os_mutex_lock(&s_mtx, OS_WAIT_FOREVER);
    busy(30);
    g_selftest.L_prio_while_holding = os_curr->prio;
    busy(20);
    g_selftest.L_unlock_tick = os_ticks();
    os_mutex_unlock(&s_mtx);
    g_selftest.L_prio_after_unlock = os_curr->prio;
}

static void task_m(void *a)
{
    (void)a;
    os_delay(20);
    busy(200);
    g_selftest.M_done_tick = os_ticks();
}

static void task_h(void *a)
{
    (void)a;
    os_delay(10);
    g_selftest.H_lock_rc   = os_mutex_lock(&s_mtx, OS_WAIT_FOREVER);
    g_selftest.H_lock_tick = os_ticks();
    os_mutex_unlock(&s_mtx);
}

static void task_t(void *a)              /* semaphore timeout + give/take */
{
    (void)a;
    os_delay(5);
    uint32_t t0 = os_ticks();
    g_selftest.sem_rc         = os_sem_take(&s_sem, 7);
    g_selftest.sem_wait_ticks = os_ticks() - t0;
    g_selftest.sem_give_rc    = os_sem_give(&s_sem);
    g_selftest.sem_take_rc    = os_sem_take(&s_sem, 0);
}

void __attribute__((noinline)) selftest_complete(void)
{
    __asm volatile ("" ::: "memory");   /* GDB breakpoint hook */
}

static void task_check(void *a)
{
    (void)a;
    os_delay(400);
    volatile selftest_t *r = &g_selftest;
    r->pass = (r->L_prio_while_holding == 3U) &&
              (r->L_prio_after_unlock == 1U) &&
              (r->H_lock_rc == OS_OK) &&
              (r->H_lock_tick < r->M_done_tick) &&
              ((r->H_lock_tick - r->L_unlock_tick) <= 2U) &&   /* H runs as soon as L unlocks */
              (r->sem_rc == OS_ERR_TIMEOUT) &&
              (r->sem_wait_ticks >= 7U) && (r->sem_wait_ticks <= 8U) &&
              (r->sem_give_rc == OS_OK) &&
              (r->sem_take_rc == OS_OK);
    r->done = 1;
    led_on(r->pass ? LED_GREEN : LED_RED);
    selftest_complete();
    for (;;) { os_delay(1000); }
}

int main(void)
{
    board_leds_init();
    fault_init();
    (void)dwt_init();
    os_init();
    os_mutex_init(&s_mtx);
    os_sem_init(&s_sem, 0, 1);
    (void)os_task_create(task_l, NULL, 1, s_stk_l, 256, "L");
    (void)os_task_create(task_m, NULL, 2, s_stk_m, 256, "M");
    (void)os_task_create(task_h, NULL, 3, s_stk_h, 256, "H");
    (void)os_task_create(task_t, NULL, 4, s_stk_t, 256, "T");
    (void)os_task_create(task_check, NULL, 5, s_stk_c, 256, "check");
    os_start();
}
