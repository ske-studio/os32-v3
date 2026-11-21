#include "in2_int.h"
#include "io.h"
#include "legacy_guest.h"
#include "os32_kapi_shared.h"

static i32 in2_x, in2_y;
static i32 wheel;
static i16 old_x, old_y;

static i32 add32(i32 a, i16 b)
{
    if (b > 0 && a > S32_MAX - b) return S32_MAX;
    if (b < 0 && a < (-S32_MAX - 1) - b) return -S32_MAX - 1;
    return a + b;
}
void mouse_ext_wheel(i16 delta)
{
    wheel = add32(wheel, delta);
}

i32 mouse_ext_read_wheel(void)
{
    unsigned f = irq_save();
    i32 delta = wheel;
    wheel = 0;
    irq_restore(f);
    return delta;
}

static i16 add16(i16 a, i16 b)
{
    int n = (int)a + b;
    if (n > 32767) n = 32767;
    if (n < -32768) n = -32768;
    return (i16)n;
}
void mouse_ext_move(i16 x, i16 y, int marked)
{
    if (pointer_seamless() == !!marked && !marked) pointer_move(x, y);
    if (legacy_active()) return;
    in2_x = add32(in2_x, x); in2_y = add32(in2_y, y);
    if (pointer_seamless() != !!marked) return;
    old_x = add16(old_x, x); old_y = add16(old_y, y);
}
void mouse_ext_read(i32 *x, i32 *y)
{
    unsigned f = irq_save();
    *x = in2_x; *y = in2_y; in2_x = in2_y = 0;
    irq_restore(f);
}
void mouse_ext_read_legacy(i16 *x, i16 *y)
{
    unsigned f = irq_save();
    *x = old_x; *y = old_y; old_x = old_y = 0;
    irq_restore(f);
}
void mouse_legacy_snapshot(i16 *dx, i16 *dy, u8 *buttons, i16 *x, i16 *y)
{
    unsigned f = irq_save();
    *dx = old_x; *dy = old_y; old_x = old_y = 0;
    *buttons = pointer_buttons_locked();
    pointer_position_locked(x, y);
    irq_restore(f);
}
/* Stage 5d adapter supplies one normalized sample; first sample is baseline only.
 * Subsequent ABS and pixel MOVE share the sampled time and source generation. */
int mouse_ext_sample(u8 src, u16 generation, i16 x, i16 y, u32 time_ms)
{
    unsigned f = irq_save();
    u8 kind, origin;
    int rc = OS32_ERR_INVAL;
    if (!pointer_seamless() || x < 0 || y < 0 ||
        !input_src_info(src, generation, &kind, &origin) || kind != INPUT_KIND_MOUSE) goto out;
    InputEvent e = { .src = src, .generation = generation, .time_ms = time_ms,
                     .type = INPUT_EVENT_ABS, .x = x, .y = y };
    i16 dx, dy;
    if (pointer_absolute_delta(x, y, &dx, &dy)) {
        input_capture(&e);
        e.type = INPUT_EVENT_MOVE; e.flags = INPUT_EVENT_SEAMLESS; e.x = dx; e.y = dy;
        input_capture(&e);
    }
    rc = 0;
out:
    irq_restore(f);
    return rc;
}
