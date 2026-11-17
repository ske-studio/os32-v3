/* Check the actual TSC descriptor against the real dispatcher and policy. */
#include "types.h"
#include "pit.h"
#include "kernel/io_work.h"
volatile u32 kctx_irq_depth;
unsigned int irq_save(void) { return 0; }
void irq_restore(unsigned int flags) { (void)flags; }
int _irq_enabled(void) { return 1; }
u32 tick_clock_ms(void) { return 0; }
const struct pit_setup *pit_get_setup(void) { static const struct pit_setup p = {1996800, 1000, 1997, 1000, PIT_MODE_TIMER0, 1}; return &p; }
int pc98_pit_read_us(u32 *lo, u32 *hi) { *lo = *hi = 0; return 0; }
unsigned int idle_work_compat_count(void) { return 0; }
void idle_work_compat_run(void) {}
#include "platform/pc98/tsc_clock.c"
void _start(void)
{
    int rc = io_register(&tsc_desc) == 0 ? 0 : 1;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
