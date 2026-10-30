#include "tick.h"
#include "io.h"
#include "irq.h"

static struct tick_slot slots[TICK_SLOT_COUNT];

int tick_register(unsigned int slot, tick_fn fn)
{
    unsigned int saved = irq_save();
    int rc = 0;

    if (kctx_irq_depth != 0) rc = TICK_ERR_CTX;
    else if (slot >= TICK_SLOT_COUNT || !fn) rc = TICK_ERR_INVAL;
    else if (slots[slot].fn && slots[slot].fn != fn) rc = TICK_ERR_BUSY;
    else slots[slot].fn = fn;
    irq_restore(saved);
    return rc;
}

void tick_dispatch(void)
{
    for (unsigned int slot = 0; slot < TICK_SLOT_COUNT; slot++) {
        if (slots[slot].fn) slots[slot].fn();
    }
}
