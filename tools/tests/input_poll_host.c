#include "in2_int.h"
#include "input_unused_key_host.h"
#include "mouse.h"
#include "pc98.h"
#define T(x) do { if (!(x)) return (__LINE__ - 1) % 127 + 1; } while (0)
unsigned test_irq = 1;
static u8 poll_samples[4], poll_pos, poll_stop;
static int poll_v86, poll_seamless;
static int poll_inject_irq;
static i16 poll_abs_x = 32767, poll_abs_y;
static unsigned poll_reads, poll_events;
static unsigned poll_check_io, poll_irq_bad;
static int poll_abs_ok = 1;
static InputEvent poll_last, poll_prior;
void base_mouse_init(void);
void base_mouse_poll(MouseState *s);
void base_mouse_set_bounds(i16, i16, i16, i16);
void ref_mouse_ext_mode(int enabled) { (void)enabled; }
void test_irq_restore(unsigned f)
{
    test_irq = f;
    if (f && poll_inject_irq) { poll_inject_irq = 0; mouse_irq_handler(); }
}
void ref_mouse_init(void);
void ref_mouse_poll(MouseState *s);
void ref_mouse_set_bounds(i16, i16, i16, i16);
void ref_mouse_irq_handler(void);
void ref_mouse_position(i16 *, i16 *);
void ref_mouse_hal_init(void) {}
void ref_mouse_hal_capture(i8 x, i8 y, u8 b) { (void)x; (void)y; (void)b; }
unsigned test_inp(unsigned port) { (void)port; if (poll_check_io && !test_irq) ++poll_irq_bad; return poll_samples[poll_pos++ % 4]; }
void test_outp(unsigned port, unsigned value) { (void)port; (void)value; }
int legacy_active(void) { return poll_v86; }
u32 in2_time_ms(void) { return 50; }
void input_emit_in2(const InputEvent *e) { poll_prior = poll_last; poll_last = *e; ++poll_events; }
int np2_detect(void) { return poll_seamless; }
int seamless_mouse_init(void) { return 1; }
int seamless_mouse_poll(i16 *x, i16 *y) { if (poll_check_io && !test_irq) ++poll_irq_bad; ++poll_reads; *x = poll_abs_x; *y = poll_abs_y; return poll_abs_ok; }
void irq_enable(u8 irq) { (void)irq; }
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
void kbd_stop_mouse_sample(u8 buttons) { poll_stop = buttons; }
int tvram_reverse_cell(int x, int y) { (void)x; (void)y; return 1; }
static void poll_sample(i8 x, i8 y, u8 raw)
{
    poll_samples[0] = raw | ((u8)x & 15); poll_samples[1] = (u8)x >> 4;
    poll_samples[2] = (u8)y & 15; poll_samples[3] = (u8)y >> 4;
    poll_pos = 0; ref_mouse_irq_handler();
    poll_pos = 0; mouse_irq_handler();
}
static int poll_equal(MouseState a, MouseState b)
{
    return a.x == b.x && a.y == b.y && a.dx == b.dx && a.dy == b.dy &&
           a.buttons == b.buttons && a.mode == b.mode;
}
static int probe(void)
{
    MouseState a, b; i16 x, y, rx, ry; i32 ax, ay;
    ref_mouse_init(); mouse_init();
    ref_mouse_poll(&a); mouse_poll(&b);
    T(poll_equal(a, b) && b.x == 319 && b.y == 199 && !b.dx && !b.dy);
    poll_sample(5, -3, MOUSE_RIGHT | MOUSE_MIDDLE);
    poll_sample(7, 4, MOUSE_RIGHT | MOUSE_MIDDLE);
    mouse_ext_read(&ax, &ay); T(ax == 12 && ay == 1);
    ref_mouse_poll(&a); mouse_poll(&b);
    T(poll_equal(a, b) && b.x == 331 && b.y == 200 && b.dx == 12 && b.dy == 1 && poll_stop == MOUSE_BTN_LEFT);
    ref_mouse_poll(&a); mouse_poll(&b); T(poll_equal(a, b) && !b.dx && !b.dy);
    poll_sample(-4, 2, MOUSE_LEFT | MOUSE_MIDDLE);
    ref_mouse_poll(0); mouse_poll(0);
    ref_mouse_poll(&a); mouse_poll(&b); T(poll_equal(a, b) && !b.dx && b.buttons == MOUSE_BTN_RIGHT);
    mouse_ext_read(&ax, &ay); T(ax == -4 && ay == 2);
    ref_mouse_set_bounds(10, 20, 50, 60); mouse_set_bounds(10, 20, 50, 60);
    ref_mouse_poll(&a); mouse_poll(&b); T(poll_equal(a, b) && b.x == 50 && b.y == 60);
    poll_sample(-100, -100, MOUSE_LEFT | MOUSE_MIDDLE | MOUSE_RIGHT);
    ref_mouse_poll(&a); mouse_poll(&b); T(poll_equal(a, b) && b.x == 10 && b.y == 20 && !b.buttons);
    u16 generation;
    int usb = input_src_register(INPUT_KIND_MOUSE, INPUT_ORIGIN_USB, &generation);
    T(usb == 2);
    InputEvent e = { .src = (u8)usb, .generation = generation,
                     .type = INPUT_EVENT_BUTTON, .buttons = MOUSE_BTN_LEFT };
    T(!input_capture(&e)); mouse_poll(&b); T(b.buttons == MOUSE_BTN_LEFT);
    poll_sample(0, 0, MOUSE_RIGHT | MOUSE_MIDDLE);
    input_src_release((u8)usb, generation);
    ref_mouse_poll(&a); mouse_poll(&b); T(poll_equal(a, b) && b.buttons == MOUSE_BTN_LEFT);
    poll_sample(0, 0, MOUSE_LEFT | MOUSE_MIDDLE | MOUSE_RIGHT);
    ref_mouse_poll(&a); mouse_poll(&b); T(poll_equal(a, b) && !b.buttons);
    /* PM: retain reversed bounds; compare the old setter's immediate clamp. */
    ref_mouse_set_bounds(40, 50, 10, 20); mouse_set_bounds(40, 50, 10, 20);
    ref_mouse_position(&rx, &ry); pointer_position(&x, &y);
    T(x == rx && y == ry && x == 40 && y == 50);
    ref_mouse_set_bounds(40, 50, 10, 20); mouse_set_bounds(40, 50, 10, 20);
    ref_mouse_position(&rx, &ry); pointer_position(&x, &y);
    T(x == rx && y == ry && x == 10 && y == 20);
    ref_mouse_set_bounds(0, 0, 639, 399); mouse_set_bounds(0, 0, 639, 399);
    poll_v86 = 1; poll_sample(4, 2, MOUSE_RIGHT | MOUSE_MIDDLE); poll_v86 = 0;
    ref_mouse_poll(&a); mouse_poll(&b);
    T(a.dx == 4 && a.dy == 2 && !b.dx && !b.dy && a.x == b.x && a.y == b.y && b.buttons == MOUSE_BTN_LEFT);
    /* An IRQ pending during the snapshot runs only after all fields are read. */
    mouse_poll(&b);
    poll_samples[0] = MOUSE_LEFT | MOUSE_MIDDLE | MOUSE_RIGHT | 3;
    poll_samples[1] = poll_samples[2] = poll_samples[3] = 0;
    poll_pos = 0; poll_inject_irq = 1;
    mouse_poll(&b); T(b.x == 14 && b.dx == 0);
    mouse_poll(&b); T(b.x == 17 && b.dx == 3);
    /* Exact ef67e51c seamless body: first baseline, movement, failed read,
     * bounds (including reversed), NULL read, and seamless/bus/seamless reset. */
    poll_seamless = 1; base_mouse_init(); mouse_init();
    poll_samples[0] = MOUSE_LEFT | MOUSE_MIDDLE | MOUSE_RIGHT;
    poll_pos = 0; base_mouse_poll(&a); poll_pos = 0; unsigned reads = poll_reads, baseline = poll_events;
    poll_check_io = 1; mouse_poll(&b); poll_check_io = 0;
    T(!poll_irq_bad && poll_reads == reads + 1 && poll_equal(a, b) && b.x == 639 && !b.y && !b.dx && !b.dy);
    T(poll_events == baseline);
    mouse_ext_read(&ax, &ay); /* discard earlier bus movement */
    poll_abs_x = 16384; poll_abs_y = 32767; unsigned n = poll_events;
    poll_pos = 0; base_mouse_poll(&a); poll_pos = 0; mouse_poll(&b);
    T(poll_equal(a, b) && b.x == 319 && b.y == 399 && b.dx == -320 && b.dy == 399);
    T(poll_events == n + 2 && poll_prior.type == INPUT_EVENT_ABS && poll_prior.x == 16384 && poll_prior.y == 32767);
    T(poll_last.type == INPUT_EVENT_MOVE && poll_last.flags == INPUT_EVENT_SEAMLESS && poll_last.x == -320 && poll_last.y == 399);
    T(poll_prior.src == poll_last.src && poll_prior.generation == poll_last.generation && poll_last.time_ms == 50);
    mouse_ext_read(&ax, &ay); T(ax == -320 && ay == 399);
    poll_abs_ok = 0; poll_samples[0] = MOUSE_RIGHT | MOUSE_MIDDLE;
    poll_pos = 0; base_mouse_poll(&a); poll_pos = 0; mouse_poll(&b);
    T(poll_equal(a, b) && !b.dx && !b.dy && b.buttons == MOUSE_BTN_LEFT && poll_stop == MOUSE_BTN_LEFT);
    const u8 raw_buttons[] = { MOUSE_LEFT | MOUSE_MIDDLE, MOUSE_LEFT | MOUSE_RIGHT,
                               MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE };
    const u8 masks[] = { MOUSE_BTN_RIGHT, MOUSE_BTN_MIDDLE, 0 };
    for (unsigned i = 0; i < 3; ++i) {
        poll_samples[0] = raw_buttons[i];
        poll_pos = 0; base_mouse_poll(&a); poll_pos = 0; mouse_poll(&b);
        T(poll_equal(a, b) && b.buttons == masks[i] && poll_stop == masks[i]);
    }
    poll_abs_ok = 1; poll_abs_x = 32767; poll_abs_y = 0;
    base_mouse_set_bounds(40, 50, 10, 20); mouse_set_bounds(40, 50, 10, 20);
    poll_pos = 0; base_mouse_poll(&a); poll_pos = 0; mouse_poll(&b);
    T(poll_equal(a, b) && b.x == 40 && b.y == 20 && b.dx == -309 && b.dy == -349);
    poll_pos = 0; base_mouse_poll(&a); poll_pos = 0; mouse_poll(&b);
    T(poll_equal(a, b) && !b.dx && !b.dy);
    base_mouse_set_bounds(0, 0, 639, 479); mouse_set_bounds(0, 0, 639, 479);
    poll_abs_x = 0; poll_abs_y = 32767;
    poll_pos = 0; base_mouse_poll(0); poll_pos = 0; reads = poll_reads; mouse_poll(0);
    T(poll_reads == reads + 1);
    poll_pos = 0; base_mouse_poll(&a); poll_pos = 0; mouse_poll(&b);
    T(poll_equal(a, b) && !b.dx && !b.dy && b.y == 479);
    poll_seamless = 0; mouse_init(); poll_sample(3, -2, MOUSE_LEFT | MOUSE_RIGHT | MOUSE_MIDDLE);
    mouse_poll(&b); T(b.dx == 3 && b.dy == -2 && b.mode == MOUSE_MODE_BUS);
    poll_seamless = 1; base_mouse_init(); mouse_init(); poll_abs_x = 32767; poll_abs_y = 0;
    poll_pos = 0; base_mouse_poll(&a); poll_pos = 0; mouse_poll(&b);
    T(poll_equal(a, b) && !b.dx && !b.dy && b.x == 639 && !b.y);
    return 0;
}
void _start(void) { int rc = probe(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); __builtin_unreachable(); }
