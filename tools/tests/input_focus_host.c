#include "av_contract.h"
#include "in2_int.h"
#include "kbd_contract.h"
#include "keymap.h"
#include "io.h"

static int wm = 1, caller = 1;
static int notified_old, notified_new;

void __real_in2_on_focus(int old, int owner);

void __wrap_in2_on_focus(int old, int owner)
{
    notified_old = old;
    notified_new = owner;
    __real_in2_on_focus(old, owner);
}

volatile int ring3_wm_depth = 1;
int res_owner_get(void) { return caller; }
int ring3_call_from_user(void) { return 0; }
int exec_gui_registered(void) { return wm; }
int pointer_seamless(void) { return 0; }
int legacy_active(void) { return 0; }
void pointer_move(i16 x, i16 y) { (void)x; (void)y; }
unsigned int host_arch_if = 0x202U;
static int unmasked_boundary;
int snapshot_path = -1;
static int snapshot_reads;
int __real_input_key_pressed(u8 sc);
int __wrap_input_key_pressed(u8 sc)
{
    if (snapshot_path >= 0) {
        if (_irq_enabled()) unmasked_boundary = 1;
        ++snapshot_reads;
    }
    return __real_input_key_pressed(sc);
}

u32 in2_time_ms(void)
{
    if (_irq_enabled()) unmasked_boundary = 1;
    return 42;
}

static int lost(int reason)
{
    InputEvent e;
    u16 seq;
    return in2_ring_pop(&e, &seq) && e.type == INPUT_EVENT_LOST && e.sc == reason;
}

