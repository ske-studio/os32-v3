/* PC-98 PIT hardware only. Setup and time state belong to the kernel. */
#include "platform_timer.h"
#include "platform_irq.h"
#include "idt.h"
#include "io.h"

void platform_timer_program(unsigned int reload)
{
    outp(PIT_MODE, PIT_MODE_TIMER0);
    outp(PIT_CNTR0, reload & 0xFF);
    outp(PIT_CNTR0, (reload >> 8) & 0xFF);
}

void platform_timer_sample(struct platform_timer_sample *out)
{
    u8 lo, hi;
    out->p1 = platform_irq_timer_pending();
    outp(PIT_MODE, PIT_LATCH_TIMER0);
    lo = (u8)inp(PIT_CNTR0);
    hi = (u8)inp(PIT_CNTR0);
    out->count = ((unsigned int)hi << 8) | lo;
    out->p2 = platform_irq_timer_pending();
}
