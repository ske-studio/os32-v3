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
    display_restore_pending.owner = 0;
    display_restore_pending.resource_gen = 0;
    input_context_discard_register(input_interrupt_discard);
    irq_restore(irq);
}
