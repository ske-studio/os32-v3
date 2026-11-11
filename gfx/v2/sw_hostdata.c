#include "gfx2_sw.h"

int gfx2_sw_hostdata(const struct gfx2_exec *ex, struct gfx2_rect *touched)
{
    struct gfx2_fmt_info fmt;
    const GFX2_Op *op = ex->op;
    i32 x, y, minx = 0, miny = 0, maxx = 0, maxy = 0;
    int any = 0;

    *touched = (struct gfx2_rect){0, 0, 0, 0};
    if (gfx2_fmt_info(ex->dv.fmt, &fmt) || !fmt.cpp)
        return OS32_ERR_INVAL;
    for (y = ex->clip.y; y < ex->clip.y + ex->clip.h; y++) {
        const u8 *row = ex->cpu + (u32)(y - op->dy) * ex->cpu_pitch;
        for (x = ex->clip.x; x < ex->clip.x + ex->clip.w; x++) {
            const u8 *src = row + (u32)(x - op->dx) * fmt.cpp;
            u32 pixel = gfx2_px_read(src, ex->dv.fmt);
            int rc;
            if ((op->flags & GFX2_F_SRCKEY) &&
                pixel >= op->skey.v && pixel <= op->skey.x)
                continue;
            if (ex->dv.sync) {
                rc = gfxsync_write_rows(ex->dv.sync, (u32)x, (u32)y, 1, 1, src, fmt.cpp);
                if (rc) return rc;
            } else {
                gfx2_px_write(ex->dv.base + (u32)y * ex->dv.pitch + (u32)x * fmt.cpp,
                              ex->dv.fmt, pixel);
            }
            if (!any) { minx = maxx = x; miny = maxy = y; any = 1; }
            else {
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
            *touched = (struct gfx2_rect){minx, miny, maxx - minx + 1, maxy - miny + 1};
        }
    }
    return 0;
}