static int test(void)
{
    IN2_WmState state;
    InputEvent e = { .type = INPUT_EVENT_KEY };
    u16 seq;
    i32 x, y;
    av_focus_set(5);
    if (notified_old != 0 || notified_new != 5) return 1;
    if (!lost(IN2_LOST_FOCUS) || !lost(IN2_LOST_EXCL)) return 1;
    legacy_raw_push(1, 0, 0);
    legacy_raw_push(KEY_TAB, 1, 0);
    legacy_raw_push(KEY_CTRL, 0, 0);
    legacy_raw_push(KEY_GRPH, 1, 0);
    legacy_raw_push(KEY_STOP, 0, SHIFT_CTRL);
    if (in2_excl_acquire(6, IN2_EXCL_KBD) != OS32_ERR_BUSY ||
        in2_excl_acquire(5, 8) != OS32_ERR_INVAL ||
        in2_excl_acquire(5, IN2_EXCL_KBD | IN2_EXCL_MOUSE)) return 2;
    legacy_raw_push(2, 0, 0);
    if (legacy_raw_take() != KEY_TAB || legacy_raw_take() != (KEY_CTRL | KBD_RAW_MAKE) ||
        legacy_raw_take() != KEY_GRPH ||
        legacy_raw_take() != (KEY_STOP | KBD_RAW_MAKE | (SHIFT_CTRL << KBD_RAW_MOD_SHIFT)) ||
        legacy_raw_take() != -1) return 3;
    if (!lost(IN2_LOST_EXCL) || in2_focus_generation() != 2) return 4;
    mouse_ext_move(3, -4, 0);
    mouse_ext_wheel(-7);
    in2_ring_push(&e);
    in2_reclaim(6);
    if (in2_excl_wm_state(&state) || state.gen != 2 || state.owner != 5 ||
        state.excl != 3 || in2_focus_generation() != 2) return 5;
    mouse_ext_read(&x, &y);
    if (x != 3 || y != -4 || mouse_ext_read_wheel() != -7 ||
        !in2_ring_pop(&e, &seq)) return 6;
    mouse_ext_move(7, 9, 0);
    mouse_ext_wheel(8);
    in2_ring_push(&e);
    legacy_cooked_push('x');
    legacy_raw_push(KEY_STOP, 1, SHIFT_CTRL);
    av_focus_set(6);
    if (legacy_cooked_take() != 'x' ||
        legacy_raw_take() != (KEY_STOP | (SHIFT_CTRL << KBD_RAW_MOD_SHIFT))) return 7;
    if (notified_old != 5 || notified_new != 6) return 7;
    if (!lost(IN2_LOST_FOCUS) || !lost(IN2_LOST_EXCL) ||
        in2_ring_pop(&e, &seq)) return 7;
    mouse_ext_read(&x, &y);
    if (x || y || mouse_ext_read_wheel()) return 8;
    if (in2_excl_wm_state(&state) || state.excl || state.owner ||
        state.gen != 3 || in2_focus_generation() != 3) return 9;
    if (in2_excl_acquire(6, IN2_EXCL_MOUSE)) return 10;
    mouse_ext_move(-5, 6, 0);
    mouse_ext_wheel(9);
    in2_reclaim(6);
    if (!lost(IN2_LOST_EXCL) || in2_ring_pop(&e, &seq)) return 11;
    mouse_ext_read(&x, &y);
    if (x || y || mouse_ext_read_wheel()) return 12;
    if (in2_excl_wm_state(&state) || state.excl || state.owner ||
        state.gen != 5 || in2_focus_generation() != 5) return 13;
    in2_reclaim(6);
    if (in2_focus_generation() != 6) return 14;
    if (in2_excl_acquire(6, IN2_EXCL_KBD) || in2_excl_unacquire(5) != OS32_ERR_BUSY ||
        in2_excl_unacquire(6) || in2_excl_wm_state(&state) || state.owner || state.excl ||
        state.gen != 7 || in2_focus_generation() != 8 ||
        !lost(IN2_LOST_EXCL) || !lost(IN2_LOST_EXCL)) return 15;
    /* Fill the ring across an exclusive boundary. The old owner's pending
     * overflow must not be delivered after focus or foreground teardown. */
    InputEvent key = { .type = INPUT_EVENT_KEY };
    InputEvent stop = { .type = INPUT_EVENT_KEY, .sc = KEY_STOP,
                        .down = 1, .buttons = SHIFT_CTRL };
    const u16 first = 0xffff - IN2_RING_CAPACITY;
    /* Put the reserved STOP at seq 65535 and the boundary at seq 0. */
    for (unsigned i = 0; ; ++i) {
        if (i == 65536 || !in2_ring_push(&key) || !in2_ring_pop(&e, &seq)) return 16;
        if (seq == first - 1) break;
    }
    for (int i = 0; i < IN2_RING_CAPACITY; ++i)
        if (!in2_ring_push(&key)) return 16;
    if (!in2_ring_push(&stop) || in2_excl_acquire(6, IN2_EXCL_MOUSE)) return 16;
    for (int i = 0; i < IN2_RING_CAPACITY - 2; ++i)
        if (!in2_ring_pop(&e, &seq) || e.type != INPUT_EVENT_KEY || e.sc ||
            seq != first + i) return 17;
    if (!in2_ring_pop(&e, &seq) || e.type != INPUT_EVENT_KEY || e.sc != KEY_STOP ||
        !e.down || e.buttons != SHIFT_CTRL || seq != 0xffff) return 17;
    if (!in2_ring_pop(&e, &seq) || e.type != INPUT_EVENT_LOST ||
        e.sc != IN2_LOST_EXCL || seq != 0) return 17;
    e = key;
    av_focus_set(7);
    if (!lost(IN2_LOST_FOCUS) || !lost(IN2_LOST_EXCL)) return 18;
    if (!in2_ring_push(&e) || !in2_ring_pop(&e, &seq) || e.type != INPUT_EVENT_KEY) return 18;
    for (int i = 0; i < IN2_RING_CAPACITY; ++i) in2_ring_push(&e);
    if (in2_ring_push(&e)) return 19;
    in2_reclaim(7);
    if (!in2_ring_push(&e) || !in2_ring_pop(&e, &seq) || e.type != INPUT_EVENT_KEY) return 19;
    /* STOP is oldest. Repeated exclusive transitions fill every other slot. */
    u16 stop_seq = (u16)(seq + 1);
    if (!in2_ring_push(&stop)) return 21;
    for (int i = 0; i < IN2_RING_CAPACITY; ++i)
        if (in2_excl_acquire(7, i % 2 ? IN2_EXCL_MOUSE : IN2_EXCL_KBD)) return 21;
    if (!in2_ring_pop(&e, &seq) || seq != stop_seq || e.type != INPUT_EVENT_KEY ||
        e.sc != KEY_STOP || !e.down || e.buttons != SHIFT_CTRL) return 21;
    for (int i = 2; i <= IN2_RING_CAPACITY; ++i)
        if (!in2_ring_pop(&e, &seq) || seq != (u16)(stop_seq + i) ||
            e.type != INPUT_EVENT_LOST || e.sc != IN2_LOST_EXCL) return 21;
    if (in2_ring_pop(&e, &seq)) return 21;
    wm = 0;
    if (in2_excl_wm_state(&state) != OS32_ERR_INVAL) return 15;
    return unmasked_boundary || !_irq_enabled() ? 20 : 0;
}
volatile u32 tick_count;

