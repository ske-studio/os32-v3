/* Shared terminal wait; the provider owns the clock and all platform work. */
#include "../input/hal/in2_int.h"

#if OS32_HOOK_IME
#include "../input/hal/input_context.h"
#include "io.h"

int ime_apply_absolute(u32 action);

/* Installed by the PC-98 provider. kboot reads its provider directly. */
enum InputReadStep input_read_cooked(int cancel, int *key)
{
    int enabled = _irq_enabled();
    unsigned flags = irq_save();
    KbdPolicySnapshot dst;
    u32 action, popped = legacy_cooked_popped();
    enum InputReadStep step = IR_EMPTY;
    int apply = 0;
    if (input_ime_action_due(popped) || (cancel && input_ime_cancel_pending())) {
        if (enabled && !input_context_snapshot(&dst)) {
            if (cancel && input_ime_cancel_pending()) step = IR_RECHECK;
            else apply = input_ime_action_take(&dst, popped, &action) == 1;
        }
    }
    if (apply) step = IR_RECHECK;
    else if (step != IR_RECHECK && !input_ime_action_due(popped)) {
        const struct InputLegacyProvider *p = input_legacy_provider();
        *key = p->try_key ? p->try_key() : -1;
        if (*key >= 0) step = IR_KEY;
    }
    irq_restore(flags);
    if (apply) ime_apply_absolute(action);
    return step;
}

#endif

static enum InputReadStep legacy_wait(int mode, int interruptible, int cancel, int *out, u32 *start_tick, int resume)
{
    const struct InputLegacyProvider *p = input_legacy_provider();
    u32 start, timeout;
    int capture, timed, gui;

    if (!p || !p->wait_step) return IR_INTERRUPTED;
    gui = p->gui_active && p->gui_active();
    if (resume) start = *start_tick;
    else {
        start = p->wait_ticks ? p->wait_ticks() : 0;
        *start_tick = start;
    }
    /* Provider supplies KBD_RSHELL_TIMEOUT_SEC (30) in its clock units. */
    timeout = p->wait_timeout_ticks ? p->wait_timeout_ticks() : 0;
    capture = p->capture_active && p->capture_active();
    timed = !gui && (capture || (mode == IL_CHAR && p->terminal_enabled &&
                                 p->terminal_enabled()));
    for (;;) {
        if (resume && timed && timeout && p->wait_ticks &&
            (u32)(p->wait_ticks() - start) >= timeout) {
            *out = 0x1b; /* Only FEP key waits resume after IR_RECHECK. */
            return IR_KEY;
        }
        resume = 0;
        int key;
        if (gui) {
            key = p->inject_try ? p->inject_try() : -1;
            if (key >= 0) { *out = key; return IR_KEY; }
            if (p->park_kbd) p->park_kbd(interruptible);
        } else {
            if (p->wait_prepare) p->wait_prepare();
            if (p->read_cooked) {
                enum InputReadStep step = p->read_cooked(cancel, &key);
                if (step == IR_RECHECK) {
                    if (cancel) return step;
                    continue;
                }
                if (step != IR_KEY) key = -1;
            } else key = p->try_key ? p->try_key() : -1;
            if (key >= 0) { *out = mode == IL_KEY ? key : key & 0xff; return IR_KEY; }
            if (mode == IL_CHAR && p->terminal_enabled &&
                p->terminal_enabled() && p->terminal_try) {
                key = p->terminal_try();
                if (key >= 0) { *out = key; return IR_KEY; }
            }
        }
        if (interruptible && p->wait_pending && p->wait_pending()) return IR_INTERRUPTED;
        p->wait_step();
        if (timed && timeout && p->wait_ticks &&
            (u32)(p->wait_ticks() - start) >= timeout) {
            *out = mode == IL_KEY ? 0x1b : (p->capture_active && p->capture_active() ? '\r' : ' ');
            return IR_KEY;
        }
    }
}

int input_legacy_wait(int mode, int interruptible)
{
    int key;
    u32 start;
    return legacy_wait(mode, interruptible, 0, &key, &start, 0) == IR_KEY ? key : -1;
}

int fep_read_wait(int interruptible, int *key, u32 *start_tick, int resume)
{
    return legacy_wait(IL_KEY, interruptible, 1, key, start_tick, resume);
}
