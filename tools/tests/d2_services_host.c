/* Real input forwarding and PC98 binding, with recording device operations. */
#include "input_service.h"
#include "../../drivers/kbd.h"
#include "../../kernel/input_service.c"
static int seen, arg, discarded;
static u32 sequence;
static void *output;
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
#define KEY_FAKE(name, id) int kbd_##name(void) { seen = id; return id + 100; }
KEY_FAKE(getchar, 1)
KEY_FAKE(getkey, 2)
KEY_FAKE(trygetchar, 3)
KEY_FAKE(trygetkey, 4)
KEY_FAKE(peekkey, 5)
KEY_FAKE(trygetchar_local, 6)
KEY_FAKE(getchar_kapi, 7)
KEY_FAKE(getkey_kapi, 8)
int kbd_getkey_wait(int interruptible) { seen = 9; arg = interruptible; return -19; }
KEY_FAKE(has_key, 10)
int kbd_is_pressed(int scancode) { seen = 11; arg = scancode; return 111; }
KEY_FAKE(trygetrawkey, 12)
u32 kbd_get_modifiers(void) { seen = 13; return 0x87654321u; }
u32 kbd_dropped_count(void) { seen = 14; return 0xfedcba98u; }
volatile u8 kbd_shift_state;
void kbd_set_gui_mode(int on) { seen = 16; arg = on; }
void kbd_discard_stop(void) { seen = 17; discarded++; }
int kbd_diag(KbdDiag *out) { seen = 18; output = out; return -18; }
int kbd_diag_log(u32 after_seq, KbdDiagLogEnt *out, int max)
{ seen = 19; sequence = after_seq; output = out; arg = max; return -19; }
#include "pointer_fixture_host.h"
#include "port_fixture_host.h"
#include "../../platform/pc98/d2_bind.c"
/* Generated wrappers retain real output guards and slot numbers. */
static int hit;
#define KAPI_HIT(n) (hit = (n))
int ring3_user_ranges_writable(u32 p, u32 n, u32 q, u32 m)
{ (void)p; (void)n; (void)q; (void)m; return 1; }
void ring3_fault_kill(void) { __builtin_trap(); }
#include "input_wrappers.inc"
#include "input_shift.inc"
#include "pointer_wrappers.inc"
#include "port_wrappers.inc"
#include "clock_fixture_host.h"
_Static_assert(sizeof(MouseState) == 10, "MouseState ABI size");
#define OFFSET(field, value) _Static_assert(__builtin_offsetof(MouseState, field) == value, "MouseState " #field)
OFFSET(x, 0); OFFSET(y, 2); OFFSET(dx, 4); OFFSET(dy, 6);
OFFSET(buttons, 8); OFFSET(mode, 9);
static int test(void)
{
    MouseState state = {1, 2, 3, 4, 5, 6};
    pointer_poll((MouseState *)0);
    pointer_poll(&state);
    CHECK(!state.x && !state.y && !state.dx && !state.dy && !state.buttons && !state.mode);
    CHECK(!pointer_available());
    pointer_set_bounds(-11, -22, 333, 444);
    pointer_cursor_set_mode(MOUSE_CURSOR_TEXT);
    pointer_cursor_show(); pointer_cursor_hide();
    CHECK(!pointer_seen);
    KbdDiag diag;
    KbdDiagLogEnt log;
    CHECK(input_getchar() == -1 && input_getkey() == -1);
    CHECK(input_trygetchar() == -1 && input_trygetkey() == -1);
    CHECK(input_peekkey() == -1 && input_trygetchar_local() == -1);
    CHECK(input_getchar_kapi() == -1 && input_getkey_kapi() == -1);
    CHECK(input_getkey_wait(1) == -1 && input_trygetrawkey() == -1);
    CHECK(!input_has_key() && !input_is_pressed(KEY_SHIFT));
    CHECK(!input_get_modifiers() && !input_dropped_count() && !input_shift_state());
    CHECK(input_diag(&diag) == OS32_ERR_NOSYS);
    CHECK(input_diag_log(1, &log, 1) == OS32_ERR_NOSYS);
    input_set_gui_mode(1); input_discard_stop(); input_owner_exit(1);
    u32 mode = 99, baud = 99, fifo = 99, overflow = 99;
    SerialDiag serial_diag;
    CHECK(port_getchar_kapi() == -1 && port_trygetchar() == -1);
    CHECK(!port_is_initialized());
    CHECK(port_putchar_kapi('X') == OS32_ERR_NOSYS);
    CHECK(port_init_vfast(9600) == OS32_ERR_NOSYS);
    CHECK(port_get_status(&mode, &baud, &fifo) == OS32_ERR_NOSYS);
    CHECK(!mode && !baud && !fifo);
    port_diag_get(&mode, &baud, &fifo, &overflow);
    CHECK(!mode && !baud && !fifo && !overflow);
    port_diag_get(0, 0, 0, 0); port_get_status(0, 0, 0);
    port_init(9600); port_puts_kapi("ignored");
    CHECK(con_output_putchar('X') == OS32_ERR_NOSYS && !port_seen);
    /* Emergency wiring works even before pc98_d2_bind and ignores the gate. */
    emergency_gate = 1;
    CHECK(serial_gate_active());
    serial_puts_polled("panic"); CHECK(emergency_seen == 1 && port_string[0] == 'p');
    emergency_gate = 0; CHECK(!serial_gate_active());
    ClockDate date = {2026, 1, 2, 3, 4, 5, 6};
    RTC_Time legacy = {26, 1, 2, 3, 4, 5, 6};
    clock_read_date(&date); clock_read_legacy(&legacy);
    CHECK(!date.year && !date.month && !date.day && !date.wday && !date.hour && !date.min && !date.sec);
    CHECK(!legacy.year && !legacy.month && !legacy.day && !legacy.wday && !legacy.hour && !legacy.min && !legacy.sec);
    CHECK(!clock_reads);
    pc98_d2_bind();
    CHECK(clock_check() == 0);
    CHECK(con_output_putchar('C') == -130 && port_seen == 10 && port_char == 'C');
#include "port_wrapper_checks.inc"
#define CALL(name, id) CHECK(input_##name() == id + 100 && seen == id)
    CALL(getchar, 1); CALL(getkey, 2); CALL(trygetchar, 3);
    CALL(trygetkey, 4); CALL(peekkey, 5); CALL(trygetchar_local, 6);
    CALL(getchar_kapi, 7); CALL(getkey_kapi, 8);
    CHECK(input_getkey_wait(1) == -19 && seen == 9 && arg == 1);
    CHECK(input_getkey_wait(0) == -19 && arg == 0);
    CALL(has_key, 10);
    CHECK(input_is_pressed(KEY_CTRL) == 111 && seen == 11 && arg == KEY_CTRL);
    CALL(trygetrawkey, 12);
    CHECK(input_get_modifiers() == 0x87654321u && seen == 13);
    CHECK(input_dropped_count() == 0xfedcba98u && seen == 14);
    kbd_shift_state = SHIFT_KANA | SHIFT_CTRL;
    CHECK(input_shift_state() == (SHIFT_KANA | SHIFT_CTRL));
    CHECK(fep_kbd_shift_state() == (SHIFT_KANA | SHIFT_CTRL));
    input_set_gui_mode(1); CHECK(seen == 16 && arg == 1);
    input_set_gui_mode(0); CHECK(arg == 0);
    input_owner_exit(0); CHECK(!discarded);
    input_owner_exit(1); CHECK(discarded == 1 && seen == 17);
    input_discard_stop(); CHECK(discarded == 2);
    CHECK(input_diag(&diag) == -18 && seen == 18 && output == &diag);
    CHECK(input_diag_log(0xff123456u, &log, -3) == -19 && seen == 19);
    CHECK(sequence == 0xff123456u && output == &log && arg == -3);
    pointer_poll((MouseState *)0);
    CHECK(pointer_seen == 1 && !pointer_output);
    pointer_poll(&state);
    CHECK(pointer_seen == 1 && pointer_output == &state);
    CHECK(state.x == -123 && state.y == 456 && state.dx == 7 && state.dy == -8);
    CHECK(state.buttons == (MOUSE_BTN_LEFT | MOUSE_BTN_RIGHT) && state.mode == MOUSE_MODE_SEAMLESS);
    CHECK(pointer_available() == 1 && pointer_seen == 2);
    pointer_set_bounds(-11, -22, 333, 444);
    CHECK(pointer_seen == 3 && pointer_bounds[0] == -11 && pointer_bounds[1] == -22);
    CHECK(pointer_bounds[2] == 333 && pointer_bounds[3] == 444);
    pointer_cursor_set_mode(MOUSE_CURSOR_GFX);
    CHECK(pointer_seen == 4 && pointer_mode == MOUSE_CURSOR_GFX);
    pointer_cursor_show(); CHECK(pointer_seen == 5);
    pointer_cursor_hide(); CHECK(pointer_seen == 6);
#include "pointer_wrapper_checks.inc"
#include "input_wrapper_checks.inc"
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc ? 1 : 0) : "memory");
    __builtin_unreachable();
}
