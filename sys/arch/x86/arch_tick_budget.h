#ifndef X86_ARCH_TICK_BUDGET_H
#define X86_ARCH_TICK_BUDGET_H
#include "types.h"
#define ARCH_TICK_BUDGET_FIRST_SLOT 2
u32 arch_tick_budget_base(void);
u32 arch_tick_budget_elapsed(u32 base);
u32 arch_tick_budget_cnt(u32 us);
#endif
