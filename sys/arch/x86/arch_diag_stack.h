/* Preserve the TRACE diagnostic instruction at the caller. */
#ifndef OS32_X86_ARCH_DIAG_STACK_H
#define OS32_X86_ARCH_DIAG_STACK_H
static __inline__ __attribute__((__always_inline__)) unsigned long arch_diag_sp(void)
{
    unsigned long sp;
    __asm__ volatile("mov %%esp, %0" : "=r"(sp));
    return sp;
}
#endif
