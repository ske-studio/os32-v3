/* L11: real control functions, with reset returning and halt waking once. */
#include "io.h"
static unsigned int enabled = TEST_IF;
static unsigned int rebooting, halts;

static void event(unsigned int op, unsigned int port, unsigned int value)
{
    unsigned int row[4] = { op, port, value, enabled };
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(row),
                     "d"(sizeof(row)) : "memory");
}
static void finish(unsigned int rc) __attribute__((noreturn));
static void finish(unsigned int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) {}
}
void outp(unsigned int port, unsigned int value) { event('W', port, value); }
unsigned int inp(unsigned int port) { event('R', port, 0); return 0; }
void _enable(void) { event('E', 0, 0); enabled = 1; }
void _disable(void) { event('D', 0, 0); enabled = 0; }
unsigned int irq_save(void)
{
    unsigned int old = enabled;
    event('S', 0, old); enabled = 0; return old;
}
void irq_restore(unsigned int flags) { event('F', 0, flags); enabled = flags; }
void io_wait(void) { event('T', 0, 0); }
void io_wait_n(int n) { event('T', 0, (unsigned int)n); }
void _halt(void)
{
    event('H', 0, 0);
    if (rebooting && ++halts == 2) finish(0);
}
/* L11 has no selected GF device or registered return callback. */
int gf_production_return(void) { return 0; }
#include "sys_control.inc"
void _start(void)
{
    buz_on();
    buz_off();
    buz_on();
    buz_off();
    sys_halt();
    event('C', 0, 0); /* sys_halt returns after one wake. */
    rebooting = 1;
    sys_reboot();
    finish(9); /* reboot must keep halting if reset and the first halt return. */
}
