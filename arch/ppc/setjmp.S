/* PPC32 SysV kernel jump context; interrupts remain disabled on return. */
#include "arch_jmp_storage.h"
#include "ppc_oea.h"

    .text
    .globl exec_setjmp
    .type exec_setjmp,@function
exec_setjmp:
    stw 1,PPC_JMP_R1_OFFSET(3)
    stw 2,PPC_JMP_R2_OFFSET(3)
    stmw 13,PPC_JMP_R13_OFFSET(3)
    mflr 0
    stw 0,PPC_JMP_LR_OFFSET(3)
    mfcr 0
    stw 0,PPC_JMP_CR_OFFSET(3)
    lis 4,kctx_irq_depth@ha
    lwz 0,kctx_irq_depth@l(4)
    stw 0,PPC_JMP_IRQ_DEPTH_OFFSET(3)
    lis 4,kctx_exc_depth@ha
    lwz 0,kctx_exc_depth@l(4)
    stw 0,PPC_JMP_EXCEPTION_DEPTH_OFFSET(3)
    mfmsr 4
    andi. 4,4,PPC_MSR_FP
    stw 4,PPC_JMP_FP_OFFSET(3)
    li 4,0
    stw 4,PPC_JMP_BUSY_OFFSET(3)
    li 3,0
    blr
    .size exec_setjmp,.-exec_setjmp

    .globl exec_longjmp
    .type exec_longjmp,@function
exec_longjmp:
    mfmsr 5
    li 4,0
    ori 4,4,(PPC_MSR_EE|PPC_MSR_FP)
    andc 5,5,4
    lwz 4,PPC_JMP_FP_OFFSET(3)
    or 5,5,4
    mtmsr 5
    isync
    lwz 5,PPC_JMP_BUSY_OFFSET(3)
    li 4,0
    stw 4,PPC_JMP_BUSY_OFFSET(3)
    cmpwi 7,5,0
    lwz 0,PPC_JMP_IRQ_DEPTH_OFFSET(3)
    lis 4,kctx_irq_depth@ha
    stw 0,kctx_irq_depth@l(4)
    lwz 0,PPC_JMP_EXCEPTION_DEPTH_OFFSET(3)
    lis 4,kctx_exc_depth@ha
    stw 0,kctx_exc_depth@l(4)
    lwz 0,PPC_JMP_LR_OFFSET(3)
    mtlr 0
    lwz 6,PPC_JMP_CR_OFFSET(3)
    lmw 13,PPC_JMP_R13_OFFSET(3)
    lwz 2,PPC_JMP_R2_OFFSET(3)
    /* Until r1 is back on the parent stack, the trap slot remains owned. */
    lwz 1,PPC_JMP_R1_OFFSET(3)
    beq 7,.Lland
    li 4,0
    stw 4,0(5)
.Lland:
    mtcr 6
    li 3,1
    blr
    .size exec_longjmp,.-exec_longjmp
