/* OF-PPC 8.2/4.2.4; OpenBSD macppc/locore0.S:87.
 * PT_LOAD code, data and the A2 stack are physical identity addresses.
 * No firmware callback is possible beyond this boundary. */
#include "ppc_oea.h"
.text
.balign 4
.globl pm_kernel_takeover
.type pm_kernel_takeover,@function
pm_kernel_takeover:
    /* r3 retains the platform-owned snapshot. Discard firmware MSR state,
     * including EE/IR/DR/IP/FP/VEC/PR, before touching vectors or the HPT. */
    li 4,0
    sync
    mtmsr 4
    sync
    isync
    b ppc_kernel_real_start
.size pm_kernel_takeover,.-pm_kernel_takeover

.globl ppc_kernel_exception_enable
.type ppc_kernel_exception_enable,@function
ppc_kernel_exception_enable:
    li 3,(PPC_MSR_ME|PPC_MSR_RI)
    sync
    mtmsr 3
    sync
    isync
    blr
.size ppc_kernel_exception_enable,.-ppc_kernel_exception_enable
.globl ppc_boot_walk_stop
.type ppc_boot_walk_stop,@function
ppc_boot_walk_stop:
    b ppc_boot_walk_stop
.size ppc_boot_walk_stop,.-ppc_boot_walk_stop
.section .note.GNU-stack,"",@progbits
