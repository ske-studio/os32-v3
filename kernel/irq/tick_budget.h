#ifndef TICK_BUDGET_H
#define TICK_BUDGET_H
#include "types.h"
#define IRQ0_SLOT_BUDGET_US 120u /* F_slot2 + F_demux = 25.4us <= 60 - 22.4. */
#define NET_TAIL_RX_US 23u       /* Measured 14.9us * 1.5, rounded up. */
#define NET_TAIL_TX_US 17u       /* Measured 11.0us * 1.5, rounded up. */
/* Clock setup, IF=0/EE=0; refresh the four fixed conversions outside IRQ. */
void tick_budget_config(void);
void tick_budget_begin(void);
void tick_budget_end(void);
/* RX uses B/2, TX uses B; one shared first unit, even RX / odd TX. */
int tick_budget_take(u32 share_cnt, u32 tail_cnt);
/* Only B/2, B, NET_TAIL_RX_US and NET_TAIL_TX_US are supported. */
u32 tick_budget_cnt(u32 us);
#endif
