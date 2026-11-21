/* A USER entry changes root and privilege. A resident entry is a SysV call. */
#include "ppc_oea.h"

.text
.align 2
.globl arch_entry_enter
.type arch_entry_enter,@function
arch_entry_enter:
    cmpwi 4,0
    beq .Lresident
    mr 30,3
    mr 31,4
    bl ppc_irq_save
    lis 3,ppc_exec_kernel_sp@ha
    stw 1,ppc_exec_kernel_sp@l(3)
    mr 3,31
    bl ppc_boot_load_root
    li 0,0
    mtlr 0
    lwz 7,0(30)
    lwz 1,4(30)
    lwz 3,8(30)
    lwz 4,12(30)
    lwz 5,16(30)
    lwz 6,20(30)
    li 8,0
    ori 8,8,(PPC_MSR_EE|PPC_MSR_PR|PPC_MSR_ME|PPC_MSR_FP|PPC_MSR_IR|PPC_MSR_DR|PPC_MSR_RI)
    mtsrr0 7
    mtsrr1 8
    rfi
.Lresident:
    /* Parent linkage and saved registers stay outside the shell image.
     * r31 survives the indirect call and identifies this frame on return. */
    mflr 0
    stw 0,4(1)
    stwu 1,-96(1)
    stw 2,12(1)
    mfcr 0
    stw 0,92(1)
    stmw 13,16(1)
    mr 31,1
    mfmsr 8
    andi. 9,8,PPC_MSR_FP
    stw 9,8(31)
    ori 8,8,PPC_MSR_FP
    mtmsr 8
    isync
    mr 7,3
    lwz 12,0(7)
    mtctr 12
    lwz 1,4(7)
    lwz 3,8(7)
    lwz 4,12(7)
    lwz 5,16(7)
    lwz 6,20(7)
    bctrl
    mr 1,31
    mfmsr 8
    li 9,PPC_MSR_FP
    andc 8,8,9
    lwz 9,8(1)
    or 8,8,9
    mtmsr 8
    isync
    lwz 2,12(1)
    lwz 0,92(1)
    mtcr 0
    lmw 13,16(1)
    lwz 1,0(1)
    lwz 0,4(1)
    mtlr 0
    blr
.size arch_entry_enter,.-arch_entry_enter
