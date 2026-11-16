/* Expectation origin: PPC_FP_NEWLIB_DESIGN §8 F1〜F5・B1.
 * Integer caller; exercises every FPR, FPSCR RN, non-destructive save and init.
 * Real kernel transitions/lifecycle are supplied by the acceptance harness.
 * Returns 0 on success, 2 for roundtrip/save damage, 5 for init leakage. */
/* Host product-context test substitutes the three public context leaves.
 * Ordinary USER fixtures use the unprivileged helpers below. */
#ifdef PPC_FP_CONTEXT_TEST
#define ppc_fp_load arch_fp_restore
#define ppc_fp_store arch_fp_save
#define ppc_fp_zero arch_fp_init
#endif
.text
.align 2
.globl ppc_fp_probe
.type ppc_fp_probe,@function
ppc_fp_probe:
    stwu 1,-832(1)
    mflr 0
    stw 0,836(1)
    stw 30,824(1)
    stw 31,828(1)
    li 31,1
.Lround:
    addi 3,1,16
    li 4,32
    mtctr 4
    lis 5,0x3ff0
    li 6,1
.Lpattern:
    stw 5,0(3)
    stw 6,4(3)
    addi 5,5,1
    addi 6,6,1
    addi 3,3,8
    bdnz .Lpattern
    li 5,0
    stw 5,0(3)
    stw 31,4(3)
    addi 3,1,16
    bl ppc_fp_load
    /* F1: the first arithmetic instruction must execute without 0x800. */
    fadd 0,0,0
    /* Reapply exact bits before testing the non-destructive save contract. */
    addi 3,1,16
    bl ppc_fp_load
    li 30,2
    crclr 6
    addi 3,1,544
    bl ppc_fp_varargs_snapshot
    cmpwi 3,1
    bne .Ldone
    crset 6
    addi 3,1,544
    bl ppc_fp_varargs_snapshot
    cmpwi 3,0
    bne .Ldone
    addi 3,1,24
    addi 4,1,544
    li 5,16
    bl .Lcompare_words
    cmpwi 3,0
    bne .Ldone
    addi 3,1,280
    bl ppc_fp_store
    addi 3,1,544
    bl ppc_fp_store
    li 30,2
    addi 3,1,16
    addi 4,1,280
    bl .Lcompare
    cmpwi 3,0
    bne .Ldone
    addi 3,1,16
    addi 4,1,544
    bl .Lcompare
    cmpwi 3,0
    bne .Ldone
    bl ppc_fp_zero
    addi 3,1,544
    bl ppc_fp_store
    li 30,5
    li 4,66
    mtctr 4
    addi 3,1,544
.Lzero:
    lwz 4,0(3)
    cmpwi 4,0
    bne .Ldone
    addi 3,3,4
    bdnz .Lzero
    addi 31,31,1
    cmpwi 31,4
    bne .Lround
    li 30,0
.Ldone:
    mr 3,30
    lwz 30,824(1)
    lwz 31,828(1)
    lwz 0,836(1)
    mtlr 0
    addi 1,1,832
    blr
.Lcompare:
    li 5,66
.Lcompare_words:
    mtctr 5
.Lword:
    lwz 5,0(3)
    lwz 6,0(4)
    cmpw 5,6
    bne .Ldifferent
    addi 3,3,4
    addi 4,4,4
    bdnz .Lword
    li 3,0
    blr
.Ldifferent:
    li 3,1
    blr
.size ppc_fp_probe,.-ppc_fp_probe
#ifndef PPC_FP_CONTEXT_TEST
/* Integer pointer ABI for USER A/B patterns across syscall/park/resume. */
.globl ppc_fp_load
.type ppc_fp_load,@function
ppc_fp_load:
    lfd 0,256(3)
    mtfsf 255,0
    .set fp_offset,0
    .irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    lfd \reg,fp_offset(3)
    .set fp_offset,fp_offset+8
    .endr
    blr
.size ppc_fp_load,.-ppc_fp_load
.globl ppc_fp_store
.type ppc_fp_store,@function
ppc_fp_store:
    .set fp_offset,0
    .irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    stfd \reg,fp_offset(3)
    .set fp_offset,fp_offset+8
    .endr
    mffs 0
    stfd 0,256(3)
    lfd 0,0(3)
    blr
.size ppc_fp_store,.-ppc_fp_store
.globl ppc_fp_zero
.type ppc_fp_zero,@function
ppc_fp_zero:
    lis 3,.Luser_zero@ha
    addi 3,3,.Luser_zero@l
    b ppc_fp_load
.size ppc_fp_zero,.-ppc_fp_zero
.section .rodata
.balign 8
.Luser_zero:
    .zero 264
.text
#endif
/* SysV variadic receiver: CR1 EQ says that floating arguments were passed.
 * Capture f1..f8 for a fixture to bit-compare multiple double arguments. */
.globl ppc_fp_varargs_snapshot
.type ppc_fp_varargs_snapshot,@function
ppc_fp_varargs_snapshot:
    bne cr1,.Lno_fp_args
    stfd 1,0(3)
    stfd 2,8(3)
    stfd 3,16(3)
    stfd 4,24(3)
    stfd 5,32(3)
    stfd 6,40(3)
    stfd 7,48(3)
    stfd 8,56(3)
    li 3,0
    blr
.Lno_fp_args:
    li 3,1
    blr
.size ppc_fp_varargs_snapshot,.-ppc_fp_varargs_snapshot
.section .note.GNU-stack,"",@progbits
