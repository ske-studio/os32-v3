/* Selected through the platform include path; keep net/ free of arch branches. */
#ifndef OS32_PC98_LINK_PLATFORM_INLINE_H
#define OS32_PC98_LINK_PLATFORM_INLINE_H
#include "net/link_platform.h"
#include "kernel_tick.h"
#include "kernel/irq/tick.h"
#include "platform/pc98/tick_order.h"
#include "io.h"
#include "kernel/idle_work.h"

static inline u32 pc98_link_platform_now(void)
{
    return tick_count;
}

static inline u32 pc98_link_platform_lock(void)
{
    return irq_save();
}

static inline void pc98_link_platform_unlock(u32 saved)
{
    irq_restore(saved);
}

static inline void pc98_link_platform_idle(void)
{
    idle_work_run();
    /* IRQ enable and wait must remain indivisible (see io.h). */
    _idle();
}

static inline int pc98_link_platform_tick_register(void (*fn)(void))
{
    return tick_register(PC98_TICK_LINK, fn);
}

/* LGY98 keeps its own RX scheduling; only the e82557 slot is drained in the tick. */
#define LINK_PLATFORM_TICK_RX_LGY98_SLOT 0

/* The external contract remains available to consumers without this header. */
#define link_platform_tick_register pc98_link_platform_tick_register
#define link_platform_now pc98_link_platform_now
#define link_platform_lock pc98_link_platform_lock
#define link_platform_unlock pc98_link_platform_unlock
#define link_platform_idle pc98_link_platform_idle

#endif
