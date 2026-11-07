#ifndef OS32_PPC_ARCH_IO_H
#define OS32_PPC_ARCH_IO_H
#include "ppc_frame.h"
static inline void _enable(void) { ppc_enable(); }
static inline void _disable(void) { ppc_disable(); }
static inline unsigned int irq_save(void) { return ppc_irq_save(); }
static inline void irq_restore(unsigned int flags) { ppc_irq_restore(flags); }
static inline int _irq_enabled(void) { return ppc_irq_enabled(); }
static inline void _lidt(void *p) { ppc_vector_install(p); }
void ppc_wait_not_available(void);
static inline void _idle(void) { ppc_wait_not_available(); }
static inline void _halt(void) { ppc_wait_not_available(); }
static inline void _stop(void) { ppc_stop(); }
#endif
