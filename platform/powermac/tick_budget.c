#include "arch_tick_budget.h"
#include "clock_source.h"

u32 arch_tick_budget_base(void)
{
    u32 value;
    __asm__ volatile("mftb %0" : "=r"(value));
    return value;
}

u32 arch_tick_budget_elapsed(u32 base)
{
    return arch_tick_budget_base() - base;
}

u32 arch_tick_budget_cnt(u32 us)
{
    struct clock_info info;
    platform_clock_info(&info);
    return (info.counter_hz / 1000000u) * us
         + (info.counter_hz % 1000000u) * us / 1000000u;
}
