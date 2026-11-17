#include "keymap.h"
#include "in2_int.h"
#include "kbd.h"
#define T(c) do { if (!(c)) return __LINE__; } while (0)
static KmEntry entries[256];
static u8 compat[256], bits[32], order[64], no_data[32];
static const KmHidExtensions ext = {order, 0, 0, no_data};
static const KmBackend map = {KM_MAP_HID, 256, entries, compat, bits, &ext};
volatile int kbd_gui_mode;
static int guest, cooked, raw, events;
static u16 entry;
static int raw_br;
int legacy_active(void) { return guest; }
u32 in2_time_ms(void) { return 10; }
int legacy_stop_filter(int sc, int br, int mods)
{ (void)sc; (void)br; (void)mods; return 0; }
void legacy_cooked_push(u16 v) { ++cooked; entry = v; }
void legacy_raw_push(int sc, int br, int mods)
{ (void)sc; (void)mods; ++raw; raw_br = br; }
void input_emit_in2(const InputEvent *event) { (void)event; ++events; }
void input_state_release(u8 src, u16 generation) { (void)src; (void)generation; }
static void reset(void) { cooked = raw = events = 0; }
static u8 src;
static u16 generation;
static int key(u16 code, u8 down, u8 repeat)
{
    KmKey k = { .src = src, .generation = generation, .keycode = code,
                .down = down, .repeat = repeat };
    return input_capture_key(&k);
}
static int check(void)
{
    for (unsigned i = 0; i < 256; ++i) compat[i] = 0xff;
    entries[4].group1[0] = entries[4].group1[1] = 'z';
    entries[5].group1[0] = entries[5].group1[1] = 0xf231; compat[5] = 0x4a;
    entries[6].group1[0] = entries[6].group1[1] = 'x';
    no_data[6 >> 3] = 1u << (6 & 7);
    entries[7].group1[0] = entries[7].group1[1] = 0xf295;
    entries[8].group1[0] = entries[8].group1[1] = 0xf101; compat[8] = KEY_SHIFT;
    entries[9] = entries[8]; compat[9] = KEY_SHIFT;
    entries[10].group1[0] = 'a'; entries[10].group1[1] = 'A'; compat[10] = 0x1d;
    T(!km_backend_register(&map));
    int n = input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_USB, &generation);
    T(n > 0); src = n; T(!km_source_bind(src, generation, KM_MAP_HID));
    T(!key(4, 1, 0) && cooked == 1 && entry == 'z' && !raw && !events);
    T(!key(4, 1, 1) && cooked == 2 && entry == 'z');
    T(!key(4, 0, 0) && cooked == 2);
    reset(); T(!key(5, 1, 0) && cooked == 1 && entry == 0x4a31 && events == 1);
    T(!key(5, 0, 0));
    reset(); T(!key(6, 1, 0) && !key(6, 1, 1) && !key(6, 0, 0) && !cooked);
    T(!key(7, 1, 0) && !key(7, 0, 0) && !cooked);
    guest = 1; reset(); T(!key(8, 1, 0) && !cooked && !raw && !events);
    guest = 0; T(!key(10, 1, 0) && cooked == 1 && entry == 0x1d41);
    T(!key(10, 0, 0) && !key(8, 0, 0));
    kbd_gui_mode = 1; reset();
    T(!key(4, 1, 0) && !key(4, 1, 1) && !key(4, 0, 0) && !raw && !cooked && !events);
    T(!key(8, 1, 0) && !key(9, 1, 0) && raw == 2 && !raw_br);
    reset(); T(!key(8, 0, 0) && !raw && input_key_pressed(KEY_SHIFT));
    T(!key(9, 0, 0) && raw == 1 && raw_br && !input_key_pressed(KEY_SHIFT));
    reset(); T(!key(10, 1, 0) && !key(10, 1, 1) && raw == 2 && !cooked);
    T(!key(8, 1, 0) && !key(9, 1, 0));
    reset(); input_src_release(src, generation);
    T(raw == 2 && raw_br && !cooked && !input_key_pressed(KEY_SHIFT) && !input_key_pressed(0x1d));
    return 0;
}
void _start(void)
{ int rc = check(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
