#include "input_context.h"

void input_context_init(InputContext *state)
{
    state->context = INPUT_CONTEXT_RUNNING;
    state->flags = INPUT_CONTEXT_ISIG;
    state->command_gen = 0;
}

int input_context_set(InputContext *state, u32 context, u32 flags)
{
    if (context < INPUT_CONTEXT_LINE_EDIT || context > INPUT_CONTEXT_GUI ||
        (flags & ~(INPUT_CONTEXT_ISIG | INPUT_CONTEXT_ICANON))) return -1;
    state->context = context;
    state->flags = flags;
    return 0;
}