static void physical(u8 src, u8 sc, u8 down)
{
    unsigned flags = irq_save();
    InputEvent e = { .src = src, .sc = sc, .down = down, .type = INPUT_EVENT_KEY };
    input_merge_key(&e, input_held_key(src, sc), INPUT_ORIGIN_NATIVE);
    irq_restore(flags);
}
u8 pointer_buttons_locked(void) { return 3; }
void input_state_release(u8 src, u16 generation) { (void)src; (void)generation; }

static int held_boundaries(void)
{
    kbd_gui_mode = 1;
    const int a = 0x1d;
    wm = 1;
    for (int path = 0; path < 4; ++path) {
        av_focus_set(10);
        if (in2_excl_acquire(10, IN2_EXCL_KBD)) return 40;
        in2_ring_clear();
        const int wm_keys[] = { KEY_TAB, KEY_CTRL, KEY_GRPH, KEY_STOP };
        for (unsigned i = 0; i < sizeof(wm_keys)/sizeof(wm_keys[0]); ++i) {
            legacy_raw_push(wm_keys[i], 0, 0);
            if (legacy_raw_take() != (wm_keys[i] | KBD_RAW_MAKE)) return 40;
        }
        physical(1, a, 1);
        physical(2, a, 1);
        if (legacy_raw_take() != -1) return 40;
        InputEvent event;
        u16 seq;
        int makes = 0;
        while (in2_ring_pop(&event, &seq))
            if (event.type == INPUT_EVENT_KEY && event.sc == a && event.down) ++makes;
        if (makes != 2) return 40;
        snapshot_reads = 0;
        snapshot_path = path;
        switch (path) {
        case 0: in2_excl_acquire(10, IN2_EXCL_MOUSE); break;
        case 1: in2_excl_unacquire(10); break;
        case 2: av_focus_set(11); break;
        case 3: in2_reclaim(10); break;
        }
        snapshot_path = -1;
        if (!snapshot_reads || unmasked_boundary) return 41;
        physical(1, a, 0);
        physical(2, a, 1);
        if (legacy_raw_take() != -1) return 41;
        /* A break presented to the raw gate alone cannot alter the snapshot. */
        legacy_raw_push(a, 1, 0);
        physical(2, a, 1);
        if (legacy_raw_take() != a || legacy_raw_take() != -1) return 41;
        /* The last physical break can take the CUI route while no raw
         * consumer is present. The next GUI make still starts a new press. */
        if (path == 3) kbd_gui_mode = 0;
        physical(2, a, 0);
        if (legacy_raw_take() != (kbd_gui_mode ? a : -1)) return 42;
        kbd_gui_mode = 1;
        physical(1, a, 1);
        if (legacy_raw_take() != (a | KBD_RAW_MAKE)) return 42;
        physical(1, a, 0);
        if (legacy_raw_take() != a) return 42;
    }
    return input_key_release_gen(128) || in2_excl_drop_raw(128, 0) ? 43 : 0;
}

