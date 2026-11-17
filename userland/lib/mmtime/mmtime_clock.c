#include "mmtime.h"
#include "os32api.h"

extern KernelAPI *kapi;

static unsigned long long base_us, last_out;
static u32 base_tick;

int mmt_now_us(u32 *lo, u32 *hi)
{
    u32 low, high;
    unsigned long long value;

    if (kapi->sys_time_now(&low, &high) == 0) {
        value = ((unsigned long long)high << 32) | low;
        base_us = value;
        base_tick = kapi->get_tick();
    } else {
        value = base_us + (unsigned long long)(u32)(kapi->get_tick() - base_tick)
                         * (MMT_QPF / OS32_TICK_HZ);
    }
    if (value < last_out) value = last_out;
    last_out = value;
    *lo = (u32)value;
    *hi = (u32)(value >> 32);
    return 0;
}

u32 mmt_now_ms(void)
{
    u32 lo, hi, result = 0, remainder;
    mmt_now_us(&lo, &hi);
    /* Only the low 32 bits of the quotient are public. Avoid a libgcc
     * 64-bit division dependency in the shared library. */
    remainder = hi % 1000u;
    for (int bit = 31; bit >= 0; --bit) {
        remainder = (remainder << 1) | ((lo >> bit) & 1u);
        if (remainder >= 1000u) {
            remainder -= 1000u;
            result |= 1UL << bit;
        }
    }
    return result;
}

int mmt_begin_period(u32 ms)
{
    return ms >= MMT_PERIOD_MIN && ms <= MMT_PERIOD_MAX ? 0 : OS32_ERR_INVAL;
}

int mmt_end_period(u32 ms)
{
    return mmt_begin_period(ms);
}
