#include "input_context.h"
#include "io.h"

static KbdPolicyImeSnapshot ime_snapshot;

void kbd_policy_ime_state(u32 active, u32 owner, u32 command_gen)
{
    unsigned int irq = irq_save();
    KbdPolicySnapshot current;
    if (active && !input_context_snapshot(&current) && owner == current.owner &&
        command_gen == current.command_gen) {
        ime_snapshot = (KbdPolicyImeSnapshot){active, owner, current.exec_gen, command_gen};
    } else {
        ime_snapshot = (KbdPolicyImeSnapshot){0};
    }
    irq_restore(irq);
}

void input_ime_snapshot(KbdPolicyImeSnapshot *out)
{
    unsigned int irq = irq_save();
    *out = ime_snapshot;
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

int input_ime_cancel_pending(void) { return cancel_owner != 0; }

/* Action queue operations share the caller's capture/read IRQ exclusion. */
static struct ImeAction {
    u32 action, owner, exec_gen, command_gen, after;
} actions[8];
static unsigned head, count;
static u32 dropped;

int input_ime_action_capture(u32 action, const KbdPolicySnapshot *dst, u32 after)
{
    if ((action != INPUT_IME_ON && action != INPUT_IME_OFF) ||
        !dst || !dst->owner || !dst->exec_gen) return -1;
    if (count == 8) {
        ++dropped;
        return -2;
    }
    actions[(head + count) % 8] = (struct ImeAction){
        action, dst->owner, dst->exec_gen, dst->command_gen, after
    };
    ++count;
    return 0;
}

int input_ime_action_due(u32 popped)
{
    return count && actions[head].after == popped;
}

int input_ime_action_take(const KbdPolicySnapshot *now, u32 popped, u32 *action)
{
    if (!now || !action) return -1;
    while (input_ime_action_due(popped)) {
        struct ImeAction entry = actions[head];
        head = (head + 1) % 8;
        --count;
        if (entry.owner == now->owner && entry.exec_gen == now->exec_gen &&
            entry.command_gen == now->command_gen) {
            *action = entry.action;
            return 1;
        }
    }
    return 0;
}

void input_ime_action_flush(void) { count = 0; }
u32 input_ime_action_dropped(void) { return dropped; }


/* Called under IRQ exclusion before publishing the next foreground. */
void input_ime_exec_exit(u32 owner, u32 gen)
{
    if (ime_snapshot.owner == owner && ime_snapshot.exec_gen == gen)
        ime_snapshot = (KbdPolicyImeSnapshot){0};
    if (cancel_owner == owner && cancel_exec_gen == gen)
        cancel_owner = cancel_exec_gen = 0;
    unsigned kept = 0;
    for (unsigned i = 0; i < count; ++i) {
        struct ImeAction next = actions[(head + i) % 8];
        if (next.owner != owner || next.exec_gen != gen)
            actions[(head + kept++) % 8] = next;
    }
    count = kept;
}