static int mapped_hold(void)
{
    u16 native_gen, usb_gen;
    int native = pc98_input_keymap_register(&native_gen);
    int usb = input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_USB, &usb_gen);
    if (native <= 0 || usb <= 0 || km_source_bind(usb, usb_gen, KM_MAP_HID)) return 47;
    KmKey a = { .src = native, .generation = native_gen, .keycode = 0x1d, .down = 1 };
    KmKey hid_a = { .src = usb, .generation = usb_gen, .keycode = 4, .down = 1 };
    if (input_capture_key(&a) || input_capture_key(&hid_a)) return 47;
    av_focus_set(12);
    a.down = 0;
    hid_a.repeat = 1;
    if (input_capture_key(&a) || input_capture_key(&hid_a) || legacy_raw_take() != -1) return 47;
    kbd_gui_mode = 0;
    hid_a.repeat = hid_a.down = 0;
    if (input_capture_key(&hid_a)) return 47;
    kbd_gui_mode = 1;
    a.down = 1;
    if (input_capture_key(&a) || legacy_raw_take() != (0x1d | KBD_RAW_MAKE)) return 47;
    input_src_release(native, native_gen);
    input_src_release(usb, usb_gen);
    if (legacy_raw_take() != 0x1d || legacy_raw_take() != -1) return 47;
    return 0;
}

static int switch_queue(void)
{
    const int tab = KEY_TAB | KBD_RAW_MAKE | (SHIFT_GRPH << KBD_RAW_MOD_SHIFT);
    const int stop = KEY_STOP | KBD_RAW_MAKE | (SHIFT_CTRL << KBD_RAW_MOD_SHIFT);
    legacy_raw_flush();
    for (int i = 0; i < 63; ++i) {
        legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
        legacy_raw_push(1, 0, 0);
    }
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH); /* both seats unavailable */
    legacy_raw_push(KEY_STOP, 0, SHIFT_CTRL);
    if (legacy_raw_take() != tab || legacy_raw_take() != (1 | KBD_RAW_MAKE)) return 44;
    av_focus_set(13);
    for (int i = 1; i < 63; ++i)
        if (legacy_raw_take() != tab || legacy_raw_take() != (1 | KBD_RAW_MAKE)) return 44;
    if (legacy_raw_take() != stop || legacy_raw_take() != -1) return 44;
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
    if (legacy_raw_take() != tab) return 45;
    legacy_raw_reset_dropped();
    for (int i = 0; i < 128; ++i) legacy_raw_push(KEY_CTRL, 0, SHIFT_GRPH);
    if (legacy_raw_dropped() != 1) return 45;
    legacy_raw_push(KEY_STOP, 0, SHIFT_CTRL | SHIFT_GRPH);
    legacy_raw_push(KEY_GRPH, 1, 0);
    for (int i = 0; i < 126; ++i)
        if (legacy_raw_take() != (KEY_CTRL | KBD_RAW_MAKE | (SHIFT_GRPH << KBD_RAW_MOD_SHIFT))) return 45;
    if (legacy_raw_take() != (stop | (SHIFT_GRPH << KBD_RAW_MOD_SHIFT)) ||
        legacy_raw_take() != KEY_GRPH || legacy_raw_take() != -1) return 45;
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
    legacy_raw_push(KEY_STOP, 0, SHIFT_CTRL);
    legacy_raw_discard_stop();
    if (legacy_raw_take() != tab || legacy_raw_take() != (KEY_GRPH | (SHIFT_CTRL << KBD_RAW_MOD_SHIFT)) ||
        legacy_raw_take() != -1) return 45;
    for (int combined = 0; combined <= 1; ++combined) {
        legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
        for (int i = 0; i < KBD_BUF_SIZE - 2; ++i)
            legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
        if (!combined) legacy_raw_push(KEY_GRPH, 1, 0);
        legacy_raw_push(KEY_STOP, 0, SHIFT_CTRL);
        int finish = KEY_GRPH | (combined ? SHIFT_CTRL << KBD_RAW_MOD_SHIFT : 0);
        if (legacy_raw_take() != tab || legacy_raw_take() != tab ||
            legacy_raw_take() != finish || legacy_raw_take() != stop ||
            legacy_raw_take() != -1) return 45;
    }
    /* Seal 125 entries, leaving exactly a start, a finish and STOP. */
    for (int i = 0; i < (KBD_BUF_SIZE - 4) / 2; ++i) {
        legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
        if (i == 0) legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
        legacy_raw_push(KEY_GRPH, 1, 0);
    }
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
    legacy_raw_push(KEY_STOP, 0, SHIFT_CTRL);
    for (int i = 0; i < (KBD_BUF_SIZE - 4) / 2; ++i) {
        if (legacy_raw_take() != tab || (i == 0 && legacy_raw_take() != tab) ||
            legacy_raw_take() != KEY_GRPH) return 45;
    }
    if (legacy_raw_take() != tab || legacy_raw_take() != (KEY_GRPH | (SHIFT_CTRL << KBD_RAW_MOD_SHIFT)) ||
        legacy_raw_take() != stop || legacy_raw_take() != -1) return 45;
    av_focus_set(10);
    in2_excl_acquire(10, IN2_EXCL_KBD);
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
    legacy_raw_push(1, 0, 0);
    if (legacy_raw_take() != tab || legacy_raw_take() != KEY_GRPH ||
        legacy_raw_take() != -1) return 45;
    in2_excl_unacquire(10);
    return 0;
}

