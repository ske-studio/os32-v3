#include "av_contract.h"
#include "in2_int.h"
#include "kbd.h"
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
    av_focus_set(6);
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
    if (in2_ring_pop(&e, &seq)) return 11;
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
int input_key_pressed(u8 sc) { return sc == 127; }
u8 pointer_buttons_locked(void) { return 3; }
void km_registry_source_released(u8 src, u16 generation) { (void)src; (void)generation; }
void input_state_release(u8 src, u16 generation) { (void)src; (void)generation; }

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
    if (in2_key_state(0) != OS32_ERR_INVAL || in2_key_state(keys) ||
        keys[0] || keys[127] != 1) return 30;
    if (in2_mouse_state(0) != OS32_ERR_INVAL || in2_mouse_state(&mouse) ||
        mouse.dx != 12 || mouse.dy != -5 || mouse.wheel != 8 || mouse.buttons != 3 ||
        in2_mouse_state(&mouse) || mouse.dx || mouse.dy || mouse.wheel) return 31;
    if (in2_status(0) != OS32_ERR_INVAL || in2_status(&status) || status.focus_owner != 1 ||
        status.size != sizeof(status) || status.acquired || status.dropped != in2_ring_dropped()) return 32;
    u32 gen = status.focus_gen;
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
    if (!rc) rc = public_api();
#endif
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
