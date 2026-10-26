/* PC-9821 resident input wiring. Registration performs no device I/O. */
#include "input_service.h"
#include "../../drivers/kbd.h"

static u8 pc98_shift_state(void) { return kbd_shift_state; }
static const InputOps pc98_input = {
    .getchar = kbd_getchar,
    .getkey = kbd_getkey,
    .trygetchar = kbd_trygetchar,
    .trygetkey = kbd_trygetkey,
    .peekkey = kbd_peekkey,
    .trygetchar_local = kbd_trygetchar_local,
    .getchar_kapi = kbd_getchar_kapi,
    .getkey_kapi = kbd_getkey_kapi,
    .getkey_wait = kbd_getkey_wait,
    .has_key = kbd_has_key,
    .is_pressed = kbd_is_pressed,
    .trygetrawkey = kbd_trygetrawkey,
    .modifiers = kbd_get_modifiers,
    .dropped_count = kbd_dropped_count,
    .shift_state = pc98_shift_state,
    .gui_mode = kbd_set_gui_mode,
    .discard_stop = kbd_discard_stop,
    .diag = kbd_diag,
    .diag_log = kbd_diag_log,
};

void pc98_d2_bind(void)
{
    input_register(&pc98_input);
}