static int priority_order(void)
{
    const int grph = SHIFT_GRPH << KBD_RAW_MOD_SHIFT;
    const int tab = KEY_TAB | KBD_RAW_MAKE | grph;
    const int ctrl = KEY_CTRL | KBD_RAW_MAKE | grph;
    const int stop = KEY_STOP | KBD_RAW_MAKE | grph | (SHIFT_CTRL << KBD_RAW_MOD_SHIFT);
    /* With both victims present, prefer the newest ordinary key to a TAB
     * repeat. Their order on readout distinguishes the two choices. */
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
    legacy_raw_push(1, 0, SHIFT_GRPH);
    legacy_raw_push(2, 0, SHIFT_GRPH);
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
    for (int i = 0; i < 123; ++i) legacy_raw_push(KEY_CTRL, 0, SHIFT_GRPH);
    legacy_raw_push(KEY_CTRL, 1, SHIFT_GRPH);
    if (legacy_raw_take() != tab || legacy_raw_take() != (1 | grph | KBD_RAW_MAKE) ||
        legacy_raw_take() != tab) return 46;
    legacy_raw_flush();
    /* A new CTRL removes the newest ordinary item, then a TAB repeat.
     * A subsequent emergency make removes a modifier after those run out. */
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
    legacy_raw_push(1, 0, SHIFT_GRPH);
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
    for (int i = 0; i < 124; ++i) legacy_raw_push(KEY_CTRL, 0, SHIFT_GRPH);
    legacy_raw_push(KEY_CTRL, 1, SHIFT_GRPH);
    legacy_raw_push(KEY_CTRL, 1, SHIFT_GRPH);
    legacy_raw_push(KEY_STOP, 0, SHIFT_CTRL | SHIFT_GRPH);
    legacy_raw_push(KEY_GRPH, 1, 0);
    if (legacy_raw_take() != tab) return 46;
    for (int i = 0; i < 124; ++i)
        if (legacy_raw_take() != ctrl) return 46;
    if (legacy_raw_take() != (KEY_CTRL | grph) || legacy_raw_take() != stop ||
        legacy_raw_take() != KEY_GRPH || legacy_raw_take() != -1) return 46;
    for (int i = 0; i < 128; ++i) legacy_raw_push(1, 0, 0);
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
    legacy_raw_push(KEY_GRPH, 1, 0);
    kbd_raw_purge_keep();
    if (legacy_raw_take() != tab || legacy_raw_take() != tab ||
        legacy_raw_take() != KEY_GRPH || legacy_raw_take() != -1) return 46;
    return 0;
}

