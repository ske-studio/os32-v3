#include "in2_int.h"
#include "io.h"

static u8 buttons[IN2_MAX_DEV];
static i16 px = 639 / 2, py = 399 / 2, xmin, ymin, xmax = 639, ymax = 399;
static i16 prev_x, prev_y;
static int prev_valid;

u8 pointer_buttons_locked(void)
{
    u8 mask = 0;
    for (unsigned i = 0; i < IN2_MAX_DEV; ++i) mask |= buttons[i];
    return mask;
}
u8 input_buttons(void)
{
    unsigned f = irq_save();
    u8 mask = pointer_buttons_locked();
    irq_restore(f);
    return mask;
}
u8 pointer_source_buttons(u8 src) { return buttons[src - 1]; }
int pointer_button(InputEvent *e)
{
    e->sc = buttons[e->src - 1] ^ e->buttons;
    buttons[e->src - 1] = e->buttons;
    return e->sc != 0;
}
static i16 clamp(int value, i16 lo, i16 hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return (i16)value;
}
void pointer_move(i16 x, i16 y)
{
    px = clamp((int)px + x, xmin, xmax);
    py = clamp((int)py + y, ymin, ymax);
}
void pointer_absolute(i16 x, i16 y)
{
    int nx = xmin + (int)x * (xmax - xmin) / 32767;
    int ny = ymin + (int)y * (ymax - ymin) / 32767;
    px = clamp(nx, xmin, xmax); py = clamp(ny, ymin, ymax);
}
void pointer_absolute_reset(void) { prev_valid = 0; }
int pointer_absolute_delta(i16 x, i16 y, i16 *dx, i16 *dy)
{
    int valid = prev_valid;
    pointer_absolute(x, y);
    *dx = valid ? clamp((int)px - prev_x, -32768, 32767) : 0;
    *dy = valid ? clamp((int)py - prev_y, -32768, 32767) : 0;
    prev_x = px; prev_y = py; prev_valid = 1;
    return valid;
}
void pointer_bounds(i16 x0, i16 y0, i16 x1, i16 y1)
{
    unsigned f = irq_save();
    /* Preserve legacy bounds, including reversed ranges and clamp order. */
    xmin = x0; ymin = y0; xmax = x1; ymax = y1;
    px = clamp(px, xmin, xmax); py = clamp(py, ymin, ymax);
    irq_restore(f);
}
void pointer_position_locked(i16 *x, i16 *y) { *x = px; *y = py; }
void pointer_position(i16 *x, i16 *y)
{
    unsigned f = irq_save();
    pointer_position_locked(x, y);
    irq_restore(f);
}

void pointer_get_bounds(i16 *x0, i16 *y0, i16 *x1, i16 *y1)
{
    unsigned f = irq_save();
    *x0 = xmin; *y0 = ymin; *x1 = xmax; *y1 = ymax;
    irq_restore(f);
}
