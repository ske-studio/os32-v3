#include "geforce_tick.h"
#include "io.h"
#include "tick.h"
#include "tick_order.h"

_Static_assert(PC98_TICK_COUNT <= TICK_SLOT_COUNT, "tick slots");

static void (*tick_poll)(void *);
static void *tick_arg;

/* nouveau nvkm/core/intr.c:164 nvkm_intr dispatches pending work;
 * OS32 also polls on every tick to recover shared IRQ edges. */
static void geforce_tick(void)
{
    if (tick_poll) tick_poll(tick_arg);
}

int geforce_tick_bind(void (*poll)(void *), void *arg)
{
    unsigned int flags;
    int rc;

    if (!poll && !arg) {
        flags = irq_save();
        tick_poll = 0;
        tick_arg = 0;
        irq_restore(flags);
        return 0;
    }
    rc = tick_register(PC98_TICK_GF, geforce_tick);
    if (rc) return rc;
    flags = irq_save();
    tick_arg = arg;
    tick_poll = poll;
    irq_restore(flags);
    return 0;
}
