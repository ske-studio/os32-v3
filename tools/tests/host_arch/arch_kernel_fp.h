#ifndef OS32_ARCH_KERNEL_FP_H
#define OS32_ARCH_KERNEL_FP_H
struct arch_kernel_fp_guard { char unused; };
static inline int arch_kernel_fp_begin(struct arch_kernel_fp_guard *g)
{
    (void)g;
    return 0;
}
static inline void arch_kernel_fp_end(const struct arch_kernel_fp_guard *g)
{
    (void)g;
}
#endif
