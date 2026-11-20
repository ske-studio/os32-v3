/* Link wrapper in the acceptance ELF only. Exercise the real call site. */
.bss
.balign 4
.Lparent_sp: .space 4
.text
.globl __wrap_arch_entry_enter
__wrap_arch_entry_enter:
    cmpwi 4,0
    bne ctx_measured_entry
    mflr 0
    stw 0,4(1)
    stwu 1,-112(1)
    stmw 13,16(1)
    stw 2,92(1)
    mfcr 0
    stw 0,96(1)
    mfmsr 0
    stw 0,100(1)
    lis 8,.Lparent_sp@ha
    stw 1,.Lparent_sp@l(8)
    lis 8,0x00a5
    ori 8,8,0xb000
    mtcrf 0x38,8
    .irp reg,2,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    li \reg,\reg
    .endr
    bl ctx_measured_entry
.Lreturned:
    mfcr 7
    mflr 8
    lis 9,.Lreturned@ha
    addi 9,9,.Lreturned@l
    cmpw 8,9
    bne .Lbad_parent
    lis 8,.Lparent_sp@ha
    lwz 8,.Lparent_sp@l(8)
    cmpw 1,8
    bne .Lbad_parent
    rlwinm 7,7,0,8,19
    lis 8,0x00a5
    ori 8,8,0xb000
    cmpw 7,8
    bne .Lbad_parent
    .irp reg,2,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    cmpwi \reg,\reg
    bne .Lbad_parent
    .endr
    mfmsr 7
    lwz 8,100(1)
    xor 7,7,8
    andi. 7,7,PPC_MSR_FP
    bne .Lbad_parent
    lmw 13,16(1)
    lwz 2,92(1)
    lwz 0,96(1)
    mtcr 0
    lwz 1,0(1)
    lwz 0,4(1)
    mtlr 0
    blr
.Lbad_parent:
    trap

/* Stable NIPs distinguish an observed mismatch from an unrelated hang. */
.macro ctx_stop name
.globl ctx_bad_\name
ctx_bad_\name:
    b ctx_bad_\name
.endm
ctx_stop pr_frame
ctx_stop parent_link
ctx_stop root_branch
ctx_stop child_return
ctx_stop srr
