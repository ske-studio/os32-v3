#ifndef OS32_ARCH_KERNEL_FP_H
#define OS32_ARCH_KERNEL_FP_H
#include "kernel_fp.h"
struct arch_kernel_fp_guard { struct ppc_kernel_fp_guard ppc; };
extern int arch_kernel_fp_begin(struct arch_kernel_fp_guard *g);
extern void arch_kernel_fp_end(const struct arch_kernel_fp_guard *g);
#endif
