#include "keymap.h"
#include "in2_int.h"
#include "kbd.h"
#define T(c) do { if (!(c)) return __LINE__; } while (0)
volatile int kbd_gui_mode;
static int cooked, raw, filtered, physical_ctrl;
static u16 entry;
static int raw_sc, raw_br, raw_mods;
int input_key_pressed(u8 sc) { return sc == KEY_CTRL && physical_ctrl; }
void legacy_cooked_push(u16 v) { entry = v; ++cooked; }
void legacy_raw_push(int sc, int br, int mods)
{ ++raw; raw_sc = sc; raw_br = br; raw_mods = mods; }
int legacy_stop_filter(int sc, int br, int mods)
{ (void)br; (void)mods; return sc == KEY_STOP && filtered; }
static void reset(void) { cooked = raw = 0; entry = 0; }
static int check(void)
{
    KmEvalResult r = { .compat_sc = 0xff, .sym = 'a' };
    input_emit_keymap(&r, 1, 0, 0, 0); T(cooked == 1 && entry == 'a');
    reset(); r.sym = 0; input_emit_keymap(&r, 1, 0, 0, 0); T(!cooked);
    /* Plain high bytes, dead/compose results, and non-byte keysyms never leak. */
    static const u16 high[] = {0x80, 0xff, 0x3042, 0xf295, 0xf001, 0xf500};
    for (unsigned i = 0; i < sizeof(high)/sizeof(high[0]); ++i) {
        reset(); r.sym = high[i]; r.compat_sc = 0xff;
        input_emit_keymap(&r, 1, 1, 0, 0); T(!cooked);
        r.compat_sc = 0x1d; input_emit_keymap(&r, 1, 1, 0, 0);
        T(cooked == 1 && entry == 0x1d00);
    }
    reset(); r.sym = 0xf231; r.compat_sc = 0x4a;
    input_emit_keymap(&r, 1, 1, 0, 0); T(cooked == 1 && entry == 0x4a31);
    for (unsigned sym = 0xf220; sym <= 0xf27f; ++sym) {
        reset(); r.sym = sym; input_emit_keymap(&r, 1, 1, 0, 0);
        T(cooked == 1 && entry == (0x4a00 | (sym & 0x7f)));
    }
    reset(); r.sym = 'a'; r.no_keydata = 1;
    input_emit_keymap(&r, 1, 1, 0, 0); T(!cooked);
    r.no_keydata = 0; r.is_modifier = 1;
    input_emit_keymap(&r, 1, 1, 0, 0); T(!cooked);
    r.is_modifier = 0;
    input_emit_keymap(&r, 1, 1, 0, 1); T(!cooked);
    input_emit_keymap(&r, 0, 1, 0, 0); T(!cooked);
    /* DOS compatibility includes non-character NOP entries and provenance. */
    r.compat_sc = 0x37; r.sym = 3; km_legacy_compat(&r);
    T(r.kind == KM_LEGACY_CONTROL && r.op == 3);
    reset(); input_emit_keymap(&r, 1, 1, 0, 0); T(entry == 0x3703);
    r.kind = KM_CHAR; r.compat_sc = KEY_STOP; r.sym = 0xf500;
    reset(); input_emit_keymap(&r, 1, 1, 0, 0); T(cooked == 1 && entry == 0x6000);
    reset(); physical_ctrl = 1; input_emit_keymap(&r, 1, 1, 0, 0); T(!cooked);
    physical_ctrl = 0; input_emit_keymap(&r, 1, 1, SHIFT_CTRL, 0); T(cooked == 1);
    /* GUI raw preserves break/modifier/Clear, last alias and STOP filtering. */
    kbd_gui_mode = 1; reset(); r.is_modifier = r.no_keydata = 1;
    r.compat_sc = KEY_SHIFT;
    input_emit_keymap(&r, 0, 1, SHIFT_SHIFT, 1);
    T(raw == 1 && raw_sc == KEY_SHIFT && raw_br && raw_mods == SHIFT_SHIFT && !cooked);
    reset(); input_emit_keymap(&r, 0, 0, 0, 0); T(!raw && !cooked);
    r.compat_sc = 0xff; input_emit_keymap(&r, 1, 1, 0, 0); T(!raw && !cooked);
    r.compat_sc = KEY_STOP; filtered = 1;
    input_emit_keymap(&r, 1, 1, 0, 0); T(!raw && !cooked);
    /* Real HID evaluator and generated map: Num always on, no Meta high bit. */
    kbd_gui_mode = 0;
    for (unsigned m = 0; m < 64; ++m) {
        KmState state = { .mods = m };
        T(!km_eval(&hid_jp_backend, 0x59, &state, &r));
        reset(); km_legacy_compat(&r); input_emit_keymap(&r, 1, 1, 0, 0);
        T(cooked == 1 && entry == 0x4a31);
        state.mods = m & (KM_MOD_ALT | KM_MOD_COMMAND | KM_MOD_META);
        T(!km_eval(&hid_jp_backend, 4, &state, &r));
        reset(); input_emit_keymap(&r, 1, 1, 0, 0); T(entry == 0x1d61);
    }
    KmState state = {0};
    T(!km_eval(&hid_jp_backend, 0x53, &state, &r));
    reset(); km_legacy_compat(&r); input_emit_keymap(&r, 1, 1, 0, 0); T(!cooked);
    T(!km_eval(&hid_jp_backend, 0x68, &state, &r));
    input_emit_keymap(&r, 1, 1, 0, 0); T(!cooked);
    return 0;
}
void _start(void)
{ int rc = check(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
