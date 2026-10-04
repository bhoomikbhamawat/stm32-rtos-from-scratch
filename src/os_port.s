/*
 * os_port.s - Cortex-M4F context switch (PendSV) and first-task launch (SVC).
 *
 * What the hardware does for us on exception entry (interview staple):
 *   - Pushes R0-R3, R12, LR, PC, xPSR (8 words) onto the CURRENT stack
 *     (the task's PSP), plus S0-S15 + FPSCR (18 words, lazily) if the task
 *     had used the FPU (CONTROL.FPCA = 1).
 *   - Loads LR with EXC_RETURN, which encodes where to return:
 *       0xFFFFFFFD  thread mode, PSP, basic frame
 *       0xFFFFFFED  thread mode, PSP, extended (FPU) frame   <- bit 4 = 0
 * So the software only has to save what the hardware didn't: R4-R11, and
 * S16-S31 when the task has an FPU frame.
 *
 * Per-task stack after a switch-out (low address = tcb->sp):
 *   R4 R5 R6 R7 R8 R9 R10 R11 EXC_RETURN [S16..S31]  | hardware frame ...
 */
    .syntax unified
    .cpu    cortex-m4
    .fpu    fpv4-sp-d16
    .thumb

    .extern os_curr
    .extern os_started
    .extern os_sched_select

/* ------------------------------------------------------------------------- */
    .section .text.PendSV_Handler, "ax", %progbits
    .global PendSV_Handler
    .type   PendSV_Handler, %function
    .thumb_func
PendSV_Handler:
    cpsid   i                       @ keep SysTick/ISRs out while TCBs change

    /* ---- save the outgoing task ---- */
    mrs     r0, psp                 @ r0 = task stack pointer (after HW stacking)
    tst     lr, #0x10               @ EXC_RETURN bit4 == 0 -> FPU frame active
    it      eq
    vstmdbeq r0!, {s16-s31}         @ save callee-saved FPU regs (also triggers
                                    @ the lazy save of S0-S15 into the HW frame)
    stmdb   r0!, {r4-r11, lr}       @ save callee-saved core regs + EXC_RETURN
    ldr     r1, =os_curr
    ldr     r2, [r1]
    str     r0, [r2]                @ os_curr->sp = r0   (sp is TCB offset 0)

    /* ---- choose the next task (C, AAPCS: clobbers r0-r3, r12, lr) ---- */
    bl      os_sched_select         @ r0 = next TCB
    ldr     r1, =os_curr
    str     r0, [r1]                @ os_curr = next

    /* ---- restore the incoming task ---- */
    ldr     r0, [r0]                @ r0 = next->sp
    ldmia   r0!, {r4-r11, lr}       @ restore core regs and ITS EXC_RETURN
    tst     lr, #0x10
    it      eq
    vldmiaeq r0!, {s16-s31}
    msr     psp, r0                 @ hardware unstacks the rest from here

    cpsie   i
    bx      lr                      @ exception return -> resumes next task
    .size   PendSV_Handler, . - PendSV_Handler

/* ------------------------------------------------------------------------- */
/*
 * SVC #0 from os_start(): "return" into the first task. Same restore path as
 * PendSV, minus the save (there is no outgoing task yet).
 */
    .section .text.SVC_Handler, "ax", %progbits
    .global SVC_Handler
    .type   SVC_Handler, %function
    .thumb_func
SVC_Handler:
    ldr     r1, =os_curr
    ldr     r1, [r1]
    ldr     r0, [r1]                @ r0 = first task's saved sp
    ldmia   r0!, {r4-r11, lr}       @ lr = 0xFFFFFFFD from the fake frame
    msr     psp, r0
    ldr     r1, =os_started
    movs    r2, #1
    str     r2, [r1]                @ SysTick may now drive the scheduler
    bx      lr                      @ -> thread mode on PSP, PC = task entry
    .size   SVC_Handler, . - SVC_Handler

    .end
