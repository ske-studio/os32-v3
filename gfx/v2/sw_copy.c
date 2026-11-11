/* HAL v2 software SRCCOPY. Inputs and clipping are validated by submit. */
#include "gfx2_sw.h"

int gfx2_sw_copy(const struct gfx2_exec *ex, struct gfx2_rect *touched)
{
    const GFX2_Op *op = ex->op;
    const struct gfx2_view *sv = &ex->sv, *dv = &ex->dv;
    struct gfx2_fmt_info fmt;
    i32 sx = op->sx + ex->clip.x - op->dx;
    i32 sy = op->sy + ex->clip.y - op->dy;
    i32 row, end, step;
    u32 bytes, i;
    int rc;

    *touched = ex->clip;
    if (ex->clip.w <= 0 || ex->clip.h <= 0) return 0;
    rc = gfx2_fmt_info(sv->fmt, &fmt);
    if (rc) return rc;
    bytes = (u32)ex->clip.w * fmt.cpp;
    if ((sv->sync || dv->sync) && bytes > GFX2_SW_ROW_BYTES)
        return OS32_ERR_INVAL;
    row = 0; end = ex->clip.h; step = 1;
    if (ex->src == ex->dst && ex->clip.y > sy) {
        row = ex->clip.h - 1; end = -1; step = -1;
    }
    for (; row != end; row += step) {
        const u8 *src;
        u8 *dst;
        if (sv->sync) {
            rc = gfxsync_read_rows(sv->sync, (u32)sx, (u32)(sy + row),
                                   (u32)ex->clip.w, 1, gfx2_sw_rowbuf[0], bytes);
            if (rc) return rc;
            src = gfx2_sw_rowbuf[0];
        } else {
            src = sv->base + (u32)(sy + row) * sv->pitch + (u32)sx * fmt.cpp;
        }
        if (dv->sync) {
            /* Snapshot a direct source before invoking the destination writer. */
            if (!sv->sync) {
                for (i = 0; i < bytes; i++) gfx2_sw_rowbuf[0][i] = src[i];
                src = gfx2_sw_rowbuf[0];
            }
            rc = gfxsync_write_rows(dv->sync, (u32)ex->clip.x,
                                    (u32)(ex->clip.y + row), (u32)ex->clip.w,
                                    1, src, bytes);
            if (rc) return rc;
        } else {
            dst = dv->base + (u32)(ex->clip.y + row) * dv->pitch +
                  (u32)ex->clip.x * fmt.cpp;
            if (ex->src == ex->dst && ex->clip.x > sx && !sv->sync) {
                for (i = bytes; i > 0; i--) dst[i - 1] = src[i - 1];
            } else {
                for (i = 0; i < bytes; i++) dst[i] = src[i];
            }
        }
    }
    return 0;
}
