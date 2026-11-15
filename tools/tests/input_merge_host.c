#include "in2_int.h"
#include "kbd.h"
#include "os32_kapi_shared.h"
#define T(c) do { if (!(c)) return __LINE__; } while (0)
static InputEvent events[512];
static u8 old_sc[512], old_down[512], old_mods[512];
static unsigned n, k, repeats, bad_irq;
unsigned test_irq = 1;
static int v86;
static u16 gen[IN2_MAX_DEV];
int legacy_active(void) { return v86; }
u32 in2_time_ms(void) { return 1234; }
void __real_input_emit_in2(const InputEvent *e);
void __wrap_input_emit_in2(const InputEvent *e)
{ bad_irq |= test_irq; events[n++] = *e; __real_input_emit_in2(e); }
void input_emit_legacy_key(u8 sc, u8 down, u8 mods)
{ bad_irq |= test_irq; old_sc[k] = sc; old_down[k] = down; old_mods[k++] = mods; }
void input_emit_legacy_repeat(u8 sc, u8 mods)
{ (void)sc; (void)mods; bad_irq |= test_irq; ++repeats; }
static int key(u8 src, u8 sc, u8 down)
{
    InputEvent e = { .time_ms = 42, .src = src, .generation = gen[src - 1],
                     .type = INPUT_EVENT_KEY, .sc = sc, .down = down };
    return input_capture(&e);
}
static int button(u8 src, u8 mask)
{
    InputEvent e = { .src = src, .generation = gen[src - 1],
                     .type = INPUT_EVENT_BUTTON, .buttons = mask };
    return input_capture(&e);
}
static int probe(void)
{
    u16 spare;
    for (int i = 0; i < IN2_MAX_DEV; ++i)
        T(input_src_register(i >= 14 ? INPUT_KIND_MOUSE : INPUT_KIND_KBD,
                             i ? INPUT_ORIGIN_USB : INPUT_ORIGIN_NATIVE, &gen[i]) == i + 1);
    T(input_src_register(1, 2, &spare) == OS32_ERR_FULL);
    InputEvent captured = { .time_ms = in2_time_ms(), .src = 1, .generation = gen[0],
                            .type = INPUT_EVENT_KEY, .sc = 1, .down = 1 }, out;
    u16 seq;
    T(input_capture(&captured) == 0 && in2_ring_pop(&out, &seq));
    T(out.type == INPUT_EVENT_KEY && out.sc == 1 && out.time_ms == in2_time_ms());
    T(!in2_ring_pop(&out, &seq));
    captured.down = 0; T(input_capture(&captured) == 0 && in2_ring_pop(&out, &seq));
    n = k = 0;
    T(key(1, KEY_SHIFT, 0) == 0 && n == 0 && k == 0);
    T(key(1, KEY_SHIFT, 1) == 0 && key(2, KEY_SHIFT, 1) == 0 && n == 2 && k == 2);
    T(events[0].src == 1 && events[1].src == 2 && events[0].time_ms == 42);
    T(events[1].buttons == SHIFT_SHIFT && old_mods[1] == SHIFT_SHIFT);
    T(key(1, KEY_SHIFT, 1) == 0 && n == 2 && k == 2 && repeats == 1);
    input_repeat(2, gen[1], KEY_SHIFT); T(n == 2 && repeats == 2);
    T(key(1, KEY_SHIFT, 0) == 0 && n == 3 && k == 2 && input_key_pressed(KEY_SHIFT));
    T(key(1, KEY_SHIFT, 0) == 0 && n == 3 && k == 2);
    input_repeat(1, gen[0], KEY_SHIFT); T(repeats == 2);
    input_src_release(2, gen[1]);
    T(n == 4 && k == 3 && !input_key_pressed(KEY_SHIFT) && !input_key_mods());
    T(events[3].src == 2 && !events[3].down && events[3].time_ms == 1234);
    T(old_sc[2] == KEY_SHIFT && !old_down[2] && !old_mods[2]);
    T(input_src_register(1, 2, &gen[1]) == 2);
    for (unsigned sc = 0; sc < 128; ++sc) T(key(3, sc, 1) == 0);
    input_src_release(3, gen[2]);
    for (unsigned sc = 0; sc < 128; ++sc) T(!input_key_pressed(sc));
    T(n == 260 && k == 259);
    T(input_src_register(1, 2, &gen[2]) == 3);
    /* Release must preserve the USB Caps/Kana toggles set by the all-key make. */
    T(input_key_mods() == (SHIFT_CAPS | SHIFT_KANA));
    for (u8 sc = KEY_CAPS; sc <= KEY_KANA; ++sc) {
        u8 bit = sc == KEY_CAPS ? SHIFT_CAPS : SHIFT_KANA;
        T(key(2, sc, 1) == 0 && !(input_key_mods() & bit));
        T(key(2, sc, 0) == 0 && !(input_key_mods() & bit));
        T(key(1, sc, 1) == 0 && (input_key_mods() & bit));
        T(key(1, sc, 1) == 0 && (input_key_mods() & bit));
        T(key(2, sc, 1) == 0 && (input_key_mods() & bit));
        T(key(1, sc, 0) == 0 && (input_key_mods() & bit));
        T(key(3, sc, 1) == 0 && !(input_key_mods() & bit));
        T(key(1, sc, 1) == 0 && (input_key_mods() & bit));
        input_src_release(2, gen[1]); T(input_key_mods() & bit);
        T(input_src_register(1, 2, &gen[1]) == 2);
        T(key(3, sc, 0) == 0 && key(1, sc, 0) == 0 && !(input_key_mods() & bit));
    }
    T(button(15, 3) == 0 && button(16, 1) == 0 && input_buttons() == 3);
    unsigned before = n;
    input_src_release(15, gen[14]); T(input_buttons() == 1 && n == before + 1);
    T(events[before].buttons == 0 && events[before].sc == 3 && events[before].src == 15);
    input_src_release(16, gen[15]); T(input_buttons() == 0);
    T(input_src_register(2, 2, &gen[14]) == 15 && input_src_register(2, 2, &gen[15]) == 16);
    before = n; unsigned old_k = k, old_r = repeats;
    v86 = 1;
    T(key(1, KEY_CTRL, 1) == 0 && key(2, KEY_CAPS, 1) == 0 && button(16, 1) == 0);
    T(input_key_pressed(KEY_CTRL) && input_key_mods() == (SHIFT_CTRL | SHIFT_CAPS));
    input_repeat(1, gen[0], KEY_CTRL); input_src_release(2, gen[1]);
    T(input_key_mods() == (SHIFT_CTRL | SHIFT_CAPS) && input_buttons() == 1);
    T(n == before && k == old_k && repeats == old_r);
    v86 = 0;
    T(key(1, KEY_CTRL, 0) == 0 && n == before + 1 && events[before].buttons == SHIFT_CAPS);
    T(key(4, KEY_SHIFT, 1) == 0 && key(5, KEY_SHIFT, 1) == 0);
    before = n; old_k = k;
    input_src_release(4, gen[3]);
    T(n == before + 1 && k == old_k && input_key_pressed(KEY_SHIFT));
    input_src_release(5, gen[4]); T(k == old_k + 1 && !input_key_pressed(KEY_SHIFT));
    before = n; v86 = 1;
    input_src_release(16, gen[15]); T(n == before && input_buttons() == 0);
    T(key(1, KEY_KANA, 1) == 0 && (input_key_mods() & SHIFT_KANA));
    T(key(1, KEY_KANA, 0) == 0 && !(input_key_mods() & SHIFT_KANA));
    v86 = 0;
    InputEvent invalid = { .src = 0, .generation = gen[0], .type = INPUT_EVENT_KEY, .down = 1 };
    before = n; old_k = k;
    T(input_capture(0) < 0 && input_capture(&invalid) < 0);
    invalid.src = 17; T(input_capture(&invalid) < 0);
    invalid.src = 1; invalid.generation++; T(input_capture(&invalid) < 0);
    invalid.generation = gen[0]; invalid.sc = 128; T(input_capture(&invalid) < 0);
    invalid.sc = 1; invalid.down = 2; T(input_capture(&invalid) < 0);
    invalid.down = 1; invalid.flags = 2; T(input_capture(&invalid) < 0);
    input_src_release(0, gen[0]); input_src_release(17, gen[0]); input_src_release(1, gen[0] + 1);
    input_repeat(0, gen[0], 1); input_repeat(17, gen[0], 1); input_repeat(1, gen[0] + 1, 1);
    T(n == before && k == old_k && !input_key_pressed(128) && !bad_irq && test_irq == 1);
    test_irq = 0; T(key(1, 1, 1) == 0 && test_irq == 0);
    return 0;
}
#ifndef PPC_COMPILE
void _start(void) { int rc = probe(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); __builtin_unreachable(); }
#else
int fixture(void) { return probe(); }
#endif
