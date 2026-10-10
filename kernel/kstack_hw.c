/* Boot marks only unused stack. Diagnostics scan on demand, never per tick. */
#include "kstack_hw.h"
#include "memmap.h"
#define KSTACK_MARK 0xA55A69C3UL
#define SHELL_STACK_BASE (MEM_SHELL_STACK_TOP - MEM_SHELL_STACK_SIZE)
#if MEM_KSTACK_TOP - MEM_KSTACK_BASE > 65535 || MEM_SHELL_STACK_SIZE > 65535
#error "MemStat stack high-water halves must fit in 16 bits"
#endif
static const volatile u32 *low[2] = {P2V_CONST(MEM_KSTACK_TOP), P2V_CONST(MEM_SHELL_STACK_TOP)};

void __attribute__((cold)) kstack_hw_init(void)
{
    volatile u32 marker;
    volatile u32 *p = P2V_BOOT(MEM_KSTACK_BASE);
    /* The local bounds the fill below this live frame and its callers. */
    while (p < &marker) *p++ = KSTACK_MARK;
    /* Resident shell has not started; its whole stack is unused. */
    p = P2V_BOOT(SHELL_STACK_BASE);
    while (p < (volatile u32 *)P2V_BOOT(MEM_SHELL_STACK_TOP)) *p++ = KSTACK_MARK;
}

static u32 __attribute__((cold, noinline)) stack_water(u32 base, u32 top, unsigned int index)
{
    const volatile u32 *p = P2V(base);
    const volatile u32 *end = P2V(top);
    while (p < low[index] && *p == KSTACK_MARK) p++;
    low[index] = p; /* Retain each peak even if later data equals MARK. */
    return (u32)(end - p) * sizeof(*p);
}

u32 __attribute__((cold)) kstack_high_water(void)
{
    return stack_water(MEM_KSTACK_BASE, MEM_KSTACK_TOP, 0) |
           (stack_water(SHELL_STACK_BASE, MEM_SHELL_STACK_TOP, 1) << 16);
}
