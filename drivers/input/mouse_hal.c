#include "mouse.h"
#include "pc98.h"
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
