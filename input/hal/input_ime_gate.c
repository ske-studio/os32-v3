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
    *out = ime_snapshot;
    irq_restore(irq);
}
