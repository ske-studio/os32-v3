#include "arch_tick_budget.h"
#include "pit.h"
#include "tick_order.h"
#include "tsc_clock.h"

_Static_assert(ARCH_TICK_BUDGET_FIRST_SLOT == PC98_TICK_NE2K, "budget starts at NE2K");

static int budget_tsc;
u32 arch_tick_budget_base(void)
{
    u32 lo, hi;
    budget_tsc = pc98_tsc_ready() && pc98_tsc_read_us(&lo, &hi) == 0;
    return budget_tsc ? lo : pit_read_count();
}

u32 arch_tick_budget_elapsed(u32 base)
{
    u32 now, hi;
    if (budget_tsc) {
        if (pc98_tsc_read_us(&now, &hi) != 0) return ~0u;
        return now - base;
    }
    now = pit_read_count();
    u32 reload = pit_get_setup()->reload;
    u32 counts = base >= now ? base - now : base + reload - now;
    return (u32)((unsigned long long)counts * 1000000u / pit_get_setup()->clk_hz);
}

u32 arch_tick_budget_cnt(u32 us)
{
    return us;
}
