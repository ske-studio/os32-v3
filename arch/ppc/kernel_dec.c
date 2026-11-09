/* CPU0, EE=0 at start/dispatch. MPC7450UM 4.6.9; PEM 6.4.9.
 * NetBSD sys/arch/powerpc/powerpc/clock.c:108-116,139-159:
 * add periods to the negative DEC, preserving interrupt latency/phase.
 * Unlike the OpenBSD macppc re-arm scheme, never restart a full period. */
#include "ppc_trap.h"
#include "kernel_tick.h"
#include "kernel/irq/tick.h"
#include "kernel/irq/irq.h"

static u32 period, remainder, fraction;
static int ready;

static u32 next_period(void)
{
    u32 value = period;
    fraction += remainder;
    if (fraction >= 1000) {
        fraction -= 1000;
        ++value;
    }
    return value;
}

int ppc_dec_start(u32 tb_hz, u32 tick_hz)
{
    /* Keep every period positive, and signed DEC additions in range.
     * Match the F1 admission bounds; the public tick unit is always 1ms. */
    if (ppc_irq_enabled() || tb_hz < 1000000 || tb_hz > 1000000000 ||
        tick_hz != 1000) return -1;
    ready = 0;
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
    int remaining = ppc_read_dec();
    u32 elapsed = 0;
    /* A stale pending exception with positive DEC must not invent a tick.
     * Include zero so every reload is strictly positive. */
    while (remaining <= 0) {
        remaining += (int)next_period();
        ++elapsed;
    }
    ppc_write_dec((u32)remaining);
    ++kctx_irq_depth;
    while (elapsed--) {
        ++tick_count;
        tick_dispatch();
    }
    --kctx_irq_depth;
}

void arch_cpu_idle(void)
{
    ppc_enable();
    for (;;) __asm__ volatile("" ::: "memory");
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
