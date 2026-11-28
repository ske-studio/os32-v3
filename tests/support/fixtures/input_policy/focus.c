#include "in2_int.h"
#include "keymap.h"
#include "sys/input/input_policy.h"
#include "av_contract.h"
#include "kbd_contract.h"

unsigned input_test_irq;
static unsigned lost, physical;
static u8 reasons[16];
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

/* Send each public read result and raw mirror to the host window pump.
 * Each batch ends with the empty (or exclusive BUSY) read and raw sentinel. */
extern int write_gui_raw(const int *);
static void window_batch(int read_raw)
{
    KbdPolicyKey key;
    int n;
    do {
        n = ikm_policy_key_read(1, 1, 1, &key, 1);
        write_gui_raw(&n);
        if (n == 1) {
            for (unsigned i = 0; i < sizeof key; i += sizeof(int)) {
                int word;
                __builtin_memcpy(&word, (const u8 *)&key + i, sizeof word);
                write_gui_raw(&word);
            }
        }
    } while (n == 1);
    do {
        n = read_raw ? legacy_raw_take() : -1;
        write_gui_raw(&n);
    } while (n >= 0);
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
    if (raw < 0 || (raw & KBD_RAW_KEY_MAKE_MASK) !=
           (hid_jp_backend.compat_sc[4] | KBD_RAW_MAKE) || legacy_raw_take() != -1) return 18;
    if (captured(1, 0) || ikm_policy_init(IKM_PLATFORM_MAC, 0, &hid_jp_backend,
                                           input_policy_pressed) ||
        input_context_owner_register(1, 1, 0, 0) || input_context_owner_select(1, 1))
        return 19;
    KbdPolicyKey out;
    if (in2_acquire(IN2_EXCL_KBD) || captured(1, 1) || captured(1, 0) ||
        in2_unacquire() || ikm_policy_key_read(1, 1, 1, &out, 1)) return 20;
    if (captured(1, 1) || ikm_policy_key_read(1, 1, 1, &out, 1) != 1 ||
        out.keysym != 'a' || out.sc_down_repeat != (0x1d | KBD_POLICY_SC_DOWN) ||
        out.src_generation != 0x101 || out.map_generation != ikm_policy_generation() ||
        ikm_policy_key_read(1, 1, 1, &out, 1)) return 21;
    KmKey a = {.src = 1, .generation = 1, .keycode = 4, .down = 1, .repeat = 1};
    /* Keep the next break when repeat occupied every seat. */
    for (unsigned i = 0; i < 129; ++i)
        if (input_capture_key(&a)) return 22;
    if (captured(1, 0)) return 23;
    for (unsigned i = 0; i < 127; ++i)
        if (ikm_policy_key_read(1, 1, 1, &out, 1) != 1 ||
            !(out.sc_down_repeat & KBD_POLICY_SC_REPEAT)) return 24;
    if (ikm_policy_key_read(1, 1, 1, &out, 1) != 1 || out.sc_down_repeat != 0x1d)
        return 25;
    /* HID F13 has no compatibility scan. Bind it directly to SWITCH_APP. */
    const u8 binding[] = {'I','K','M','1',2,0,16,0,1,0,0,0,0,0,0,0,
        8,4,4,0,0x68,0,2,0,KBD_POLICY_ACTION_SWITCH_APP,0,0,0,0,0,0,0};
    if (ikm_policy_set(binding, sizeof binding, 1, 1)) return 26;
    KmKey f13 = {.src = 1, .generation = 1, .keycode = 0x68, .down = 1};
    if (input_capture_key(&f13) || input_capture_key(&f13)) return 27;
    f13.down = 0;
    if (input_capture_key(&f13) || ikm_policy_key_read(1, 1, 1, &out, 1) != 1 ||
        out.sc_down_repeat != (0xff | KBD_POLICY_SC_DOWN) ||
        out.action != KBD_POLICY_ACTION_SWITCH_APP || ikm_policy_key_read(1, 1, 1, &out, 1))
        return 28;
    if (captured(1, 1) || captured(1, 0) || ikm_policy_reset(1, 1) ||
        ikm_policy_key_read(1, 1, 1, &out, 1)) return 29;
    if (captured(1, 1) || captured(1, 0) || in2_acquire(IN2_EXCL_MOUSE) ||
        ikm_policy_key_read(1, 1, 1, &out, 1) || in2_unacquire()) return 43;
    /* Finish GRPH+Tab in the normal WM before acquiring with Alt held again. */
    KmKey alt = {.src = 1, .generation = 1, .keycode = 0xe2, .down = 1};
    KmKey tab = {.src = 1, .generation = 1, .keycode = 0x2b, .down = 1};
    const u8 alt_tab[] = {'I','K','M','1',2,0,16,0,1,0,0,0,0,0,0,0,
        8,4,4,0,0x2b,0,2,0,KBD_POLICY_ACTION_SWITCH_APP,0,0,0,KM_MOD_ALT,0,0,0};
    if (ikm_policy_set(alt_tab, sizeof alt_tab, 1, 1) ||
        input_capture_key(&alt) || input_capture_key(&tab)) return 30;
    alt.down = tab.down = 0;
    if (input_capture_key(&tab) || input_capture_key(&alt)) return 30;
    window_batch(0);  /* The normal WM reads only Key. Leave its mirrors queued. */
    alt.down = 1;
    if (input_capture_key(&alt) || in2_acquire(IN2_EXCL_KBD) ||
        captured(1, 1) || captured(1, 0)) return 31;
    window_batch(1);
    tab.down = 1;
    if (input_capture_key(&tab) || in2_acquire(IN2_EXCL_KBD)) return 32;
    alt.down = tab.down = 0;
    if (input_capture_key(&tab) || input_capture_key(&alt)) return 33;
    window_batch(1);
    if (in2_unacquire() || captured(1, 1)) return 34;
    window_batch(1);
    for (unsigned i = 0; i < 129; ++i)
        if (input_capture_key(&a)) return 35;
    if (captured(1, 0)) return 36;
    window_batch(1);
    if (ikm_policy_set(binding, sizeof binding, 1, 1)) return 37;
    f13.down = 1;
    if (input_capture_key(&f13) || input_capture_key(&f13)) return 38;
    f13.down = 0;
    if (input_capture_key(&f13)) return 39;
    window_batch(1);
    if (captured(1, 1) || captured(1, 0) || ikm_policy_reset(1, 1)) return 40;
    window_batch(1);
    /* Unmapped HID F13 can contribute text without a window scan. */
    const u8 text[] = {'I','K','M','1',2,0,16,0,1,0,0,0,0,0,0,0,
        2,0,2,0,0x68,0,1,0,'q',0,0,0,0,0,0,0};
    if (ikm_policy_set(text, sizeof text, 1, 1)) return 41;
    f13.down = 1;
    if (input_capture_key(&f13)) return 42;
    window_batch(1);
    return 0;
}
