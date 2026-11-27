#ifndef IO_H
#define IO_H
extern unsigned model_if;
static inline unsigned irq_save(void) { unsigned f = model_if; model_if = 0; return f; }
static inline void irq_restore(unsigned f) { model_if = f; }
static inline int _irq_enabled(void) { return model_if; }
unsigned inp(unsigned port);
unsigned inpw(unsigned port);
unsigned long inpd(unsigned port);
void outp(unsigned port, unsigned value);
void outpd(unsigned port, unsigned long value);
void _halt(void);
void _idle(void);
void _enable(void);
void _disable(void);
void _stop(void);
void io_wait(void);
void io_wait_n(int n);
#endif
