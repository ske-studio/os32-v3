/* Plan-D root provider. Callers keep code, stack and provider data identity
 * mapped across both roots. No allocation, MMIO, or fill in the real interval.
 * MPC7450UM 2.3.4.3.1/5.4.4.2.3: 128 entries / two ways = 64 sets;
 * tlbie indexes EA[14:19] and invalidates both ways of both I/D TLBs.
 * NetBSD oea/pmap.c:548-575 supplies the invalidation ordering reference.
 * Integer-only kernel: no dssall is needed (MPC7450UM table 2-33).
 */
#include "ppc_oea.h"
.text
.balign 4
.globl ppc_root_current
.type ppc_root_current,@function
ppc_root_current:
    lis 3,.Lcurrent_root@ha
    lwz 3,.Lcurrent_root@l(3)
    blr
.size ppc_root_current,.-ppc_root_current

.globl ppc_root_load
.type ppc_root_load,@function
ppc_root_load:
    li 4,1
    b .Lroot_change
.size ppc_root_load,.-ppc_root_load

.globl ppc_root_flush
.type ppc_root_flush,@function
ppc_root_flush:
    li 4,0
.Lroot_change:
    /* ABI linkage area + private saved MSR/LR. r3/r4 survive entry. */
    stwu 1,-32(1)
    mflr 0
    stw 0,36(1)
    mfmsr 5
    stw 5,16(1)
    li 6,0
    ori 6,6,(PPC_MSR_EE|PPC_MSR_IR|PPC_MSR_DR)
    andc 6,5,6
    sync
    mtmsr 6
    isync
    cmpwi 4,0
    beq .Lclear
    lis 5,.Lcurrent_root@ha
    stw 3,.Lcurrent_root@l(5)
.Lclear:
    /* Unconditional, including load(current). C2 zeroes every HPT word. */
    bl ppc_hpt_clear
    sync
    li 3,0
    li 4,PPC_TLB_COUNT
    mtctr 4
.Linvalidate:
    tlbie 3
    eieio
    sync
    addi 3,3,PPC_PAGE_SIZE
    bdnz .Linvalidate
    tlbsync
    sync
.Lfinish:
    lwz 5,16(1)
    lis 6,.Lcurrent_root@ha
    lwz 6,.Lcurrent_root@l(6)
    cmpwi 6,0
    bne .Lrestore
    /* Before paging has a root, never resume translated execution. The
     * initial load from real mode still restores the original real MSR. */
    li 6,(PPC_MSR_IR|PPC_MSR_DR)
    andc 5,5,6
.Lrestore:
    mtmsr 5
    isync
    lwz 0,36(1)
    mtlr 0
    addi 1,1,32
    blr
.size ppc_root_flush,.-ppc_root_flush

.bss
.balign 4
.Lcurrent_root:
    .long 0
.section .note.GNU-stack,"",@progbits
