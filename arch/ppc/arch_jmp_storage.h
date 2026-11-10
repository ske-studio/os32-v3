/* PowerPC kernel jump context storage: compile-only, provisional.
 * exec_setjmp/exec_longjmp are not implemented for PPC yet (② stops with
 * UNSUPPORTED on that path, PPC_KBOOT_DESIGN §5). The size covers the SysV
 * callee-saved set used by the probe recovery record (arch/ppc/ppc_frame.h
 * struct ppc_onfault: pc, sp, cr, r13-r31 = 22 words) plus the two saved
 * IRQ/exception depths that include/ksetjmp.h promises. Final in the ③ design. */
#ifndef OS32_PPC_ARCH_JMP_STORAGE_H
#define OS32_PPC_ARCH_JMP_STORAGE_H
#define ARCH_JMP_BYTES 96
#define ARCH_JMP_ALIGN 4
#endif
