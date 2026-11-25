#include "in2_int.h"
#include "io.h"

static u8 buttons[IN2_MAX_DEV];
static i16 px = 639 / 2, py = 399 / 2, xmin, ymin, xmax = 639, ymax = 399;
static i16 prev_x, prev_y;
static int prev_valid;
static int seamless;
static int app_bounds_set;

int pointer_seamless(void) { return seamless; }
void mouse_ext_mode(int enabled)
{
    unsigned f = irq_save();
    if (seamless != !!enabled) {
        i16 dx, dy;
        seamless = !!enabled;
        mouse_ext_read_legacy(&dx, &dy);
        pointer_absolute_reset();
    }
    irq_restore(f);
}

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
    i16 nx = xmin + (int)x * (xmax - xmin) / 32767;
    i16 ny = ymin + (int)y * (ymax - ymin) / 32767;
    pointer_absolute(x, y);
    *dx = valid ? clamp((int)nx - prev_x, -32768, 32767) : 0;
    *dy = valid ? clamp((int)ny - prev_y, -32768, 32767) : 0;
    prev_x = nx; prev_y = ny; prev_valid = 1;
    return valid;
}
static void set_bounds_locked(i16 x0, i16 y0, i16 x1, i16 y1)
{
    /* Preserve legacy bounds, including reversed ranges and clamp order. */
    xmin = x0; ymin = y0; xmax = x1; ymax = y1;
    px = clamp(px, xmin, xmax); py = clamp(py, ymin, ymax);
}
void pointer_set_default_bounds(i16 x_max, i16 y_max)
{
    unsigned f = irq_save();
    if (!app_bounds_set)
        set_bounds_locked(0, 0, x_max, y_max);
    irq_restore(f);
}
void pointer_bounds(i16 x0, i16 y0, i16 x1, i16 y1)
{
    unsigned f = irq_save();
    app_bounds_set = 1;
    set_bounds_locked(x0, y0, x1, y1);
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
