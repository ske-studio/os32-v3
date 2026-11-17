#include "kernel_tick.h"
#include "tick_clock.h"
#include "os32_kapi_shared.h"

volatile u32 tick_count = 0;
static u32 tick_hi, raw_lo, raw_hi;
static u32 jif_phase, ms_phase, ms_now, step_jif, step_ms, clk;

int tick_clock_config(u32 num, u32 den)
{
    if (!num || !den || num > 0xffffffffu / TICK_CLOCK_MS_HZ)
        return TICK_CLOCK_EINVAL;
    if (num * OS32_TICK_HZ > den)
        return TICK_CLOCK_EINVAL;
    if (den > 0xffffffffu - num * TICK_CLOCK_MS_HZ)
        return TICK_CLOCK_EINVAL;
    step_jif = num * OS32_TICK_HZ;
    step_ms = num * TICK_CLOCK_MS_HZ;
    clk = den;
    jif_phase = ms_phase = ms_now = 0;
    return 0;
}

u32 tick_clock_step(void)
{
    u32 result = 0;
    if (!clk) {
        if (!++tick_count) ++tick_hi;
        return TICK_STEP_JIFFY;
    }
    if (!++raw_lo) ++raw_hi;
    ms_phase += step_ms;
    while (ms_phase >= clk) {
        ms_phase -= clk;
        ++ms_now;
    }
    jif_phase += step_jif;
    if (jif_phase >= clk) {
        jif_phase -= clk;
        if (!++tick_count) ++tick_hi;
        result = TICK_STEP_JIFFY;
    }
    return result;
}

void tick_clock_raw(u32 *lo, u32 *hi)
{
    *lo = raw_lo;
    *hi = raw_hi;
}

void tick_clock_jiffy64(u32 *lo, u32 *hi)
{
    *lo = tick_count;
    *hi = tick_hi;
}

u32 tick_clock_ms(void)
{
    return ms_now;
}
