/* HAL v2 software line: rasterize before clipping, excluding the endpoint. */
#include "gfx2_sw.h"

int gfx2_sw_line(const struct gfx2_exec *ex, struct gfx2_rect *touched)
{
    const GFX2_Op *op = ex->op;
    const struct gfx2_view *v = &ex->dv;
    struct gfx2_fmt_info info;
    i32 x = op->dx, y = op->dy;
    i32 dx = (i32)op->sx - x, dy = (i32)op->sy - y;
    i32 sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    i32 major, minor, e, n;
    int xmajor, rc;

    touched->x = touched->y = touched->w = touched->h = 0;
    if (ex->clip.w <= 0 || ex->clip.h <= 0)
        return 0;
    rc = gfx2_fmt_info(v->fmt, &info);
    if (rc) return rc;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    xmajor = dx >= dy;
    major = xmajor ? dx : dy;
    minor = xmajor ? dy : dx;
    e = 2 * minor - major;
    for (n = 0; n < major; ++n) {
        if (x >= ex->clip.x && y >= ex->clip.y &&
            x - ex->clip.x < ex->clip.w && y - ex->clip.y < ex->clip.h) {
            u8 *p;
            u32 d, result = 0, bit;
            if (v->sync) {
                p = gfx2_sw_rowbuf[0];
                rc = gfxsync_read_rows(v->sync, (u32)x, (u32)y, 1, 1, p, info.cpp);
                if (rc) return rc;
            } else {
                p = v->base + (u32)y * v->pitch + (u32)x * info.cpp;
            }
            d = gfx2_px_read(p, v->fmt);
            for (bit = 0; bit < 32; ++bit) {
                u32 index = (((u32)op->fg.v >> bit) & 1u) * 4u + ((d >> bit) & 1u);
                result |= ((op->rop >> index) & 1u) << bit;
            }
            gfx2_px_write(p, v->fmt, result);
            if (v->sync) {
                rc = gfxsync_write_rows(v->sync, (u32)x, (u32)y, 1, 1, p, info.cpp);
                if (rc) return rc;
            }
            if (!touched->w) {
                touched->x = x; touched->y = y; touched->w = touched->h = 1;
            } else {
                i32 right = touched->x + touched->w, bottom = touched->y + touched->h;
                if (x < touched->x) touched->x = x;
                if (y < touched->y) touched->y = y;
                if (x >= right) right = x + 1;
                if (y >= bottom) bottom = y + 1;
                touched->w = right - touched->x; touched->h = bottom - touched->y;
            }
        }
        if (e > 0) {
            if (xmajor) y += sy; else x += sx;
            e -= 2 * major;
        }
        if (xmajor) x += sx; else y += sy;
        e += 2 * minor;
    }
    return 0;
}
