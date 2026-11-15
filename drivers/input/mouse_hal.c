#include "mouse.h"
#include "pc98.h"
#include "io.h"
#include "../../input/hal/in2_int.h"

static u8 bus_hal_src;
static u16 bus_hal_generation;

void mouse_hal_init(void)
{
    int src = input_src_register(INPUT_KIND_MOUSE, INPUT_ORIGIN_NATIVE,
                                 &bus_hal_generation);
    bus_hal_src = src > 0 ? (u8)src : 0;
}

void mouse_hal_capture(i8 dx, i8 dy, u8 raw)
{
    if (!bus_hal_src) return;
    InputEvent e = { .src = bus_hal_src, .generation = bus_hal_generation,
                     .time_ms = in2_time_ms(), .type = INPUT_EVENT_MOVE,
                     .x = dx, .y = dy };
    if (dx || dy) input_capture(&e);
    e.type = INPUT_EVENT_BUTTON; e.x = e.y = 0;
    if (!(raw & MOUSE_LEFT)) e.buttons |= MOUSE_BTN_LEFT;
    if (!(raw & MOUSE_RIGHT)) e.buttons |= MOUSE_BTN_RIGHT;
    if (!(raw & MOUSE_MIDDLE)) e.buttons |= MOUSE_BTN_MIDDLE;
    input_capture(&e);
}

static u8 seamless_hal_src;
static u16 seamless_hal_generation;

void mouse_hal_seamless_init(void)
{
    if (seamless_hal_src) return;
    int src = input_src_register(INPUT_KIND_MOUSE, INPUT_ORIGIN_NATIVE,
                                 &seamless_hal_generation);
    seamless_hal_src = src > 0 ? (u8)src : 0;
}

/* Foreground poll until the stage-B tick wiring is available. One device read. */
void mouse_hal_seamless_poll(void)
{
    unsigned f = irq_save();
    i16 x, y;
    if (seamless_hal_src) {
        u32 time_ms = in2_time_ms();
        if (seamless_mouse_poll(&x, &y))
            mouse_ext_sample(seamless_hal_src, seamless_hal_generation, x, y, time_ms);
        InputEvent e = { .src = seamless_hal_src, .generation = seamless_hal_generation,
                         .time_ms = time_ms, .type = INPUT_EVENT_BUTTON,
                         .buttons = mouse_read_buttons() };
        input_capture(&e);
    }
    irq_restore(f);
}
