#include "tick_budget.h"
#include "arch_tick_budget.h"
#include "kernel_tick.h"

static u32 base, rx_share, tx_share, rx_tail, tx_tail;
static int active, taken, rx_turn;

void tick_budget_config(void)
{
    rx_share = arch_tick_budget_cnt(IRQ0_SLOT_BUDGET_US / 2);
    tx_share = arch_tick_budget_cnt(IRQ0_SLOT_BUDGET_US);
    rx_tail = arch_tick_budget_cnt(NET_TAIL_RX_US);
    tx_tail = arch_tick_budget_cnt(NET_TAIL_TX_US);
}

u32 tick_budget_cnt(u32 us)
{
    switch (us) {
    case IRQ0_SLOT_BUDGET_US / 2: return rx_share;
    case IRQ0_SLOT_BUDGET_US: return tx_share;
    case NET_TAIL_RX_US: return rx_tail;
    case NET_TAIL_TX_US: return tx_tail;
    default: return 0;
    }
}

void tick_budget_begin(void)
{
    base = arch_tick_budget_base();
    rx_turn = !(tick_count & 1u);
    taken = 0;
    active = 1;
}

void tick_budget_end(void) { active = 0; }

int tick_budget_take(u32 share_cnt, u32 tail_cnt)
{
    if (!active) return 1;
    u32 elapsed = arch_tick_budget_elapsed(base);
    int turn = rx_turn ? share_cnt == rx_share : share_cnt == tx_share;
    if ((elapsed < share_cnt && tail_cnt < share_cnt - elapsed) ||
        (!taken && turn)) {
        taken = 1;
        return 1;
    }
    return 0;
}

/* Optional work must leave the first-unit guarantee to its actual consumer. */
int tick_budget_peek(u32 share_cnt, u32 tail_cnt)
{
    if (active) {
        u32 elapsed = arch_tick_budget_elapsed(base);
        return elapsed < share_cnt && share_cnt - elapsed > tail_cnt;
    }
    return 1;
}
