#include "tick.h"
#include "tick_budget.h"
#include "arch_tick_budget.h"
#include "io.h"
#include "irq.h"
#ifdef TICK_PROBE
#include "tick_probe.h"
#endif

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
        if (slot == ARCH_TICK_BUDGET_FIRST_SLOT) tick_budget_begin();
        if (slots[slot].fn) {
#ifdef TICK_PROBE
            tick_probe_begin(TP_SLOT0 + slot);
#endif
            slots[slot].fn();
#ifdef TICK_PROBE
            tick_probe_end(TP_SLOT0 + slot);
#endif
        }
    }
    tick_budget_end();
}
