#include "input_context.h"
#include "io.h"

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
