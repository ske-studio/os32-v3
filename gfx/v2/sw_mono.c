/* HAL v2 software monochrome expansion.  Inputs are validated by submit. */
#include "gfx2_sw.h"

int gfx2_sw_mono(const struct gfx2_exec *ex, struct gfx2_rect *touched)
{
    const GFX2_Op *op = ex->op;
    const struct gfx2_rect *clip = &ex->clip;
    struct gfx2_fmt_info fmt;
    int rc;

    *touched = (struct gfx2_rect){0, 0, 0, 0};
    if (clip->w <= 0 || clip->h <= 0) return 0;
    rc = gfx2_fmt_info(ex->dv.fmt, &fmt);
    if (rc) return rc;
    for (i32 y = clip->y; y < clip->y + clip->h; y++) {
        const u8 *bits = ex->cpu + (u32)(y - op->dy) * ex->cpu_pitch;
        for (i32 x = clip->x; x < clip->x + clip->w; x++) {
            u32 bit = (u32)(x - op->dx);
            int set = (bits[bit / 8] & (0x80u >> (bit & 7))) != 0;
            u8 pixel[4];
            if (!set && (op->bg.x & GFX2_CX_TRANSPARENT)) continue;
            gfx2_px_write(pixel, ex->dv.fmt, set ? op->fg.v : op->bg.v);
            if (ex->dv.sync) {
                rc = gfxsync_write_rows(ex->dv.sync, (u32)x, (u32)y, 1, 1, pixel, fmt.cpp);
                if (rc) return rc;
            } else {
                u8 *dst = ex->dv.base + (u32)y * ex->dv.pitch + (u32)x * fmt.cpp;
                for (u32 i = 0; i < fmt.cpp; i++) dst[i] = pixel[i];
            }
            if (!touched->w) *touched = (struct gfx2_rect){x, y, 1, 1};
            else {
                i32 right = touched->x + touched->w;
                i32 bottom = touched->y + touched->h;
                if (x < touched->x) touched->x = x;
                if (y < touched->y) touched->y = y;
                if (x >= right) right = x + 1;
                if (y >= bottom) bottom = y + 1;
                touched->w = right - touched->x;
                touched->h = bottom - touched->y;
            }
        }
    }
    return 0;
}
