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

/* exec_gen is the existing shell/AS generation, supplied by the caller.
 * Foreground selection is separate from registration; GUI routing stays in WM. */
typedef struct {
    u32 owner, exec_gen, command_gen, context, flags;
} KbdPolicySnapshot;
int input_context_owner_register(u32 owner, u32 exec_gen, u32 parent, u32 parent_gen);
int input_context_first_shell(u32 exec_gen);
int input_context_owner_select(u32 owner, u32 exec_gen);
int input_context_owner_release(u32 owner, u32 exec_gen);
int input_context_owner_set(u32 owner, u32 exec_gen, u32 context, u32 flags);
int input_context_owner_begin(u32 owner, u32 exec_gen);
int input_context_owner_end(u32 owner, u32 exec_gen);
int input_context_snapshot(KbdPolicySnapshot *out);

#endif
