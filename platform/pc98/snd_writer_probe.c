/* Single foreground writer. Only the armed wake marker is touched by IRQ. */
#include "snd_writer_probe.h"
#include "tsc_clock.h"

static struct {
    unsigned long result[SW_PAIRS + 1];
    u32 ready, exit, previous, work, longest, exit_work, jumps;
    unsigned phase;
} trace;
static volatile unsigned sleeping, wake_irq;
static volatile u32 wake_us;

static int stamp(u32 *us)
{
    u32 hi;
    return pc98_tsc_read_us(us, &hi) == 0;
}

void sw_reset(void)
{
    trace = (typeof(trace)){0};
    trace.result[SW_WAKE_IRQ] = ~0U;
    sleeping = 0;
    wake_irq = ~0U;
}

void sw_sleep(void)
{
    wake_irq = ~0U;
    sleeping = 1;
}

void sw_irq(unsigned irq)
{
    u32 now;
    if (!sleeping) return;
    sleeping = 0;
    if (stamp(&now)) {
        wake_us = now;
        wake_irq = irq;
    }
}

void sw_cancel(void)
{
    sleeping = trace.phase = 0;
    wake_irq = ~0U;
}

void sw_ready(void)
{
    u32 now;
    if (!stamp(&now)) { sw_cancel(); return; }
    if (wake_irq != ~0U && now - wake_us > trace.result[SW_WAKE_US]) {
        trace.result[SW_WAKE_US] = now - wake_us;
        trace.result[SW_WAKE_IRQ] = wake_irq;
    }
    wake_irq = ~0U;
    sleeping = 0;
    trace.phase = 1;
    trace.previous = trace.ready = now;
    trace.longest = trace.work = trace.jumps = 0;
}

/* Each interval ends at exactly one checkpoint, including the user interval. */
void sw_point(unsigned work)
{
    u32 now;
    if (!trace.phase) return;
    if (!stamp(&now)) { sw_cancel(); return; }
    u32 elapsed = now - trace.previous;
    trace.previous = now;
    if (elapsed >= 10000) ++trace.jumps;
    if (elapsed > trace.longest) {
        trace.longest = elapsed;
        trace.work = work;
    }
}

void sw_exit(void)
{
    if (trace.phase != 1) return;
    trace.exit_work = trace.work;
    trace.exit = trace.previous;
    trace.phase = 2;
}

void sw_write(void)
{
    if (trace.phase != 2) return;
    sw_point(SW_OTHER);
    if (!trace.phase) return;
    u32 first = trace.exit - trace.ready, second = trace.previous - trace.exit;
    if (first > trace.result[SW_WAIT_EXIT]) {
        trace.result[SW_WAIT_EXIT] = first;
        trace.result[SW_WORK] = (trace.result[SW_WORK] & 0xff) | (trace.exit_work << 8);
    }
    if (second > trace.result[SW_EXIT_WRITE]) trace.result[SW_EXIT_WRITE] = second;
    if (first + second > trace.result[SW_TOTAL]) {
        trace.result[SW_TOTAL] = first + second;
        trace.result[SW_WORK] = (trace.result[SW_WORK] & ~0xffU) | trace.work;
        trace.result[SW_WORK_US] = trace.longest;
    }
    trace.result[SW_JUMPS] += trace.jumps;
    ++trace.result[SW_PAIRS];
    sw_cancel();
}

void sw_snapshot(unsigned long *out)
{
    for (unsigned i = SW_WAIT_EXIT; i <= SW_PAIRS; ++i) out[i] = trace.result[i];
}
