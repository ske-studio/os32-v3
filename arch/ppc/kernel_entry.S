/* OF-PPC 8.2.1--8.3: r5 is the client interface, translation stays on.
 * OpenBSD macppc/locore0.S:87,119-123 supplies the stack-chain reference.
 * No firmware calls or MSR changes belong before the later capture stage. */
#include "kernel_entry.h"
    .section .text.entry,"ax",@progbits
    .balign 4
    .globl ppc_kernel_entry
    .type ppc_kernel_entry,@function
ppc_kernel_entry:
    mr 3,5                      /* Preserve CI across the BSS loop, in a GPR. */
    lis 1,__kernel_stack_top@ha
    addi 1,1,__kernel_stack_top@l
    rlwinm 1,1,0,0,27           /* 16-byte ABI alignment. */
    li 0,0
    stwu 0,-PPC_KERNEL_ENTRY_FRAME(1) /* Null back chain + caller linkage. */
    lis 8,__bss_start@ha
    addi 8,8,__bss_start@l
    lis 9,__bss_end@ha
    addi 9,9,__bss_end@l
.Lkernel_clear:
    cmplw 8,9
    bge .Lkernel_c
    stb 0,0(8)                 /* Exact half-open range, including ledger. */
    addi 8,8,1
    b .Lkernel_clear
.Lkernel_c:
    bl ppc_kernel_start
    .globl ppc_kernel_entry_stop
ppc_kernel_entry_stop:
    b ppc_kernel_entry_stop
    .size ppc_kernel_entry,.-ppc_kernel_entry
    .section .note.GNU-stack,"",@progbits
