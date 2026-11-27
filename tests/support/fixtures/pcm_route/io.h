#ifndef IO_H
#define IO_H
unsigned int inp(unsigned int port);
void outp(unsigned int port, unsigned int value);
extern unsigned int model_if;
static inline unsigned int irq_save(void)
{
    unsigned int old = model_if;
    model_if = 0;
    return old;
}
static inline void irq_restore(unsigned int flags) { model_if = flags; }
static inline int _irq_enabled(void) { return model_if != 0; }
#endif
