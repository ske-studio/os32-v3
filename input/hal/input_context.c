#include "input_context.h"

void input_context_init(KbdPolicyContext *state)
{
    state->context = INPUT_CONTEXT_RUNNING;
    state->flags = INPUT_CONTEXT_ISIG;
    state->command_gen = 0;
    state->next_gen = 0;
    state->command_active = 0;
    state->saved_context = 0;
    state->saved_flags = 0;
}

int input_context_set(KbdPolicyContext *state, u32 context, u32 flags)
{
    if (context < INPUT_CONTEXT_LINE_EDIT || context > INPUT_CONTEXT_GUI ||
        (flags & ~(INPUT_CONTEXT_ISIG | INPUT_CONTEXT_ICANON))) return -1;
    state->context = context;
    state->flags = flags;
    return 0;
}

#include "io.h"

static void (*discard_pending)(void);

void input_context_discard_register(void (*discard)(void))
{
    unsigned int irq = irq_save();
    discard_pending = discard;
    irq_restore(irq);
}

int input_context_command_begin(KbdPolicyContext *state)
{
    unsigned int irq = irq_save();
    if (state->command_active || !discard_pending) {
        irq_restore(irq);
        return -1;
    }
    state->saved_context = state->context;
    state->saved_flags = state->flags;
    discard_pending();
    if (++state->next_gen == 0) ++state->next_gen;
    state->context = INPUT_CONTEXT_RUNNING;
    state->flags = INPUT_CONTEXT_ISIG;
    state->command_active = 1;
    state->command_gen = state->next_gen;
    irq_restore(irq);
    return 0;
}

int input_context_command_end(KbdPolicyContext *state)
{
    unsigned int irq = irq_save();
    if (!state->command_active || !discard_pending) {
        irq_restore(irq);
        return -1;
    }
    discard_pending();
    state->command_gen = 0;
    state->context = state->saved_context;
    state->flags = state->saved_flags;
    state->command_active = 0;
    irq_restore(irq);
    return 0;
}
