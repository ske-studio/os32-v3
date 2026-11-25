/* HAL v2 software solid fill. */
#include "gfx2_sw.h"

int gfx2_sw_fill(const struct gfx2_exec *ex, struct gfx2_rect *touched)
{
    const struct gfx2_rect *r = &ex->clip;
    struct gfx2_fmt_info fmt;
    int rc;

    *touched = (struct gfx2_rect){0, 0, 0, 0};
    if (r->w <= 0 || r->h <= 0) return 0;
    rc = gfx2_fmt_info(ex->dv.fmt, &fmt);
    if (rc) return rc;
    for (i32 y = r->y; y < r->y + r->h; ++y) {
        for (i32 x = r->x; x < r->x + r->w; ++x) {
            if (ex->dv.sync) {
                u8 pixel[4];
                gfx2_px_write(pixel, ex->dv.fmt, ex->op->fg.v);
                rc = gfxsync_write_rows(ex->dv.sync, x, y, 1, 1, pixel, fmt.cpp);
                if (rc) return rc;
            } else {
                u8 *p = ex->dv.base + (u32)y * ex->dv.pitch + (u32)x * fmt.cpp;
                gfx2_px_write(p, ex->dv.fmt, ex->op->fg.v);
            }
        }
    }
    *touched = *r;
    return 0;
}
