#include "av_contract.h"
#include "in2_int.h"
#include "kbd.h"
#include "io.h"

static int wm = 1;
static int notified_old, notified_new;

void __real_in2_on_focus(int old, int owner);

void __wrap_in2_on_focus(int old, int owner)
{
    notified_old = old;
    notified_new = owner;
    __real_in2_on_focus(old, owner);
}

volatile int ring3_wm_depth = 1;
int res_owner_get(void) { return 1; }
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
    wm = 0;
    if (in2_excl_wm_state(&state) != OS32_ERR_INVAL) return 15;
    return unmasked_boundary || !_irq_enabled() ? 20 : 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
