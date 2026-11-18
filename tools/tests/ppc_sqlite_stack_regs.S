/* 期待の出所: PPC_SQLITE_STACK_DESIGN §6 / PPC_FP_NEWLIB_DESIGN §8 S1. */
#include "ppc_oea.h"
#include "arch_exec_storage.h"

.text
.align 2
/* Frame: linkage, saved CR and FP output pointer, original and returned GPRs.
 * r29 carries the expected SP across the call, so recovery needs no load. */
.globl s1_abi_call
s1_abi_call:
    mflr 0
    stwu 1,-176(1)
    stw 0,180(1)
    mfcr 0
    stw 0,8(1)
    stw 6,12(1)
    stmw 14,16(1)
    stw 1,12(5)
    lis 12,.Lgpr@ha
    addi 12,12,.Lgpr@l
    lmw 14,0(12)
    mr 29,1
    lis 0,0x0055
    ori 0,0,0x5000
    mtcrf 0x38,0
    bl sqlite_call_on_alt_stack
.Lreturned:
    cmpw 1,29
    beq .Lsp_ok
    mr 1,29
    li 3,1
    b .Lrestore
.Lsp_ok:
    mflr 10
    lis 11,.Lreturned@ha
    addi 11,11,.Lreturned@l
    li 3,3
    cmpw 10,11
    bne .Lrestore
    mfcr 0
    rlwinm 0,0,0,8,19
    lis 11,0x0055
    ori 11,11,0x5000
    cmpw 0,11
    bne .Lrestore
    stmw 14,96(1)
    lis 12,.Lgpr@ha
    addi 12,12,.Lgpr@l
    addi 10,1,96
    li 9,0
.Lgpr_check:
    lwzx 11,10,9
    lwzx 0,12,9
    cmpwi 9,(29-14)*4
    bne 1f
    mr 0,1
1:  cmpw 11,0
    bne .Lrestore
    addi 9,9,4
    cmpwi 9,(32-14)*4
    blt .Lgpr_check
    lwz 3,12(1)
    bl s1_sample
    li 3,0
.Lrestore:
    lmw 14,16(1)
    lwz 0,8(1)
    mtcrf 0x38,0
    lwz 0,180(1)
    mtlr 0
    addi 1,1,176
    blr

.macro capture_frame
    stw 1,0(3)
    lwz 0,0(1)
    stw 0,4(3)
    stw 30,8(3)
.endm
/* Frame: linkage, saved CR and FP output pointer, original and returned GPRs.
 * r29 carries the expected SP across the call, so recovery needs no load. */
.globl s1_abi_callback
s1_abi_callback:
    capture_frame
    b s1_abi_body
.globl s1_engine_callback
s1_engine_callback:
    capture_frame
    fneg 0,0
    mtfsfi 7,2
    b s1_engine_body

.macro fp_enable
    mfmsr 5
    ori 6,5,PPC_MSR_FP
    sync
    mtmsr 6
    isync
.endm
.macro fp_return
    sync
    mtmsr 5
    isync
    blr
.endm
.globl s1_seed
s1_seed:
    fp_enable
    lfd 0,PPC_FP_FPSCR_OFFSET(3)
    mtfsf 255,0
    .irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    lfd \reg,8*\reg(3)
    .endr
    fp_return
.globl s1_sample
s1_sample:
    fp_enable
    .irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    stfd \reg,8*\reg(3)
    .endr
    mffs 0
    stfd 0,PPC_FP_FPSCR_OFFSET(3)
    lfd 0,0(3)
    fp_return

/* Each exported symbol is the exact PC seen by QMP. */
.macro stop name
    .globl \name
\name:
    b \name
.endm
stop sqlite_s1_done
stop sqlite_s1_bad_sp
stop sqlite_s1_bad_fp
stop sqlite_s1_fail

.section .rodata
.align 2
.Lgpr:
    .irp reg,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    .long 0x51000000 + \reg
    .endr
.section .note.GNU-stack,"",@progbits
