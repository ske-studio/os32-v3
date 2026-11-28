#include "sys/dev/usb/hid_kbd.c"
#include "usb_input.h"
#include "input_work.h"
#include "in2_int.h"
#include "idle_work.h"
#include "io_clock.h"
#include "io_work.h"
#include "io_policy.h"
#include "io_kapi.h"
#include "sys/input/input_policy.h"
#include "kbd_contract.h"
#include "av_contract.h"

unsigned input_test_irq;
volatile u32 kctx_irq_depth, kctx_exc_depth, tick_count;
static u32 milliseconds, count, generation = 1, stride;
static unsigned physical, other_work;
static int guest, switch_token, refuse_conversion, reinject;
static unsigned reads;
#ifdef __PPC__
int legacy_active(void) { return guest; }
#else
int v86_is_active(void);
int check_v86(void);
int legacy_active(void) { return guest || v86_is_active(); }
#endif
u32 usbp_ms(void) { return milliseconds; }
int usb_xfer_start(struct usb_xfer *x) { (void)x; return 0; }
u32 in2_time_ms(void) { return milliseconds; }
void input_emit_in2(const InputEvent *e) { (void)e; ++physical; }
void input_state_release(u8 src, u16 gen) { (void)src; (void)gen; }
void ring3_abort_request(void) {}
void appslot_poll_yield_reset(void) {}
static int read_count(u32 *out, u32 *source, u32 *gen)
{
    if (++reads == 3 && switch_token) ++generation;
    *out = count;
    count += stride;
    *source = 1;
    *gen = generation;
    return 0;
}
static int convert(u32 us, u32 source, u32 gen, u32 *out)
{
    if (refuse_conversion || source != 1 || gen != generation) return -1;
    *out = us;
    return 0;
}
static int read_us(u32 *lo, u32 *hi) { *lo = count; *hi = 0; return 0; }
static void tick(unsigned i, u32 now);
static void other(void)
{
    ++other_work;
    if (reinject) {
        reinject = 0;
        tick(2, milliseconds + 30);
    }
    if (io_dispatch(500) != 0) __builtin_trap();
}
static void discard(void) {}
static struct hid_kbd keyboards[4];
static struct usb_iface interfaces[4];
static void report(unsigned i, u8 a, u8 b)
{
    u8 bytes[8] = {0, 0, a, b};
    struct usb_xfer x = {.actual = 8, .ctx = &keyboards[i]};
    x.buf.va = bytes;
    kbd_done(&x);
}
static void tick(unsigned i, u32 now)
{
    milliseconds = now;
    kbd_tick(0, &interfaces[i], now);
}
static int take(void)
{
    int key = legacy_cooked_take();
    return key < 0 ? -1 : key & 255;
}
#include "work_edges.inc"
#include "work_focus.inc"

static int busy_step(void *ctx, struct io_slice *slice)
{
    (void)ctx;
    (void)slice;
    count += IO_PASS_US;
    return IO_MORE;
}

static int check_fair(void)
{
    u8 wire[80];
    unsigned size;
    struct io_desc busy = {.id = IO_SVC_NE2K_BH, .default_tier = 2,
        .default_budget_us = 120, .budget_min_us = 1, .budget_max_us = 500,
        .max_unit_us = 30, .step = busy_step};
    if (io_register(&busy) || io_policy_get(wire, sizeof wire, &size)) return 40;
    wire[20] = 1; /* NE2K now takes the ordinary pass before input. */
    if (io_policy_set(wire, size, 0, 0)) return 41;
    io_request(busy.id);
    tick(2, milliseconds + 30);
    tick(3, milliseconds + 30);
    unsigned served = 0;
    for (unsigned pass = 0; pass < 16; ++pass) {
        idle_work_run();
        if (take() == -1) continue;
        ++served;
        if (take() != -1) return 42;
    }
    if (served != 2 || io_unregister(busy.id) || io_policy_reset(0, 0)) return 43;
    return 0;
}

static int check_clock(void)
{
    tick(2, milliseconds + 30);
    reads = 0;
    switch_token = 1;
    idle_work_run();
    switch_token = 0;
    if (take() != -1) return 34;
    refuse_conversion = 1;
    idle_work_run();
    refuse_conversion = 0;
    if (take() != -1) return 35;
    count = 0xfffffffeu;
    idle_work_run();
    if (take() != 'c') return 36;

    /* This pass expires on the first take, exactly at the 120 us boundary. */
    tick(2, milliseconds + 30);
    stride = 60;
    idle_work_run();
    stride = 0;
    if (take() != -1) return 37;
    reinject = 1;
    idle_work_run();
    if (take() != 'c' || take() != -1) return 38;
    idle_work_run();
    if (take() != 'c' || take() != -1) return 39;
    return 0;
}

