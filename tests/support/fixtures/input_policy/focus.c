#include "in2_int.h"
#include "keymap.h"
#include "av_contract.h"
#include "kbd_contract.h"

unsigned input_test_irq;
static unsigned lost, physical;
static u8 reasons[8];
int res_owner_get(void) { return 1; }
int av_focus_owner(void) { return 1; }
int av_focus_is_wm(void) { return 1; }
int legacy_active(void) { return 0; }
int __real_input_key_pressed(u8 sc);
int __wrap_input_key_pressed(u8 sc)
{
    if (!input_test_irq) __builtin_trap();
    return __real_input_key_pressed(sc);
}
void input_emit_in2(const InputEvent *e) { if (e->sc == 42) ++physical; }
volatile u32 tick_count;
u32 in2_time_ms(void) { return tick_count; }
void appslot_poll_yield_reset(void) {}
int input_src_info(u8 src, u16 gen, u8 *kind, u8 *origin)
{
    *kind = INPUT_KIND_KBD;
    *origin = INPUT_ORIGIN_USB;
    return (src == 1 || src == 2) && gen == 1;
}

void in2_ring_lost(u8 reason) { reasons[lost++] = reason; }
void in2_ring_clear(void) { lost = 0; }
void mouse_ext_read(i32 *x, i32 *y) { *x = *y = 0; }
i32 mouse_ext_read_wheel(void) { return 0; }

static void key(u8 src, u8 down)
{
    InputEvent event = {.src = src, .sc = 42, .down = down};
    input_merge_key(&event, input_held_key(src, 42), INPUT_ORIGIN_USB);
}

static int captured(u8 src, u8 down)
{
    KmKey k = { .src = src, .generation = 1, .keycode = 4, .down = down };
    return input_capture_key(&k);
}

int probe(void)
{
    IN2_WmState state;
    kbd_set_gui_mode(1);
    if (in2_acquire(IN2_EXCL_KBD) || !in2_excl_drop_raw(42, 0)) return 1;
    key(1, 1);
    key(2, 1);
    if (legacy_raw_take() != -1 || physical != 2 || in2_excl_drop_raw(KEY_STOP, 0) || in2_excl_drop_raw(KEY_CTRL, 0) ||
        in2_excl_drop_raw(KEY_GRPH, 0) || in2_excl_drop_raw(KEY_TAB, 0)) return 2;
    if (in2_acquire(IN2_EXCL_MOUSE) || !in2_excl_drop_raw(42, 0)) return 3;
    key(1, 0);
    if (!in2_excl_drop_raw(42, 0)) return 4;
    kbd_set_gui_mode(0);
    key(2, 0);
    kbd_set_gui_mode(1);
    key(1, 1);
    if ((legacy_raw_take() & KBD_RAW_KEY_MAKE_MASK) != (42 | KBD_RAW_MAKE)) return 13;
    key(1, 0);
    legacy_raw_take();
    if (in2_excl_drop_raw(42, 0) || in2_unacquire()) return 5;
    if (in2_wm_state(&state) || state.excl || state.gen != 3 || lost != 3) return 6;
    in2_on_focus(1, 2);
    if (lost != 2 || reasons[0] != IN2_LOST_FOCUS || reasons[1] != IN2_LOST_EXCL) return 7;
    if (in2_acquire(IN2_EXCL_KBD)) return 8;
    in2_reclaim(1);
    if (lost != 1 || reasons[0] != IN2_LOST_EXCL || in2_excl_keyboard()) return 9;
    for (unsigned i = 0; i < KBD_BUF_SIZE; ++i) legacy_raw_push(42, 0, 0);
    for (unsigned i = 0; i < 2; ++i) {
        legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
        legacy_raw_push(KEY_GRPH, 1, 0);
    }
    legacy_raw_push(KEY_STOP, 0, SHIFT_CTRL);
    kbd_raw_purge_keep();
    for (unsigned i = 0; i < 2; ++i) {
        if ((legacy_raw_take() & KBD_RAW_KEY_MAKE_MASK) != (KEY_TAB | KBD_RAW_MAKE)) return 10;
        if ((legacy_raw_take() & KBD_RAW_KEY_MAKE_MASK) != KEY_GRPH) return 11;
    }
    if ((legacy_raw_take() & KBD_RAW_KEY_MAKE_MASK) != (KEY_STOP | KBD_RAW_MAKE)) return 12;
    if (legacy_raw_take() != -1) return 14;
    if (km_backend_register(&hid_jp_backend) || km_source_bind(1, 1, KM_MAP_HID) ||
        km_source_bind(2, 1, KM_MAP_HID) || in2_acquire(IN2_EXCL_KBD) ||
        captured(1, 1) || captured(2, 1) || legacy_raw_take() != -1 ||
        in2_unacquire() || captured(1, 0) || legacy_raw_take() != -1) return 15;
    kbd_set_gui_mode(0);
    if (captured(2, 0)) return 16;
    kbd_set_gui_mode(1);
    if (captured(1, 1)) return 17;
    int raw = legacy_raw_take();
    return raw < 0 || (raw & KBD_RAW_KEY_MAKE_MASK) !=
           (hid_jp_backend.compat_sc[4] | KBD_RAW_MAKE) || legacy_raw_take() != -1;
}
