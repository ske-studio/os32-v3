/* CPU0, EE=0 at start/dispatch. MPC7450UM 4.6.9; PEM 6.4.9.
 * NetBSD sys/arch/powerpc/powerpc/clock.c:108-116,139-159:
 * add periods to the negative DEC, preserving interrupt latency/phase.
 * Unlike the OpenBSD macppc re-arm scheme, never restart a full period. */
#include "arch_io.h"
#include "ppc_trap.h"
#include "sys/hal/time/tick_clock.h"
#include "sys/kern/irq/tick.h"
#include "sys/kern/irq/irq.h"

static u32 period, remainder, fraction;
static int ready;
static bool sample_enabled, sample_valid;
static struct ppc_dec_sample latest;

void ppc_dec_sample_enable(bool enable)
{
    sample_enabled = enable;
    sample_valid = false;
}

int ppc_dec_sample_read(struct ppc_dec_sample *out)
{
    if (!sample_valid) return -1;
    *out = latest;
    return 0;
}

static u32 next_period(void)
{
    u32 value = period;
    fraction += remainder;
    if (fraction >= PM_DEC_HZ) {
        fraction -= PM_DEC_HZ;
        ++value;
    }
    return value;
}

int ppc_dec_start(u32 tb_hz, u32 tick_hz)
{
    /* Keep every period positive, and signed DEC additions in range.
     * Match the F1 admission bounds; DEC is 1ms, logical ticks are 10ms. */
    if (ppc_irq_enabled() || tb_hz < 1000000 || tb_hz > 1000000000 ||
        tick_hz != PM_DEC_HZ) return -1;
    if (tick_clock_config(1, tick_hz)) return -1;
    ready = 0;
    ppc_dec_sample_enable(false);
    period = tb_hz / tick_hz;
    remainder = tb_hz % tick_hz;
    fraction = 0;
    ppc_trap_set_handler(0x900, ppc_decrementer_interrupt);
    ready = 1;
    ppc_write_dec(next_period());
    return 0;
}

void ppc_decrementer_interrupt(struct ppc_frame *frame)
{
    (void)frame;
    if (!ready) {
        ppc_write_dec(PPC_DEC_FAR);
        return;
    }
    bool capture = sample_enabled;
    unsigned long long before = capture ? ppc_read_tb64() : 0;
    int remaining = ppc_read_dec();
    u32 elapsed = 0;
    /* A stale pending exception with positive DEC must not invent a tick.
     * Include zero so every reload is strictly positive. */
    while (remaining <= 0) {
        remaining += (int)next_period();
        ++elapsed;
    }
    ppc_write_dec((u32)remaining);
    if (capture && elapsed) {
        unsigned long long after = ppc_read_tb64();
        u32 lo, hi;
        tick_clock_raw(&lo, &hi);
        unsigned long long raw = (((unsigned long long)hi << 32) | lo) + elapsed;
        latest = (struct ppc_dec_sample){before, after, (u32)raw, (u32)(raw >> 32)};
        sample_valid = true;
    }
    while (elapsed--)
        if (tick_clock_step() & TICK_STEP_JIFFY) tick_dispatch();
}

void arch_cpu_idle(void)
{
    /* FG1 only (EE=1, not in IRQ): nap until the next interrupt (DEC is 1 kHz).
     * EE=0 callers keep the old contract: enable and return without sleeping.
     * A wake-up marked between the caller's work check and the nap is
     * picked up by the next DEC, within 1 ms. */
    if (!ppc_irq_enabled() || kctx_irq_depth != 0) {
        ppc_enable();
        __asm__ volatile("nop" ::: "memory");
        return;
    }
    ppc_cpu_nap();
}

void arch_cpu_halt(void)
{
    /* Lightweight wait, preserving MSR[EE]; no POW/nap in milestone 2. */
    __asm__ volatile("nop" ::: "memory");
}

void arch_cpu_stop(void)
{
    ppc_disable();
    for (;;) __asm__ volatile("" ::: "memory");
}
