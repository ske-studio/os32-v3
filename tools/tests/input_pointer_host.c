#include "in2_int.h"
#define T(x) do { if (!(x)) return __LINE__; } while (0)
unsigned test_irq = 1;
static InputEvent last, prior;
static unsigned count;
static int v86;
int legacy_active(void) { return v86; }
u32 in2_time_ms(void) { return 50; }
void input_emit_in2(const InputEvent *e) { prior = last; last = *e; ++count; }
static int probe(void)
{
    u16 g1, g2;
    i16 x, y, dx, dy;
    i32 ax, ay;
    pointer_position(&x, &y); T(x == 319 && y == 199);
    T(input_src_register(INPUT_KIND_MOUSE, INPUT_ORIGIN_USB, &g1) == 1);
    T(input_src_register(INPUT_KIND_MOUSE, INPUT_ORIGIN_USB, &g2) == 2);
    InputEvent e = { .src = 1, .generation = g1, .type = INPUT_EVENT_BUTTON, .buttons = 3 };
    T(!input_capture(&e) && last.buttons == 3 && last.sc == 3);
    e.src = 2; e.generation = g2; e.buttons = 1;
    T(!input_capture(&e) && input_buttons() == 3 && last.sc == 1);
    input_src_release(1, g1);
    T(input_buttons() == 1 && last.src == 1 && last.buttons == 0 && last.sc == 3);
    input_src_release(2, g2); T(!input_buttons() && last.sc == 1);
    T(input_src_register(INPUT_KIND_MOUSE, INPUT_ORIGIN_USB, &g1) == 1);
    e.src = 1; e.generation = g1; e.type = INPUT_EVENT_MOVE; e.buttons = 0; e.x = 10; e.y = -5;
    T(!input_capture(&e)); e.x = 20; e.y = 7; T(!input_capture(&e));
    mouse_ext_read(&ax, &ay); T(ax == 30 && ay == 2);
    mouse_ext_read_legacy(&dx, &dy); T(dx == 30 && dy == 2);
    mouse_ext_read(&ax, &ay); T(!ax && !ay);
    mouse_ext_mode(1); e.x = 12; e.y = 1; T(!input_capture(&e));
    mouse_ext_read_legacy(&dx, &dy); T(!dx && !dy);
    e.flags = INPUT_EVENT_SEAMLESS; T(!input_capture(&e));
    mouse_ext_read_legacy(&dx, &dy); T(dx == 12 && dy == 1);
    pointer_bounds(0, 0, 640, 400);
    e.flags = 0; e.type = INPUT_EVENT_ABS; e.x = 32767; e.y = 0;
    T(!input_capture(&e)); pointer_position(&x, &y); T(x == 640 && y == 0);
    e.x = -1; T(input_capture(&e) < 0);
    e.type = INPUT_EVENT_WHEEL; e.x = -3; e.y = 0; T(!input_capture(&e) && last.x == -3);
    unsigned n = count; e.src = 0; T(input_capture(&e) < 0); e.src = 17; T(input_capture(&e) < 0 && n == count);
    e.src = 1; e.generation = g1; e.flags = 0; e.buttons = 0; e.type = INPUT_EVENT_MOVE;
    mouse_ext_mode(0); mouse_ext_read(&ax, &ay); e.x = 32767; e.y = -32768;
    /* 65,540 valid events cross both i32 limits without a private-state shim. */
    for (unsigned i = 0; i < 65540; ++i) T(!input_capture(&e));
    mouse_ext_read_legacy(&dx, &dy); T(dx == 32767 && dy == -32768);
    mouse_ext_read(&ax, &ay); T(ax == 2147483647L && ay == (-2147483647L - 1));
    e.x = -32768; e.y = 32767;
    for (unsigned i = 0; i < 65540; ++i) T(!input_capture(&e));
    mouse_ext_read(&ax, &ay); T(ax == (-2147483647L - 1) && ay == 2147483647L);
    mouse_ext_read_legacy(&dx, &dy); T(dx == -32768 && dy == 32767);
    pointer_bounds(0, 0, 639, 399);
    /* Old mouse_poll: normalized half maps to (319,199); first dx/dy are zero. */
    mouse_ext_mode(1); n = count;
    T(!mouse_ext_sample(1, g1, 16384, 16384, 100) && count == n);
    pointer_position(&x, &y); T(x == 319 && y == 199);
    mouse_ext_read(&ax, &ay); mouse_ext_read_legacy(&dx, &dy); T(!ax && !ay && !dx && !dy);
    T(!mouse_ext_sample(1, g1, 32767, 0, 101) && count == n + 2);
    T(prior.type == INPUT_EVENT_ABS && prior.x == 32767 && !prior.y && prior.src == 1 && prior.generation == g1 && prior.time_ms == 101);
    T(last.type == INPUT_EVENT_MOVE && last.flags == INPUT_EVENT_SEAMLESS && last.time_ms == 101);
    mouse_ext_read(&ax, &ay); T(ax == 320 && ay == -199);
    mouse_ext_read_legacy(&dx, &dy); T(dx == 320 && dy == -199);
    pointer_position(&x, &y); T(x == 639 && y == 0); /* no second coordinate advance */
    mouse_ext_mode(1); T(!mouse_ext_sample(1, g1, 0, 32767, 102));
    mouse_ext_read(&ax, &ay); T(ax == -639 && ay == 399);
    mouse_ext_mode(0); mouse_ext_read_legacy(&dx, &dy); T(!dx && !dy);
    mouse_ext_mode(1); n = count;
    T(!mouse_ext_sample(1, g1, 32767, 32767, 103) && count == n);
    mouse_ext_read(&ax, &ay); T(!ax && !ay);
    T(mouse_ext_sample(0, g1, 0, 0, 104) < 0 && mouse_ext_sample(17, g1, 0, 0, 104) < 0);
    T(mouse_ext_sample(1, g1 + 1, 0, 0, 104) < 0 && mouse_ext_sample(1, g1, -1, 0, 104) < 0);
    pointer_position(&x, &y); T(x == 639 && y == 399);
    v86 = 1; T(!mouse_ext_sample(1, g1, 0, 0, 105) && count == n);
    pointer_position(&x, &y); T(!x && !y);
    mouse_ext_read(&ax, &ay); mouse_ext_read_legacy(&dx, &dy); T(!ax && !ay && !dx && !dy);
    v86 = 0; T(!mouse_ext_sample(1, g1, 16384, 16384, 106));
    mouse_ext_read(&ax, &ay); mouse_ext_read_legacy(&dx, &dy);
    T(ax == 319 && ay == 199 && dx == 319 && dy == 199);
    mouse_ext_mode(0); v86 = 1; n = count; e.x = 10; e.y = -5;
    T(!input_capture(&e)); pointer_position(&x, &y); T(x == 329 && y == 194);
    e.x = e.y = 0; e.type = INPUT_EVENT_BUTTON; e.buttons = 3;
    T(!input_capture(&e) && input_buttons() == 3);
    e.buttons = 0; e.type = INPUT_EVENT_WHEEL; e.x = -2; T(!input_capture(&e));
    input_src_release(1, g1); T(!input_buttons() && count == n);
    v86 = 0; mouse_ext_read(&ax, &ay); mouse_ext_read_legacy(&dx, &dy);
    T(!ax && !ay && !dx && !dy);
    for (unsigned i = 1; i <= IN2_MAX_DEV; ++i)
        T(input_src_register(INPUT_KIND_MOUSE, INPUT_ORIGIN_USB, &g1) == (int)i);
    e.src = 16; e.generation = g1; e.type = INPUT_EVENT_BUTTON; e.x = 0; e.buttons = 4;
    test_irq = 0; T(!input_capture(&e) && !test_irq && input_buttons() == 4);
    mouse_ext_mode(1); pointer_bounds(-32768, -32768, 32767, 32767);
    T(!mouse_ext_sample(16, g1, 0, 32767, 107));
    T(!mouse_ext_sample(16, g1, 32767, 0, 108));
    mouse_ext_read_legacy(&dx, &dy); T(dx == 32767 && dy == -32768);
    T(!test_irq);
    input_src_release(16, g1); T(!input_buttons() && last.sc == 4);
    return 0;
}
void _start(void) { int rc = probe(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); __builtin_unreachable(); }
