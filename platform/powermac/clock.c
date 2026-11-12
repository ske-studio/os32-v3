/* Integer CPU0 timebase provider. PEM 2.2.1: coherent upper/lower/upper;
 * MPC7450UM 4.6.9 / PEM 6.4.9: TB/DEC are not the CPU core clock.
 * BSD references: macppc/clock.c (OpenBSD) 119-129,251-261;
 * powerpc/clock.c (NetBSD) 108-116,139-159. DEC programming belongs to F2. */
#include "clock_source.h"
#include "platform_timer.h"
#include "kboot_info.h"
#include "os32_kapi_shared.h"

static struct clock_info clock_state;
static u32 bound_hz;
static unsigned long long origin;

#ifndef PM_CLOCK_FAKE
static inline u32 tb_upper(void)
{
    u32 value;
    __asm__ volatile("mftbu %0" : "=r"(value));
    return value;
}
static inline u32 tb_lower(void)
{
    u32 value;
    __asm__ volatile("mftb %0" : "=r"(value));
    return value;
}
#endif

static unsigned long long read_tb(void)
{
    u32 upper, lower, again;
    do {
        upper = tb_upper();
        lower = tb_lower();
        again = tb_upper();
    } while (upper != again);
    return ((unsigned long long)upper << 32) | lower;
}

/* Boot foreground, EE=0, before init; retain no snapshot pointer. */
void pm_clock_bind(const struct pm_kboot_info *info)
{
    clock_state = (struct clock_info){0};
    bound_hz = info ? info->tb_hz : 0;
}

u32 pm_tick_hz(void) { return 100; }

int platform_clock_init(u32 tick_hz)
{
    clock_state = (struct clock_info){0};
    /* Admission policy, not a core-clock estimate: 1MHz..1GHz covers
     * PowerMac bus-derived TB rates, with >=1 count/us and bounded
     * remainder*1000000 below 10^15. OF CPU timebase-frequency is required. */
    if (bound_hz < 1000000 || bound_hz > 1000000000 || tick_hz != pm_tick_hz())
        return OS32_ERR_INVAL;
    origin = read_tb();
    clock_state.tick_hz = tick_hz;
    clock_state.counter_hz = bound_hz;
    clock_state.ready = 1;
    return 0;
}

void platform_clock_info(struct clock_info *out) { *out = clock_state; }

void platform_clock_read(struct platform_clock_value *out)
{
    out->hz = bound_hz;
    out->is_8mhz = 0;
}

int platform_clock_read_us(u32 *lo, u32 *hi)
{
    if (!clock_state.ready) return OS32_ERR_NOSYS;
    unsigned long long delta = read_tb() - origin;
    unsigned long long hz = clock_state.counter_hz;
    /* Split before multiplication: even a full 64-bit TB interval fits
     * in us for admitted hz. lib/divdi3.c supplies the integer helpers. */
    unsigned long long us = (delta / hz) * 1000000
                          + ((delta % hz) * 1000000) / hz;
    *lo = (u32)us;
    *hi = (u32)(us >> 32);
    return 0;
}
