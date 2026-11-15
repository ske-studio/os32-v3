#include "in2_int.h"
#include "input_unused_key_host.h"
#include "mouse.h"
#include "pc98.h"
#define T(x) do { if (!(x)) return __LINE__; } while (0)
unsigned test_irq = 1;
static unsigned char samples[4];
static unsigned pos, count;
static InputEvent events[16];
static int v86;
unsigned test_inp(unsigned port) { (void)port; return samples[pos++]; }
void test_outp(unsigned port, unsigned value) { (void)port; (void)value; }
int legacy_active(void) { return v86; }
u32 in2_time_ms(void) { return 50; }
void input_emit_in2(const InputEvent *e) { if (count < 16) events[count] = *e; ++count; }
static void irq_sample(u8 raw, u8 x, u8 y)
{
    samples[0] = raw | (x & 15); samples[1] = x >> 4;
    samples[2] = y & 15; samples[3] = y >> 4; pos = 0;
    mouse_irq_handler();
}
static int probe(void)
{
    i16 dx, dy; i32 x, y; u8 buttons; u16 gen;
    T(bus_mouse_init());
    irq_sample(MOUSE_MIDDLE | MOUSE_RIGHT, 5, 0xfd);
    T(pos == 4 && count == 2 && events[0].type == INPUT_EVENT_MOVE && events[0].x == 5 && events[0].y == -3 && events[0].time_ms == 50);
    T(events[1].type == INPUT_EVENT_BUTTON && events[1].buttons == MOUSE_BTN_LEFT && events[1].sc == MOUSE_BTN_LEFT);
    mouse_ext_read(&x, &y); T(x == 5 && y == -3);
    mouse_ext_read_legacy(&dx, &dy); buttons = input_buttons(); T(dx == 5 && dy == -3 && buttons == MOUSE_BTN_LEFT);
    irq_sample(MOUSE_LEFT | MOUSE_MIDDLE | MOUSE_RIGHT, 0, 0);
    T(count == 3 && events[2].type == INPUT_EVENT_BUTTON && !events[2].buttons && events[2].sc == MOUSE_BTN_LEFT);
    v86 = 1; irq_sample(MOUSE_MIDDLE | MOUSE_RIGHT, 4, 2);
    T(count == 3 && input_buttons() == MOUSE_BTN_LEFT);
    v86 = 0; mouse_ext_read(&x, &y); mouse_ext_read_legacy(&dx, &dy); T(!x && !y && !dx && !dy);
    mouse_ext_read_legacy(&dx, &dy); buttons = input_buttons(); T(!dx && !dy && buttons == MOUSE_BTN_LEFT);
    for (unsigned i = 2; i <= IN2_MAX_DEV; ++i) T(input_src_register(INPUT_KIND_MOUSE, INPUT_ORIGIN_USB, &gen) == (int)i);
    T(bus_mouse_init()); irq_sample(MOUSE_LEFT | MOUSE_MIDDLE | MOUSE_RIGHT, 7, 0xfe);
    T(count == 3); mouse_ext_read_legacy(&dx, &dy); T(!dx && !dy);
    return 0;
}
void _start(void) { int rc = probe(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); __builtin_unreachable(); }
