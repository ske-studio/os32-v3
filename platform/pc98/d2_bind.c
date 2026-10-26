/* PC-9821 resident input wiring. Registration performs no device I/O. */
#include "input_service.h"
#include "clock_service.h"
#include "../../drivers/rtc.h"
#include "port_service.h"
#include "console_output.h"
#include "../../drivers/serial.h"
#include "pointer_service.h"
#include "../../drivers/mouse.h"
#include "../../drivers/kbd.h"

static u8 pc98_shift_state(void) { return kbd_shift_state; }
static const InputOps pc98_input = {
    .irq = KBD_IRQ,
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

static const PointerOps pc98_pointer = {
    .poll = mouse_poll,
    .available = mouse_available,
    .bounds = mouse_set_bounds,
    .cursor_mode = mouse_cursor_set_mode,
    .cursor_show = mouse_cursor_show,
    .cursor_hide = mouse_cursor_hide,
};

static void pc98_port_init(u32 baud) { serial_init((unsigned long)baud); }
static int pc98_port_init_vfast(u32 baud) { return serial_init_vfast((unsigned long)baud); }
static const PortOps pc98_port = {
    .init = pc98_port_init, .puts_kapi = serial_puts_kapi,
    .getchar_kapi = serial_getchar_kapi, .putchar_kapi = serial_putchar_kapi,
    .trygetchar = serial_trygetchar, .is_initialized = serial_is_initialized,
    .init_vfast = pc98_port_init_vfast, .get_status = serial_get_status,
    .diag = serial_diag_get,
};
static const ConOutputOps pc98_console_output = { .putchar = serial_putchar };
const EmergencyOps emergency_output = {
    .puts_polled = pc98_serial_puts_polled, .gate_active = pc98_serial_gate_active,
};

void pc98_d2_bind(void)
{
    input_register(&pc98_input);
    pointer_register(&pc98_pointer);
    port_register(&pc98_port);
    clock_register(&pc98_clock);
    con_output_register(&pc98_console_output);
}
