#include "input_context.h"
#include "io.h"
#include "exec/appslot.h"
#include "mm/pgalloc.h"

/* Interrupt and display restoration have independent lifetimes (N4). */
typedef struct {
    u32 owner, exec_gen, command_gen, reason;
} InputInterruptPending;

typedef struct {
    u32 owner, resource_gen;
} InputDisplayRestorePending;

static InputInterruptPending interrupt_pending;
static u32 coalesced_count;
static InputDisplayRestorePending display_restore_pending;

/* M5 calls this with interrupts disabled, before publishing a generation. */
static void input_interrupt_discard(void)
{
    interrupt_pending.owner = 0;
    interrupt_pending.exec_gen = 0;
    interrupt_pending.command_gen = 0;
    interrupt_pending.reason = 0;
}

void input_interrupt_init(void)
{
    unsigned int irq = irq_save();
    input_interrupt_discard();
    coalesced_count = 0;
    display_restore_pending.owner = 0;
    display_restore_pending.resource_gen = 0;
    input_context_discard_register(input_interrupt_discard);
    irq_restore(irq);
}

/* A pending request retains its original destination and reason. */
int input_interrupt_capture(const KbdPolicySnapshot *destination, u32 reason)
{
    unsigned int irq = irq_save();
    if (!destination || !destination->owner || !destination->exec_gen || !reason) {
        irq_restore(irq);
        return -1;
    }
    if (interrupt_pending.owner) {
        ++coalesced_count;
    } else {
        interrupt_pending.owner = destination->owner;
        interrupt_pending.exec_gen = destination->exec_gen;
        interrupt_pending.command_gen = destination->command_gen;
        interrupt_pending.reason = reason;
    }
    irq_restore(irq);
    return 0;
}

int input_interrupt_take(const KbdPolicySnapshot *current, u32 *reason)
{
    unsigned int irq = irq_save();
    if (!current || !reason || !interrupt_pending.owner) {
        irq_restore(irq);
        return -1;
    }
    int matches = interrupt_pending.owner == current->owner &&
                  interrupt_pending.exec_gen == current->exec_gen &&
                  interrupt_pending.command_gen == current->command_gen;
    if (matches) *reason = interrupt_pending.reason;
    input_interrupt_discard();
    irq_restore(irq);
    return matches ? 0 : -1;
}

u32 input_interrupt_coalesced_count(void)
{
    unsigned int irq = irq_save();
    u32 count = coalesced_count;
    irq_restore(irq);
    return count;
}

void input_interrupt_exec_exit(u32 owner, u32 exec_gen)
{
    unsigned int irq = irq_save();
    if (interrupt_pending.owner == owner && interrupt_pending.exec_gen == exec_gen)
        input_interrupt_discard();
    irq_restore(irq);
}

int input_display_restore_capture(u32 owner)
{
    unsigned int irq = irq_save();
    u32 gen;
    if (appslot_gfx_resource_snapshot((int)owner, &gen)) {
        irq_restore(irq);
        return 0;
    }
    if (display_restore_pending.resource_gen &&
        (display_restore_pending.owner != owner ||
         display_restore_pending.resource_gen != gen)) {
        irq_restore(irq);
        return -1;
    }
    display_restore_pending = (InputDisplayRestorePending){owner, gen};
    irq_restore(irq);
    return 0;
}

int input_display_restore_drain(void)
{
    if (!_irq_enabled() || kctx_irq_depth || kctx_exc_depth) return -1;
    unsigned int irq = irq_save();
    InputDisplayRestorePending pending = display_restore_pending;
    irq_restore(irq);
    if (!pending.resource_gen) return 0;
    int rc = appslot_gfx_owner_exit_if((int)pending.owner, pending.resource_gen);
    if (rc < 0) return -1;
    irq = irq_save();
    if (display_restore_pending.owner == pending.owner &&
        display_restore_pending.resource_gen == pending.resource_gen)
        display_restore_pending = (InputDisplayRestorePending){0, 0};
    irq_restore(irq);
    return 0;
}
