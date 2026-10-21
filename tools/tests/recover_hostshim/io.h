#ifndef RECOVER_HOST_IO_H
#define RECOVER_HOST_IO_H
#include "types.h"
u32 recover_test_irq_save(void);
void recover_test_irq_restore(u32 flags);
void recover_test_outp(u32 port,u32 value);
u32 recover_test_inp(u32 port);
static inline unsigned int irq_save(void) { return recover_test_irq_save(); }
static inline void irq_restore(unsigned int flags) { recover_test_irq_restore(flags); }
static inline void outp(unsigned int port,unsigned int value) { recover_test_outp(port,value); }
static inline unsigned int inp(unsigned int port) { return recover_test_inp(port); }
#endif
