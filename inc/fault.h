/*
 * fault.h - HardFault / MemManage / BusFault / UsageFault crash diagnostics.
 */
#ifndef FAULT_H
#define FAULT_H

#include <stdint.h>

#define FAULT_MAGIC 0xFA017EC0UL

typedef struct {
    uint32_t magic;          /* FAULT_MAGIC when the record is valid          */
    uint32_t exception;      /* IPSR: 3=HardFault 4=MemManage 5=BusFault 6=Usage */
    /* Stacked by hardware at the moment of the fault */
    uint32_t r0, r1, r2, r3, r12;
    uint32_t lr;             /* return address of the faulting function       */
    uint32_t pc;             /* instruction that faulted (or the next one)    */
    uint32_t xpsr;
    /* Fault status registers */
    uint32_t cfsr;           /* MMFSR[7:0] | BFSR[15:8] | UFSR[31:16]         */
    uint32_t hfsr;           /* bit30 FORCED = escalated from a lower fault   */
    uint32_t mmfar;          /* valid if CFSR.MMARVALID (bit 7)               */
    uint32_t bfar;           /* valid if CFSR.BFARVALID (bit 15)              */
    uint32_t exc_return;     /* which stack, FPU frame or not                 */
    uint32_t sp;             /* stack pointer at fault (frame address)        */
    uint32_t task_id;        /* 0xFF if it happened outside any task          */
    const char *task_name;
} fault_record_t;

/* Written by the fault handler; lives in .noinit so it survives the reset */
extern volatile fault_record_t g_fault;
/* Copy of the previous boot's crash (valid if .magic == FAULT_MAGIC) */
extern fault_record_t g_last_boot_fault;

void fault_init(void);              /* enable separate fault handlers, div0 trap */
int  fault_check_previous_boot(void);  /* 1 if last reset was caused by a fault */
void fault_trigger_test(uint32_t kind);  /* 1=div0 2=bad address 3=undef instr 4=bad fn ptr */

#endif