/* Ending GRPH or releasing TAB within GRPH closes a repeat group. */
static int previous_tab_press(int end_grph)
{
    const int grph = SHIFT_GRPH << KBD_RAW_MOD_SHIFT;
    const int tab = KEY_TAB | KBD_RAW_MAKE | grph;
    const int boundary = end_grph ? KEY_GRPH : KEY_TAB | grph;
    legacy_raw_flush();
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
    legacy_raw_push(end_grph ? KEY_GRPH : KEY_TAB, 1, end_grph ? 0 : SHIFT_GRPH);
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
    legacy_raw_push(KEY_TAB, 0, SHIFT_GRPH);
    for (int i = 0; i < KBD_BUF_SIZE - 6; ++i)
        legacy_raw_push(KEY_CTRL, 0, SHIFT_GRPH);
    /* The first replacement takes the current repeat. The second must
     * leave the earlier press intact even though its repeat is queued. */
    legacy_raw_push(KEY_CTRL, 1, SHIFT_GRPH);
    legacy_raw_push(KEY_CTRL, 1, SHIFT_GRPH);
    legacy_raw_push(KEY_GRPH, 1, 0);
    if (legacy_raw_take() != tab || legacy_raw_take() != tab ||
        legacy_raw_take() != boundary || legacy_raw_take() != tab) return 47;
    for (int i = 0; i < KBD_BUF_SIZE - 6; ++i)
        if (legacy_raw_take() != (KEY_CTRL | KBD_RAW_MAKE | grph)) return 47;
    if (legacy_raw_take() != KEY_GRPH ||
        legacy_raw_take() != -1) return 47;
    return 0;
}

