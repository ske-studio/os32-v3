/* OS32 irreversible supervisor-only boundary. */
#include "ppc_oea.h"
#include "ppc_layout.h"
.text
.globl ppc_takeover
ppc_takeover:
    mr 31,3
    bl ppc_disable
    sync
    li 3,0
    mtmsr 3
    isync
#if PPC_MMU_MODE == 2
    b ppc_stop
#endif
    bl ppc_cpu_setup
    mr 3,31
    bl ppc_pmap_bootstrap
    lis 3,ppc_cpu_save@ha
    addi 3,3,ppc_cpu_save@l
    mtsprg0 3
    lis 3,ppc_vectors@ha
    addi 3,3,ppc_vectors@l
    bl ppc_vector_install
    mfmsr 3
    ori 3,3,(PPC_MSR_ME|PPC_MSR_RI)
    mtmsr 3
    isync
    mr 3,31
    b pm_mmu_start
.section .note.GNU-stack,"",@progbits
