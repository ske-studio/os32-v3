#ifndef IO_H
#define IO_H
unsigned int inp(unsigned int port);
void outp(unsigned int port, unsigned int value);
static inline unsigned int irq_save(void) { return 0; }
static inline void irq_restore(unsigned int flags) { (void)flags; }
#endif
