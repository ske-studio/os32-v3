#include "arch_tick_budget.h"
#include "pit.h"

u32 arch_tick_budget_base(void)
{
    return pit_read_count();
}

u32 arch_tick_budget_elapsed(u32 base)
{
    u32 now = arch_tick_budget_base();
    u32 reload = pit_get_setup()->reload;
    return base >= now ? base - now : base + reload - now;
}

u32 arch_tick_budget_cnt(u32 us)
{
    /* Budget constants are small; split Hz so no product overflows u32. */
    u32 hz = pit_get_setup()->clk_hz;
    return (hz / 1000000u) * us + (hz % 1000000u) * us / 1000000u;
}
