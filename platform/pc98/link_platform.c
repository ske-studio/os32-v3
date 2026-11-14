/* PC-98 implementation of the common link platform contract. */
#include "net/link_platform.h"
#include "idt.h"
#include "io.h"
#include "kernel/idle_work.h"

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
    idle_work_run();
    /* IRQ enable and wait must remain indivisible (see io.h). */
    _idle();
}
