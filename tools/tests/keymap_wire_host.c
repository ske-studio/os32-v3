#include "keymap.h"
#include "in2_int.h"
#include "kbd.h"
#include "os32_kapi_shared.h"
#include "s3_expected.h"

volatile int kbd_gui_mode;
static int guest, cooked, raw, events, filtered;
static u16 entry;
static int raw_sc, raw_break;
unsigned wire_snapshot_calls, wire_snapshot_visits, wire_boot_visits;
int legacy_active(void) { return guest; }
u32 in2_time_ms(void) { return 123; }
int legacy_stop_filter(int sc, int br, int mods)
{
    if (sc == KEY_STOP && (mods & SHIFT_CTRL)) { ++filtered; return 1; }
    (void)br;
    return 0;
}
void legacy_cooked_push(u16 value) { entry = value; ++cooked; }
void legacy_raw_push(int sc, int br, int mods)
{
    raw_sc = sc; raw_break = br; ++raw; (void)mods;
}
void input_emit_in2(const InputEvent *e)
{
    if (e->time_ms != 123) __builtin_trap();
    ++events;
}
void input_state_release(u8 src, u16 generation) { (void)src; (void)generation; }
void wire_old_emit(u8 sc, u8 down, u8 mods);
static void reset(void) { cooked = raw = events = filtered = 0; entry = 0; }
static int key(u8 src, u16 gen, u16 code, u8 down, u8 repeat)
{
    KmKey k = { .src = src, .generation = gen, .keycode = code,
                .down = down, .repeat = repeat };
    return input_capture_key(&k);
}
static const u8 modifiers[5] = {KEY_SHIFT, KEY_CTRL, KEY_GRPH, KEY_CAPS, KEY_KANA};
static int check(void)
{
    u16 gen, usb_gen, extra_gen[14];
    u8 extra[14];
    KmState state;
    KmSource source;
    int src = pc98_input_keymap_register(&gen);
    if (src <= 0 || km_source_lookup(src, gen, &source) || source.map != &pc98_jp_backend) return 1;
    if (pc98_input_keymap_register(&usb_gen) != OS32_ERR_INVAL) return 2;
    int usb = input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_USB, &usb_gen);
    if (usb <= 0 || km_source_bind(usb, usb_gen, KM_MAP_HID)) return 3;
    /* Compare the actual capture exit with the pinned pre-S0d character exit.
     * Each key can change physical modifiers itself, so sample after capture. */
    for (unsigned mask = 0; mask < 32; ++mask) {
        for (unsigned sc = 0; sc < 128; ++sc) {
            for (unsigned m = 0; m < 5; ++m)
                if (mask & (1u << m)) if (key(src, gen, modifiers[m], 1, 0)) return 4;
            reset();
            if (key(src, gen, sc, 1, 0)) return 5;
            int actual_count = cooked;
            u16 actual_entry = entry;
            u8 mods = input_key_mods();
            reset(); wire_old_emit(sc, 1, mods);
            if (cooked != actual_count || (cooked && (((u16)sc << 8) | s3_expected[sc][mods]) != actual_entry)) return 6;
            if (sc == 0x35 && entry != 0x3500) return 7;
            if (sc == 0x51 && entry != 0x5100) return 8;
            if (sc == KEY_STOP && !(mods & SHIFT_CTRL) && entry != 0x6000) return 9;
            if (key(src, gen, sc, 0, 0)) return 10;
            for (unsigned m = 0; m < 5; ++m)
                if (key(src, gen, modifiers[m], 0, 0)) return 11;
        }
    }
    /* One held array: duplicate make/repeat cannot increment alias counts. */
    kbd_gui_mode = 1; reset();
    if (key(usb, usb_gen, 0xe1, 1, 0) || key(usb, usb_gen, 0xe5, 1, 0)) return 12;
    if (!input_key_pressed(KEY_SHIFT)) return 13;
    reset();
    if (key(usb, usb_gen, 0xe1, 0, 0) || raw || !input_key_pressed(KEY_SHIFT)) return 14;
    if (key(usb, usb_gen, 0xe5, 0, 0) || raw != 1 || raw_sc != KEY_SHIFT || !raw_break) return 15;
    reset();
    key(src, gen, KEY_CTRL, 1, 0); key(src, gen, KEY_STOP, 1, 0);
    if (filtered != 1) return 16;
    key(src, gen, KEY_STOP, 0, 0); key(src, gen, KEY_CTRL, 0, 0);
    kbd_gui_mode = 0;
    reset(); guest = 1;
    if (key(src, gen, 0x1d, 1, 0) || cooked || raw || events || !input_key_pressed(0x1d)) return 17;
    key(src, gen, 0x1d, 0, 0); guest = 0;
    reset();
    if (key(usb, usb_gen, 0x04, 1, 1) != OS32_ERR_INVAL || cooked || input_key_pressed(0x1d)) return 18;
    if (key(src, gen + 1, 0x1d, 1, 0) != OS32_ERR_INVAL || key(src, gen, 128, 1, 0) != OS32_ERR_INVAL) return 19;
    for (unsigned code = 0; code < 4; ++code)
        if (key(usb, usb_gen, code, 1, 0) != OS32_ERR_INVAL) return 26;
    key(usb, usb_gen, 0xe1, 1, 0);
    key(src, gen, 0x1d, 1, 0);
    km_state_snapshot(&state);
    if (!(state.mods & KM_MOD_SHIFT) || entry != 0x1d41) return 20;
    key(src, gen, 0x1d, 0, 0);
    /* Source release runs while binding is live, then removes effects/binding. */
    input_src_release(usb, usb_gen);
    km_state_snapshot(&state);
    if (input_key_pressed(KEY_SHIFT) || state.mods || !km_source_lookup(usb, usb_gen, &source)) return 21;
    /* Populate all sixteen slots for a measured worst-case snapshot. */
    for (unsigned i = 0; i < 14; ++i) {
        int n = input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_USB, &extra_gen[i]);
        if (n <= 0 || km_source_bind(n, extra_gen[i], KM_MAP_HID)) return 22;
        extra[i] = n;
        key(n, extra_gen[i], 0x04, 1, 0);
    }
    usb = input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_USB, &usb_gen);
    if (usb <= 0 || km_source_bind(usb, usb_gen, KM_MAP_HID)) return 23;
    wire_snapshot_calls = wire_snapshot_visits = wire_boot_visits = 0;
    key(src, gen, 0x1d, 1, 0);
    if (wire_snapshot_calls != 2 || wire_snapshot_visits != 32 || wire_boot_visits != 32) return 24;
    for (unsigned i = 0; i < 14; ++i) input_src_release(extra[i], extra_gen[i]);
    input_src_release(usb, usb_gen); input_src_release(src, gen);
    if (input_key_pressed(0x1d)) return 25;
    return 0;
}
void _start(void)
{
    int rc = check();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
