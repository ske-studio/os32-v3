/* Shared terminal wait; the provider owns the clock and all platform work. */
#include "../input/hal/in2_int.h"

int input_legacy_wait(int mode, int interruptible)
{
    const struct InputLegacyProvider *p = input_legacy_provider();
    u32 start, timeout;
    int capture, timed;

    if (!p || !p->wait_step) return -1;
    start = p->wait_ticks ? p->wait_ticks() : 0;
    /* Provider supplies KBD_RSHELL_TIMEOUT_SEC (30) in its clock units. */
    timeout = p->wait_timeout_ticks ? p->wait_timeout_ticks() : 0;
    capture = p->capture_active && p->capture_active();
    timed = capture || (mode == IL_CHAR && p->terminal_enabled &&
                        p->terminal_enabled());
    for (;;) {
        int key;
        if (p->gui_active && p->gui_active()) {
            key = p->inject_try ? p->inject_try() : -1;
            if (key >= 0) return key;
            if (p->park_kbd) p->park_kbd(interruptible);
        } else {
            key = p->try_key ? p->try_key() : -1;
            if (key >= 0) return mode == IL_KEY ? key : key & 0xff;
            if (mode == IL_CHAR && p->terminal_enabled &&
                p->terminal_enabled() && p->terminal_try) {
                key = p->terminal_try();
                if (key >= 0) return key;
            }
        }
        if (interruptible && p->wait_pending && p->wait_pending()) return -1;
        p->wait_step();
        if (timed && timeout && p->wait_ticks &&
            (u32)(p->wait_ticks() - start) >= timeout)
            return mode == IL_KEY ? 0x1b : (capture ? '\r' : ' ');
    }
}
