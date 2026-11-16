#include "input_context.h"
#include "io.h"

static KbdPolicyImeSnapshot ime_snapshot;

void kbd_policy_ime_state(u32 active, u32 owner, u32 command_gen)
{
    unsigned int irq = irq_save();
    ime_snapshot.active = active;
    ime_snapshot.owner = owner;
    ime_snapshot.command_gen = command_gen;
    irq_restore(irq);
}

void input_ime_snapshot(KbdPolicyImeSnapshot *out)
{
    unsigned int irq = irq_save();
    KbdPolicySnapshot current;
    *out = ime_snapshot;
    out->exec_gen = 0;
    if (!input_context_snapshot(&current) && current.owner == out->owner)
        out->exec_gen = current.exec_gen;
    irq_restore(irq);
}

/* The cancellation slot is independent of the normal input queue. */
static u32 cancel_owner, cancel_exec_gen;
static u32 cancel_coalesced, cancel_replaced;

int input_ime_cancel_capture(const KbdPolicyImeSnapshot *destination)
{
    unsigned int irq = irq_save();
    if (!destination || !destination->active || !destination->owner ||
        !destination->exec_gen) {
        irq_restore(irq);
        return -1;
    }
    if (cancel_owner == destination->owner &&
        cancel_exec_gen == destination->exec_gen) {
        ++cancel_coalesced;
    } else {
        if (cancel_owner) ++cancel_replaced;
        cancel_owner = destination->owner;
        cancel_exec_gen = destination->exec_gen;
    }
    irq_restore(irq);
    return 0;
}

int input_ime_cancel_take(const KbdPolicySnapshot *current)
{
    unsigned int irq = irq_save();
    if (!current || !cancel_owner) {
        irq_restore(irq);
        return -1;
    }
    int matches = cancel_owner == current->owner &&
                  cancel_exec_gen == current->exec_gen;
    cancel_owner = 0;
    cancel_exec_gen = 0;
    irq_restore(irq);
    return matches ? 0 : -1;
}

u32 input_ime_cancel_coalesced_count(void)
{
    unsigned int irq = irq_save();
    u32 count = cancel_coalesced;
    irq_restore(irq);
    return count;
}

u32 input_ime_cancel_replaced_count(void)
{
    unsigned int irq = irq_save();
    u32 count = cancel_replaced;
    irq_restore(irq);
    return count;
}
