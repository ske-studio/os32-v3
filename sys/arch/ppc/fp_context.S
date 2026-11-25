/* PPC_FP_NEWLIB_DESIGN §2. MPC7450UM §4.5/4.6.8;
 * NetBSD fpu_subr.S save/restore order; OpenBSD save_fpu reloads f0.
 * Only volatile GPRs are used. All three public callers are soft-float. */
#include "ppc_oea.h"
#include "arch_exec_storage.h"
.text
.align 2
/* Each leaf saves MSR in r5, excludes IRQs, enables FP with FE=00. */
.macro fp_enter
    mfmsr 5
    li 6,0
    ori 6,6,(PPC_MSR_EE|PPC_MSR_FE)
    andc 6,5,6
    ori 6,6,PPC_MSR_FP
    mtmsr 6
    isync
.endm
.macro fp_leave
    sync
    mtmsr 5
    isync
    blr
.endm
.globl arch_fp_save
.type arch_fp_save,@function
arch_fp_save:
    fp_enter
    stfd 0,0(3)
    stfd 1,8(3)
    stfd 2,16(3)
    stfd 3,24(3)
    stfd 4,32(3)
    stfd 5,40(3)
    stfd 6,48(3)
    stfd 7,56(3)
    stfd 8,64(3)
    stfd 9,72(3)
    stfd 10,80(3)
    stfd 11,88(3)
    stfd 12,96(3)
    stfd 13,104(3)
    stfd 14,112(3)
    stfd 15,120(3)
    stfd 16,128(3)
    stfd 17,136(3)
    stfd 18,144(3)
    stfd 19,152(3)
    stfd 20,160(3)
    stfd 21,168(3)
    stfd 22,176(3)
    stfd 23,184(3)
    stfd 24,192(3)
    stfd 25,200(3)
    stfd 26,208(3)
    stfd 27,216(3)
    stfd 28,224(3)
    stfd 29,232(3)
    stfd 30,240(3)
    stfd 31,248(3)
    mffs 0
    stfd 0,PPC_FP_FPSCR_OFFSET(3)
    lfd 0,0(3)
    fp_leave
.size arch_fp_save,.-arch_fp_save
.globl arch_fp_init
.type arch_fp_init,@function
arch_fp_init:
    lis 3,.Lzero_fp@ha
    addi 3,3,.Lzero_fp@l
    b arch_fp_restore
.size arch_fp_init,.-arch_fp_init
.globl arch_fp_restore
.type arch_fp_restore,@function
arch_fp_restore:
    fp_enter
    lfd 0,PPC_FP_FPSCR_OFFSET(3)
    mtfsf 255,0
    lfd 0,0(3)
    lfd 1,8(3)
    lfd 2,16(3)
    lfd 3,24(3)
    lfd 4,32(3)
    lfd 5,40(3)
    lfd 6,48(3)
    lfd 7,56(3)
    lfd 8,64(3)
    lfd 9,72(3)
    lfd 10,80(3)
    lfd 11,88(3)
    lfd 12,96(3)
    lfd 13,104(3)
    lfd 14,112(3)
    lfd 15,120(3)
    lfd 16,128(3)
    lfd 17,136(3)
    lfd 18,144(3)
    lfd 19,152(3)
    lfd 20,160(3)
    lfd 21,168(3)
    lfd 22,176(3)
    lfd 23,184(3)
    lfd 24,192(3)
    lfd 25,200(3)
    lfd 26,208(3)
    lfd 27,216(3)
    lfd 28,224(3)
    lfd 29,232(3)
    lfd 30,240(3)
    lfd 31,248(3)
    fp_leave
.size arch_fp_restore,.-arch_fp_restore
.section .rodata
.balign ARCH_FP_ALIGN
.Lzero_fp:
    .zero ARCH_FP_BYTES
.section .note.GNU-stack,"",@progbits
