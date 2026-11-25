/* PC-98 implementation of the common link platform contract. */
#include "sys/hal/net/link_platform.h"
#include "kernel_tick.h"
#include "sys/kern/irq/tick.h"
#include "sys/platform/pc98/tick_order.h"
#include "io.h"
#include "sys/kern/idle_work.h"

u32 link_platform_now(void)
{
    return tick_count;
}

u32 link_platform_lock(void)
{
    return irq_save();
}

void link_platform_unlock(u32 saved)
{
    irq_restore(saved);
}

void link_platform_idle(void)
{
    /* Boot-time link selftests may enter with interrupts disabled. */
    if (!_irq_enabled()) {
        _idle();
        return;
    }
    idle_wait_step();
}

int link_platform_tick_register(void (*fn)(void))
{
    return tick_register(PC98_TICK_LINK, fn);
}
