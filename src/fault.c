/*
 * fault.c - Custom fault handlers for crash diagnostics.
 *
 * Flow:
 *  1. Every fault vector lands in a tiny naked asm stub that works out WHICH
 *     stack the hardware pushed the exception frame on (EXC_RETURN bit 2:
 *     0 = MSP, 1 = PSP) and passes that frame pointer to C.
 *  2. fault_handler_c() copies the stacked registers and the SCB fault status
 *     registers into g_fault (a .noinit RAM record).
 *  3. Debugger attached -> BKPT so GDB stops right there with full context.
 *     No debugger -> light the red LED, wait, and reset. On the next boot,
 *     main() finds the record and keeps a copy in g_last_boot_fault.
 *
 * Reading a fault in GDB:  `p/x g_fault`, then `info line *g_fault.pc`
 * (or `list *g_fault.pc`) gives the exact source line that crashed.
 *
 * Common CFSR bits (interview: "how do you debug a HardFault?"):
 *   UFSR (CFSR >> 16): DIVBYZERO(9) UNALIGNED(8) NOCP(3) INVPC(2) INVSTATE(1) UNDEFINSTR(0)
 *   BFSR (CFSR >> 8):  BFARVALID(7) LSPERR(5) STKERR(4) UNSTKERR(3) IMPRECISERR(2) PRECISERR(1) IBUSERR(0)
 *   MMFSR (CFSR):      MMARVALID(7) MLSPERR(5) MSTKERR(4) MUNSTKERR(3) DACCVIOL(1) IACCVIOL(0)
 */
#include "fault.h"
#include "stm32f407.h"
#include "board.h"
#include "os.h"
#include <string.h>

__attribute__((section(".noinit"))) volatile fault_record_t g_fault;
fault_record_t g_last_boot_fault;

void fault_init(void)
{
    /* Without these, every fault escalates to HardFault and you lose the
     * information about which kind it was. */
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA | SCB_SHCSR_BUSFAULTENA | SCB_SHCSR_USGFAULTENA;
    SCB->CCR   |= SCB_CCR_DIV_0_TRP;    /* integer divide by zero -> UsageFault */
}

int fault_check_previous_boot(void)
{
    if (g_fault.magic == FAULT_MAGIC) {
        memcpy(&g_last_boot_fault, (const void *)&g_fault, sizeof(g_last_boot_fault));
        g_fault.magic = 0;
        return 1;
    }
    memset(&g_last_boot_fault, 0, sizeof(g_last_boot_fault));
    return 0;
}

void fault_handler_c(uint32_t *frame, uint32_t exc_return) __attribute__((used, noreturn));

void fault_handler_c(uint32_t *frame, uint32_t exc_return)
{
    g_fault.exception  = __get_IPSR() & 0x1FFU;
    g_fault.r0         = frame[0];
    g_fault.r1         = frame[1];
    g_fault.r2         = frame[2];
    g_fault.r3         = frame[3];
    g_fault.r12        = frame[4];
    g_fault.lr         = frame[5];
    g_fault.pc         = frame[6];
    g_fault.xpsr       = frame[7];
    g_fault.cfsr       = SCB->CFSR;
    g_fault.hfsr       = SCB->HFSR;
    g_fault.mmfar      = SCB->MMFAR;
    g_fault.bfar       = SCB->BFAR;
    g_fault.exc_return = exc_return;
    g_fault.sp         = (uint32_t)(uintptr_t)frame;
    if ((exc_return & 0x4U) && os_curr != NULL) {
        g_fault.task_id   = os_curr->id;
        g_fault.task_name = os_curr->name;
    } else {
        g_fault.task_id   = 0xFFU;
        g_fault.task_name = "handler/main";
    }
    g_fault.magic = FAULT_MAGIC;

    led_on(LED_RED);

    if (COREDEBUG_DHCSR & DHCSR_C_DEBUGEN) {
        __asm volatile ("bkpt #0");      /* stop here in GDB: p/x g_fault */
    }

    /* No debugger: give a visible red LED for ~1 s, then reset */
    for (volatile uint32_t i = 0; i < 20000000UL; i++) { }
    SCB->AIRCR = SCB_AIRCR_SYSRESET;
    __dsb();
    for (;;) { }
}

/* All four fault vectors share the same stub */
__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile (
        "tst   lr, #4          \n"   /* which stack holds the frame? */
        "ite   eq              \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "mov   r1, lr          \n"
        "b     fault_handler_c \n"
    );
}

void MemManage_Handler(void)  __attribute__((alias("HardFault_Handler")));
void BusFault_Handler(void)   __attribute__((alias("HardFault_Handler")));
void UsageFault_Handler(void) __attribute__((alias("HardFault_Handler")));

/* ---------------- deliberate faults for testing the handler ---------------- */
typedef void (*fn_t)(void);

void fault_trigger_test(uint32_t kind)
{
    switch (kind) {
    case 1: {   /* UsageFault: DIVBYZERO */
        volatile uint32_t zero = 0;
        volatile uint32_t x = 100U / zero;
        (void)x;
        break;
    }
    case 2: {   /* BusFault: precise data bus error at an unmapped address */
        volatile uint32_t *bad = (volatile uint32_t *)0xCCCCCCC0UL;
        (void)*bad;
        break;
    }
    case 3:     /* UsageFault: UNDEFINSTR (permanently undefined encoding) */
        __asm volatile (".short 0xDEFF");
        break;
    case 4: {   /* UsageFault: INVSTATE (branch to an address with bit0 = 0) */
        fn_t f = (fn_t)0x08000100UL;
        f();
        break;
    }
    default:
        break;
    }
}
