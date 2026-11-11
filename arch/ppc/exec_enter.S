/* Enter a prepared USER context after switching to its address space. */
#include "ppc_oea.h"

.text
.align 2
.globl arch_entry_enter
.type arch_entry_enter,@function
arch_entry_enter:
    mr 30,3
    mr 31,4
    bl ppc_irq_save
    lis 3,ppc_exec_kernel_sp@ha
    stw 1,ppc_exec_kernel_sp@l(3)
    mr 3,31
    bl ppc_boot_load_root
    lwz 7,0(30)
    lwz 1,4(30)
    lwz 3,8(30)
    lwz 4,12(30)
    lwz 5,16(30)
    lwz 6,20(30)
    li 8,0
    ori 8,8,(PPC_MSR_EE|PPC_MSR_PR|PPC_MSR_ME|PPC_MSR_IR|PPC_MSR_DR|PPC_MSR_RI)
    mtsrr0 7
    mtsrr1 8
    rfi
.size arch_entry_enter,.-arch_entry_enter