static int public_api(void)
{
    IN2_PadState pad = { .packet = 19 };
    IN2_PadCaps caps = { .size = 21 };
    IN2_Vibration vibration = { 1, 2 };
    IN2_Status status;
    IN2_WmState state;
    IN2_DevInfo dev;
    IN2_Event event;
    IN2_MouseState mouse = { .dx = 71 };
    u8 keys[128] = { 55 };
    u16 first, second;
    wm = 1;
    av_focus_set(caller);
    if (in2_pad_state(3, &pad) != OS32_ERR_NOTFOUND || pad.packet != 19 ||
        in2_pad_state(4, &pad) != OS32_ERR_INVAL || in2_pad_state(0, 0) != OS32_ERR_INVAL) return 22;
    if (in2_pad_caps(3, &caps) != OS32_ERR_NOTFOUND || caps.size != 21 ||
        in2_pad_caps(4, &caps) != OS32_ERR_INVAL || in2_pad_caps(0, 0) != OS32_ERR_INVAL) return 23;
    if (in2_pad_vibrate(3, &vibration) != OS32_ERR_NOTFOUND ||
        in2_pad_vibrate(4, &vibration) != OS32_ERR_INVAL || in2_pad_vibrate(0, 0) != OS32_ERR_INVAL) return 24;
    if (in2_dev_count() || input_src_register(1, 1, &first) != 1 ||
        input_src_register(2, 2, &second) != 2 || in2_dev_count() != 2) return 25;
    input_src_release(1, first);
    if (in2_dev_count() != 1 || in2_dev_info(0, &dev) || dev.dev != 2 ||
        dev.source != 2 || dev.kind != 2 || dev.size != sizeof(dev) ||
        in2_dev_info(1, &dev) != OS32_ERR_INVAL || in2_dev_info(0, 0) != OS32_ERR_INVAL) return 26;
    if (in2_read(0, 0) || in2_read(0, 1) != OS32_ERR_INVAL ||
        in2_read(&event, 129) != OS32_ERR_INVAL) return 27;
    mouse_ext_move(12, -5, 0);
    mouse_ext_wheel(8);
    caller = 9;
    if (in2_read(&event, 1) != OS32_ERR_BUSY || in2_key_state(keys) != OS32_ERR_BUSY ||
        keys[0] != 55 || in2_mouse_state(&mouse) != OS32_ERR_BUSY || mouse.dx != 71 ||
        in2_pad_vibrate(0, &vibration) != OS32_ERR_BUSY || in2_acquire(1) != OS32_ERR_BUSY ||
        in2_unacquire() != OS32_ERR_BUSY) return 28;
    caller = 1;
    if (in2_read(&event, 1) != 1 || event.type != IN2_EV_LOST ||
        event.u.lost.reason != IN2_LOST_FOCUS) return 29;
    physical(1, 127, 1);
    if (in2_key_state(0) != OS32_ERR_INVAL || in2_key_state(keys) ||
        keys[0] || keys[127] != 1) return 30;
    if (in2_mouse_state(0) != OS32_ERR_INVAL || in2_mouse_state(&mouse) ||
        mouse.dx != 12 || mouse.dy != -5 || mouse.wheel != 8 || mouse.buttons != 3 ||
        in2_mouse_state(&mouse) || mouse.dx || mouse.dy || mouse.wheel) return 31;
    if (in2_status(0) != OS32_ERR_INVAL || in2_status(&status) || status.focus_owner != 1 ||
        status.size != sizeof(status) || status.acquired || status.dropped != in2_ring_dropped()) return 32;
    u32 gen = status.focus_gen;
    if (in2_acquire(IN2_EXCL_KBD)) return 33;
    legacy_raw_push(2, 0, 0);
    legacy_raw_push(KEY_STOP, 0, SHIFT_CTRL);
    if (legacy_raw_take() != (KEY_STOP | KBD_RAW_MAKE | (SHIFT_CTRL << KBD_RAW_MOD_SHIFT)) ||
        legacy_raw_take() != -1 || in2_unacquire()) return 33;
    legacy_raw_push(2, 0, 0);
    if (legacy_raw_take() != (2 | KBD_RAW_MAKE)) return 33;
    gen += 2;
    if (in2_acquire(4) != OS32_ERR_INVAL || in2_acquire(2) || in2_status(&status) ||
        !status.acquired || status.focus_gen != gen + 1) return 33;
    caller = 9;
    if (in2_status(&status) || status.acquired || status.focus_owner != 1) return 33;
    caller = 1;
    if (in2_wm_state(&state) || state.excl != 2 || state.owner != caller ||
        in2_wm_state(0) != OS32_ERR_INVAL) return 34;
    if (in2_unacquire() || in2_status(&status) || status.acquired ||
        status.focus_gen != gen + 2) return 35;
    wm = 0;
    if (in2_wm_state(&state) != OS32_ERR_INVAL) return 36;
    return 0;
}
static int closed_acquire(void)
{
    IN2_WmState before, after;
    IN2_Status status;
    IN2_Event event;
    av_focus_set(caller);
    if (!lost(IN2_LOST_FOCUS) || !lost(IN2_LOST_EXCL) || in2_wm_state(&before)) return 37;
    u32 gen = in2_focus_generation();
    legacy_raw_push(2, 0, 0);
    int rc = in2_acquire(IN2_EXCL_KBD | IN2_EXCL_MOUSE);
    legacy_raw_push(2, 1, 0);
    if (legacy_raw_take() != (2 | KBD_RAW_MAKE) || legacy_raw_take() != 2) return 38;
    if (rc != OS32_ERR_NOSYS || in2_status(&status) || status.acquired ||
        status.focus_gen != gen || in2_wm_state(&after) || after.excl || after.owner ||
        after.gen != before.gen || in2_read(&event, 1) != 0) return 39;
    return 0;
}

void _start(void)
{
#ifdef TEST_CLOSED_ACQUIRE
    int rc = closed_acquire();
#else
    int rc = test();
    if (!rc) rc = held_boundaries();
    if (!rc) rc = mapped_hold();
    if (!rc) rc = switch_queue();
    if (!rc) rc = priority_order();
    if (!rc) rc = previous_tab_press(1);
    if (!rc) rc = previous_tab_press(0);
    if (!rc) rc = public_api();
#endif
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
