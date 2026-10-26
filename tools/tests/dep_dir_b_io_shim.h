/* Test-only I/O boundary: every operation is recorded, including unexpected waits. */
#ifndef DEP_DIR_B_IO_SHIM_H
#define DEP_DIR_B_IO_SHIM_H
void outp(unsigned int port, unsigned int value);
unsigned int inp(unsigned int port);
void _halt(void);
void _enable(void);
void _disable(void);
unsigned int irq_save(void);
void irq_restore(unsigned int flags);
void io_wait(void);
void io_wait_n(int n);
#endif
