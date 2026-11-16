#include "in2_int.h"
#include "drivers/input/kbd.h"
#include "io.h"
#define T(c) do { if (!(c)) return __LINE__; } while (0)
static int native_v86, native_abort, native_guest;
volatile u32 tick_count;
int legacy_active(void) { return native_v86; }
int legacy_kbd_push(u8 sc) { (void)sc; native_guest++; return 1; }
void legacy_request_exit(void) { native_guest++; }
void ring3_abort_request(void) { native_abort++; }
void appslot_poll_yield_reset(void) {}
u32 in2_time_ms(void) { return tick_count * 10; }
#include "native_deliver.inc"
void oracle_init(u8 locks);
void oracle_deliver(u8 sc);
int oracle_pressed(int sc);
extern volatile u8 oracle_mods;
extern int oracle_abort, oracle_guest;
void oracle_mode(int on);
void oracle_discard(void);
int oracle_raw(void);
int oracle_cooked(void);
u32 oracle_dropped(void);
static int rings(void)
{
    int a, b;
    do { a = oracle_raw(); b = legacy_raw_take(); T(a == b); } while (a != -1);
    do { a = oracle_cooked(); b = legacy_cooked_take(); T(a == b); } while (a != -1);
    T(oracle_dropped() == legacy_raw_dropped() + legacy_cooked_dropped());
    T(native_abort == oracle_abort && native_guest == oracle_guest);
    return 0;
}
static int step(u8 scan)
{
    InputEvent out;
    u16 seq;
    u8 sc = scan & 127, down = !(scan & 128);
    int old = input_key_pressed(sc);
    oracle_deliver(scan); kbd_deliver(scan);
    T(oracle_mods == kbd_shift_state && input_key_mods() == oracle_mods);
    if (!native_v86) for (int i = 0; i < 128; i++) T(oracle_pressed(i) == input_key_pressed(i));
    if (!native_v86 && old != down) {
        T(in2_ring_pop(&out, &seq));
        T(out.type == INPUT_EVENT_KEY && out.sc == sc && out.down == down);
        T(out.src == 1 && out.generation && out.time_ms == in2_time_ms());
        T(out.buttons == oracle_mods);
    }
    T(!in2_ring_pop(&out, &seq));
    return 0;
}
static int probe(void)
{
    InputEvent out; u16 seq;
    oracle_init(0); kbd_hal_init(0);
    /* All keycodes, each modifier combination, both destinations, including repeat. */
    for (int gui = 0; gui < 2; gui++) {
        oracle_mode(gui); kbd_set_gui_mode(gui);
        for (int mask = 0; mask < 32; mask++) {
            for (int i = 0; i < 5; i++) if (mask & (1 << i)) T(!step(KEY_SHIFT + i));
            for (int sc = 0; sc < 128; sc++) {
                if (sc >= KEY_SHIFT && sc <= KEY_CTRL) continue;
                T(!step(sc) && !step(sc) && !step(sc | 128) && !rings());
            }
            for (int i = 0; i < 5; i++) if (mask & (1 << i)) T(!step((KEY_SHIFT + i) | 128));
            T(!rings());
        }
        /* Full legacy queues still update state; CTRL+STOP retained at raw tail. */
        for (int i = 0; i < 140; i++) T(!step(0x1d));
        T(!step(KEY_CTRL) && !step(KEY_STOP) && !step(KEY_STOP | 128));
        T(!step(KEY_CTRL | 128) && !step(0x9d) && !rings());
        if (gui) {
            T(!step(KEY_CTRL) && !step(KEY_STOP));
            oracle_discard(); kbd_discard_stop();
            T(!step(KEY_STOP) && !step(KEY_STOP | 128) && !rings());
            tick_count += 50;
            T(!step(KEY_STOP) && !step(KEY_STOP | 128) && !step(KEY_CTRL | 128) && !rings());
        }
    }
    /* Unmatched break: the design suppresses old raw and in2, state stays clear. */
    kbd_hal_capture(KEY_CTRL | 128);
    T(legacy_raw_take() == -1 && !input_key_pressed(KEY_CTRL) && !input_key_mods());
    T(!in2_ring_pop(&out, &seq));
    /* V86 may clear the old bitmap's stale held key; rings remain empty. */
    T(!step(0x1d) && !rings()); native_v86 = 1;
    T(!step(0x9d) && !step(KEY_SHIFT) && !step(KEY_SHIFT | 128));
    T(!step(KEY_CAPS) && !step(KEY_CAPS | 128) && !rings());
    native_v86 = 0;
    T(!input_key_pressed(0x1d) && !input_key_pressed(KEY_SHIFT) && !input_key_mods());
    /* BIOS locks seed only mechanical state; first break clears it without publishing. */
    kbd_hal_init(SHIFT_CAPS | SHIFT_KANA);
    T(input_key_mods() == (SHIFT_CAPS | SHIFT_KANA) && !input_key_pressed(KEY_CAPS));
    T(!in2_ring_pop(&out, &seq));
    kbd_hal_capture(KEY_CAPS | 128); kbd_hal_capture(KEY_KANA | 128);
    T(!input_key_mods() && !in2_ring_pop(&out, &seq) && legacy_raw_take() == -1);
    return 0;
}
void _start(void) { int rc = probe(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); __builtin_unreachable(); }

void bootlog_push(const char *s, u32 n) { (void)s; (void)n; }
int pc98_input_keymap_register(u16 *generation)
{ (void)generation; return -1; }