static int check_boundaries(void)
{
    KbdPolicyKey event;
    if (ikm_policy_init(IKM_PLATFORM_MAC, 0, &hid_jp_backend, input_policy_pressed) ||
        input_context_first_shell(1)) return 21;
    input_context_discard_register(discard);
    tick(2, milliseconds + 30);
    if (input_context_owner_begin(1, 1)) return 22;
    idle_work_run();
    if (take() != -1) return 23;
    tick(2, milliseconds + 30);
    if (input_context_owner_end(1, 1)) return 24;
    idle_work_run();
    if (take() != -1) return 25;
    tick(2, milliseconds + 30);
    if (input_context_delivery_set(1, 1, 1)) return 26;
    idle_work_run();
    if (take() != -1) return 27;
    tick(2, milliseconds + 30);
    if (input_context_delivery_set(1, 99, 0) == 0) return 28;
    idle_work_run();
    if (take() != 'c') return 29;
    tick(2, milliseconds + 30);
    kbd_set_gui_mode(1);
    idle_work_run();
    if (legacy_raw_take() != -1 || ikm_policy_key_read(1, 1, 1, &event, 1)) return 30;
    tick(2, milliseconds + 30);
    tick(2, milliseconds + 30);
    if (legacy_raw_take() != -1 || ikm_policy_key_read(1, 1, 1, &event, 1)) return 31;
    idle_work_run();
    if (ikm_policy_key_read(1, 1, 1, &event, 1) != 1 || event.keycode != 6 ||
        !(event.sc_down_repeat & KBD_POLICY_SC_REPEAT) || legacy_raw_take() == -1 ||
        ikm_policy_key_read(1, 1, 1, &event, 1) || legacy_raw_take() != -1) return 32;
    tick(2, milliseconds + 30);
    kbd_set_gui_mode(0);
    idle_work_run();
    if (take() != -1) return 33;
    return 0;
}

int probe(void)
{
    const struct io_clock_ops clock = {read_count, convert, read_us};
    u8 wire[80];
    unsigned needed;
    io_clock_set_ops(&clock);
    if (km_backend_register(&hid_jp_backend)) return 1;
    for (unsigned i = 0; i < 4; ++i) {
        interfaces[i].src = usb_input_alloc(USBP_INPUT_KBD);
        if (interfaces[i].src != i + 1) return 2;
        interfaces[i].sc = &keyboards[i];
        keyboards[i].boot.iface = &interfaces[i];
        keyboards[i].repeat = 0xffff;
    }
    milliseconds = 0xffffff00u;
    report(0, 4, 0);
    if (take() != 'a') return 3;
    tick(0, milliseconds + 499);
    if (take() != -1) return 4;
    tick(0, milliseconds + 1);
    if (take() != 'a') return 5; /* synchronous before registration */
    if (input_work_register() || idle_work_register(other)) return 6;
    if (io_policy_get(wire, sizeof wire, &needed) || needed != 80) return 7;
    tick(0, milliseconds + 30);
    tick(0, milliseconds + 30);
    if (take() != -1) return 8;
    input_test_irq = 1;
    idle_work_run();
    kapi_io_pump(500);
    input_test_irq = 0;
    if (take() != -1 || other_work) return 9;
    kctx_irq_depth = 1;
    idle_work_run();
    kapi_io_pump(500);
    kctx_irq_depth = 0;
    if (take() != -1 || other_work) return 10;
    idle_work_run();
    if (take() != 'a' || take() != -1 || physical != 1 || other_work != 1) return 11;
    tick(0, milliseconds + 30);
    report(1, 5, 0);
    if (take() != 'b') return 12;
    idle_work_run();
    if (take() != -1) return 13;
    report(2, 6, 0);
    report(3, 7, 0);
    if (take() != 'c' || take() != 'd') return 14;
    for (unsigned i = 0; i < 4; ++i) tick(i, milliseconds + 500);
    count = 0xfffffff0u;
    stride = 41; /* limit and take each read once; only one fits in 120 us */
    for (unsigned i = 0; i < 4; ++i) {
        kapi_io_pump(500);
        if (take() < 0 || take() != -1) return 15;
    }
    stride = 0;
    tick(0, milliseconds + 30);
    report(0, 0, 0);
    idle_work_run();
    if (take() != -1) return 16;
#ifdef __PPC__
    guest = 1;
    tick(1, milliseconds + 30);
    guest = 0;
    idle_work_run();
    if (take() != -1) return 17;
#endif
    tick(1, milliseconds + 30);
    if (!usb_input_release(USBP_INPUT_KBD, interfaces[1].src)) return 18;
    if (usb_input_alloc(USBP_INPUT_KBD) != interfaces[1].src) return 19;
    idle_work_run();
    if (take() != -1) return 20;
    int rc = check_edges();
    if (!rc) rc = check_clock();
    if (!rc) rc = check_fair();
#ifndef __PPC__
    if (!rc) rc = check_native();
    if (!rc) rc = check_v86();
#endif
    if (!rc) rc = check_boundaries();
    return rc ? rc : check_foreground();
}
