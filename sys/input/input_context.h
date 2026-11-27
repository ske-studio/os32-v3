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
void input_context_focus_register(void (*flush)(void));
void input_context_discard_register(void (*discard)(void));
void input_interrupt_init(void);
int input_context_command_begin(KbdPolicyContext *state);
int input_context_command_end(KbdPolicyContext *state);

/* exec_gen identifies a single reservation of an exec slot.
 * Foreground selection is separate from registration; GUI routing stays in WM. */
typedef struct {
    u32 owner, exec_gen, command_gen, context, flags;
} KbdPolicySnapshot;
int input_context_owner_reserve(u32 owner, u32 exec_gen, u32 parent, u32 parent_gen, u32 context);
void input_context_owner_activate(u32 owner, int foreground);
int input_context_owner_register(u32 owner, u32 exec_gen, u32 parent, u32 parent_gen);
int input_context_first_shell(u32 exec_gen);
u32 input_context_owner_generation(u32 owner);
int input_context_owner_select(u32 owner, u32 exec_gen);
void input_context_owner_quiesce(u32 owner, u32 gen);
int input_context_owner_release(u32 owner, u32 exec_gen);
int input_context_owner_set(u32 owner, u32 exec_gen, u32 context, u32 flags);
int input_context_action_mode(u32 owner, u32 gen);
int input_context_delivery_set(u32 owner, u32 gen, u32 mode);
int input_context_owner_begin(u32 owner, u32 exec_gen);
int input_context_owner_end(u32 owner, u32 exec_gen);
int input_context_snapshot(KbdPolicySnapshot *out);

/* Caller supplies the destination captured by IRQ or the WM. */
int input_interrupt_capture(const KbdPolicySnapshot *destination, u32 reason);
int input_interrupt_take(const KbdPolicySnapshot *current, u32 *reason);
u32 input_interrupt_coalesced_count(void);

int input_display_restore_capture(u32 owner);
int input_display_restore_drain(void);
int input_display_restore_lock(unsigned int *flags);
void input_ime_exec_exit(u32 owner, u32 gen);
void input_interrupt_exec_exit(u32 owner, u32 exec_gen);

typedef struct {
    u32 active, owner, exec_gen, command_gen;
} KbdPolicyImeSnapshot;
void kbd_policy_ime_state(u32 active, u32 owner, u32 command_gen);
void input_ime_snapshot(KbdPolicyImeSnapshot *out);
int input_ime_cancel_capture(const KbdPolicyImeSnapshot *destination);
int input_ime_cancel_take(const KbdPolicySnapshot *current);
u32 input_ime_cancel_coalesced_count(void);
u32 input_ime_cancel_replaced_count(void);

int input_ime_cancel_pending(void);
enum { INPUT_IME_ON = 1, INPUT_IME_OFF = 2 };
int input_ime_action_capture(u32 action, const KbdPolicySnapshot *dst, u32 after);
int input_ime_action_take(const KbdPolicySnapshot *now, u32 popped, u32 *action);
int input_ime_action_due(u32 popped);
void input_ime_action_flush(void);
u32 input_ime_action_dropped(void);

#endif
