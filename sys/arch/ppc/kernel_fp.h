/* PPC_FP_NEWLIB_DESIGN §2: integer-only callers, surviving kernel stack. */
#ifndef OS32_PPC_KERNEL_FP_H
#define OS32_PPC_KERNEL_FP_H
#include "exec_arch.h"
struct ppc_kernel_fp_guard {
    struct arch_fp_context previous;
    u32 previous_msr;
};
/* Nonzero refuses IRQ/exception/reentry without changing FP state.
 * A successful interval must return normally through end: no exec/park,
 * longjmp or USER callback. IRQ handlers remain integer-only. */
int ppc_kernel_fp_begin(struct ppc_kernel_fp_guard *g);
void ppc_kernel_fp_end(const struct ppc_kernel_fp_guard *g);
#endif
