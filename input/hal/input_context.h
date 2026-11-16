#ifndef INPUT_HAL_INPUT_CONTEXT_H
#define INPUT_HAL_INPUT_CONTEXT_H

#include "types.h"

enum {
    INPUT_CONTEXT_LINE_EDIT = 1, INPUT_CONTEXT_RUNNING,
    INPUT_CONTEXT_BYTE_INPUT, INPUT_CONTEXT_GUI
};
enum { INPUT_CONTEXT_ISIG = 1, INPUT_CONTEXT_ICANON = 2 };

typedef struct {
    u32 context, flags, command_gen;
    u32 next_gen, saved_context, saved_flags, command_active;
} KbdPolicyContext;

void input_context_init(KbdPolicyContext *state);
int input_context_set(KbdPolicyContext *state, u32 context, u32 flags);

/* The registered M6 callback runs with interrupts disabled. */
void input_context_discard_register(void (*discard)(void));
int input_context_command_begin(KbdPolicyContext *state);
int input_context_command_end(KbdPolicyContext *state);

#endif
