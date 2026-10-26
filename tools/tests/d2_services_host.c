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
#include "../../platform/pc98/d2_bind.c"
/* Generated wrappers retain real output guards and slot numbers. */
static int hit;
#define KAPI_HIT(n) (hit = (n))
int ring3_user_ranges_writable(u32 p, u32 n, u32 q, u32 m)
{ (void)p; (void)n; (void)q; (void)m; return 1; }
void ring3_fault_kill(void) { __builtin_trap(); }
#include "input_wrappers.inc"
#include "input_shift.inc"
static int test(void)
{
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
    pc98_d2_bind();
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
#include "input_wrapper_checks.inc"
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc ? 1 : 0) : "memory");
    __builtin_unreachable();
}
