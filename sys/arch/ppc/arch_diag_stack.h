/* Stack diagnostics are not provided on PowerPC yet. */
#ifndef OS32_PPC_ARCH_DIAG_STACK_H
#define OS32_PPC_ARCH_DIAG_STACK_H
static __inline__ __attribute__((__always_inline__)) unsigned long arch_diag_sp(void)
{
    return 0;
}
#endif
