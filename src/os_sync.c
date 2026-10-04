/*
 * os_sync.c - Counting semaphores and mutexes.
 *
 * Both use "direct hand-off": when a waiter exists, give/unlock passes the
 * token (or ownership) straight to the highest-priority waiter instead of
 * incrementing a count and letting tasks race for it. That removes the retry
 * loop and guarantees the woken task actually gets the resource.
 *
 * Interview notes:
 *  - Semaphore = signalling (ISR -> task, producer -> consumer). No owner.
 *  - Mutex = mutual exclusion. Has an owner; only the owner may unlock;
 *    never use from an ISR (an ISR can't block and isn't a task to boost).
 *  - Priority inversion: low task L holds the mutex, high task H blocks on
 *    it, medium task M preempts L -> H waits on M indefinitely (Mars
 *    Pathfinder, 1997). Priority inheritance fixes it by temporarily raising
 *    L to H's priority until it unlocks.
 */
#include "os.h"

/* ------------------------------------------------------------------------- */
/* Semaphore                                                                 */
/* ------------------------------------------------------------------------- */
void os_sem_init(os_sem_t *s, uint32_t initial, uint32_t max)
{
    s->count = initial;
    s->max   = max;
}

int os_sem_take(os_sem_t *s, uint32_t timeout_ticks)
{
    uint32_t pm = os_enter_critical();

    if (s->count > 0U) {
        s->count--;
        os_exit_critical(pm);
        return OS_OK;
    }
    if (timeout_ticks == 0U) {
        os_exit_critical(pm);
        return OS_ERR_TIMEOUT;
    }

    os_block_current(s, timeout_ticks);   /* pends PendSV ...                */
    os_exit_critical(pm);                 /* ... which is taken right here   */

    /* We run again only after a give (hand-off) or a timeout */
    return (os_curr->wake_reason == WAKE_SIGNAL) ? OS_OK : OS_ERR_TIMEOUT;
}

/* Safe from ISRs: never blocks, only pends PendSV. */
int os_sem_give(os_sem_t *s)
{
    int      rc = OS_OK;
    uint32_t pm = os_enter_critical();

    tcb_t *w = os_highest_waiter(s);
    if (w != NULL) {
        w->wait_obj    = NULL;
        w->wake_reason = WAKE_SIGNAL;
        os_task_set_state(w, TASK_READY);
        if (w->prio >= os_curr->prio) {
            os_request_switch();
        }
    } else if (s->count < s->max) {
        s->count++;
    } else {
        rc = OS_ERR_FULL;
    }

    os_exit_critical(pm);
    return rc;
}

/* ------------------------------------------------------------------------- */
/* Mutex with basic priority inheritance                                     */
/* ------------------------------------------------------------------------- */
void os_mutex_init(os_mutex_t *m)
{
    m->owner = NULL;
}

int os_mutex_lock(os_mutex_t *m, uint32_t timeout_ticks)
{
    uint32_t pm = os_enter_critical();

    if (m->owner == NULL) {
        m->owner = os_curr;
        os_exit_critical(pm);
        return OS_OK;
    }
    if (m->owner == os_curr) {        /* non-recursive mutex */
        os_exit_critical(pm);
        return OS_ERR_OWNER;
    }
    if (timeout_ticks == 0U) {
        os_exit_critical(pm);
        return OS_ERR_TIMEOUT;
    }

    /* Priority inheritance: lend our priority to the owner */
    if (m->owner->prio < os_curr->prio) {
        os_task_set_prio(m->owner, os_curr->prio);
    }

    os_block_current(m, timeout_ticks);
    os_exit_critical(pm);

    /* On WAKE_SIGNAL, unlock() already made us the owner */
    return (os_curr->wake_reason == WAKE_SIGNAL) ? OS_OK : OS_ERR_TIMEOUT;
}

/*
 * Simplification (worth mentioning if asked): the owner drops straight back
 * to its base priority. That is correct while a task holds at most one
 * contended mutex at a time; nested mutexes would need the kernel to
 * recompute the priority from the remaining waiters, and a waiter that times
 * out leaves the owner boosted until it unlocks.
 */
int os_mutex_unlock(os_mutex_t *m)
{
    uint32_t pm = os_enter_critical();

    if (m->owner != os_curr) {
        os_exit_critical(pm);
        return OS_ERR_OWNER;
    }

    os_task_set_prio(os_curr, os_curr->base_prio);   /* undo inheritance */

    tcb_t *w = os_highest_waiter(m);
    if (w != NULL) {
        m->owner       = w;                 /* hand ownership over */
        w->wait_obj    = NULL;
        w->wake_reason = WAKE_SIGNAL;
        os_task_set_state(w, TASK_READY);
        if (w->prio >= os_curr->prio) {
            os_request_switch();
        }
    } else {
        m->owner = NULL;
    }

    os_exit_critical(pm);
    return OS_OK;
}
