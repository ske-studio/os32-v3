/* ILP32 host implementation of io.h's interrupt contract (T2a′). */
#ifndef ARCH_IO_H
#define ARCH_IO_H
#define X86_EFLAGS_IF 0x200U
static unsigned int host_arch_if = 0x202U;
static inline void _enable(void) { host_arch_if |= X86_EFLAGS_IF; }
static inline void _disable(void) { host_arch_if &= ~X86_EFLAGS_IF; }
static inline unsigned int irq_save(void) {
    unsigned int flags = host_arch_if;
    _disable();
    return flags;
}
static inline void irq_restore(unsigned int flags) { host_arch_if = flags; }
static inline int _irq_enabled(void) { return !!(host_arch_if & X86_EFLAGS_IF); }
static inline void _lidt(void *ptr) { (void)ptr; }
static inline void _halt(void) { for (;;) {} }
static inline void _idle(void) { _enable(); _halt(); }
static inline void _stop(void) { _disable(); _halt(); }
#endif
