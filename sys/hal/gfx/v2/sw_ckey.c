/* HAL v2 software keyed COPY. Inputs have been checked by submit. */
#include "gfx2_sw.h"

int gfx2_sw_ckey(const struct gfx2_exec *ex, struct gfx2_rect *touched)
{
    const GFX2_Op *op = ex->op;
    struct gfx2_fmt_info sf, df;
    i32 x, y, start, end, step;
    int any = 0, rc;

    *touched = (struct gfx2_rect){0, 0, 0, 0};
    if (ex->clip.w <= 0 || ex->clip.h <= 0) return 0;
    if (gfx2_fmt_info(ex->sv.fmt, &sf) || gfx2_fmt_info(ex->dv.fmt, &df))
        return OS32_ERR_INVAL;
    if ((u32)ex->clip.w * sf.cpp > GFX2_SW_ROW_BYTES ||
        (u32)ex->clip.w * df.cpp > GFX2_SW_ROW_BYTES)
        return OS32_ERR_INVAL;

    start = ex->clip.y;
    end = start + ex->clip.h;
    step = 1;
    if (ex->src == ex->dst && op->dy > op->sy) {
        start = end - 1;
        end = ex->clip.y - 1;
        step = -1;
    }
    for (y = start; y != end; y += step) {
        u32 sx = (u32)(op->sx + ex->clip.x - op->dx);
        u32 sy = (u32)(op->sy + y - op->dy);
        u32 dx = (u32)ex->clip.x;
        u32 width = (u32)ex->clip.w;
        u32 sb = width * sf.cpp, db = width * df.cpp;
        u8 *s = gfx2_sw_rowbuf[0], *d = gfx2_sw_rowbuf[1];
        if (ex->sv.sync) {
            rc = gfxsync_read_rows(ex->sv.sync, sx, sy, width, 1, s, sb);
            if (rc) return rc;
        } else {
            const u8 *p = ex->sv.base + sy * ex->sv.pitch + sx * sf.cpp;
            for (u32 i = 0; i < sb; ++i) s[i] = p[i];
        }
        if (ex->dv.sync) {
            rc = gfxsync_read_rows(ex->dv.sync, dx, (u32)y, width, 1, d, db);
            if (rc) return rc;
        } else {
            const u8 *p = ex->dv.base + (u32)y * ex->dv.pitch + dx * df.cpp;
            for (u32 i = 0; i < db; ++i) d[i] = p[i];
        }
        for (x = 0; x < ex->clip.w; ++x) {
            u32 sv = gfx2_px_read(s + (u32)x * sf.cpp, ex->sv.fmt);
            u32 dv = gfx2_px_read(d + (u32)x * df.cpp, ex->dv.fmt);
            u32 out = sv;
            if ((op->flags & GFX2_F_SRCKEY) && sv >= op->skey.v && sv <= op->skey.x)
                continue;
            if ((op->flags & GFX2_F_DSTKEY) && !(dv >= op->dkey.v && dv <= op->dkey.x))
                continue;
            if (ex->sv.fmt != ex->dv.fmt) {
                rc = gfx2_from_argb(ex->dv.fmt, gfx2_to_argb(ex->sv.fmt, sv, ex->sv.pal), &out);
                if (rc) return rc;
            }
            gfx2_px_write(d + (u32)x * df.cpp, ex->dv.fmt, out);
            if (!any) {
                *touched = (struct gfx2_rect){ex->clip.x + x, y, 1, 1};
                any = 1;
            } else {
                i32 right = touched->x + touched->w, bottom = touched->y + touched->h;
                if (ex->clip.x + x < touched->x) touched->x = ex->clip.x + x;
                if (y < touched->y) touched->y = y;
                if (ex->clip.x + x >= right) right = ex->clip.x + x + 1;
                if (y >= bottom) bottom = y + 1;
                touched->w = right - touched->x;
                touched->h = bottom - touched->y;
            }
        }
        if (ex->dv.sync) {
            rc = gfxsync_write_rows(ex->dv.sync, dx, (u32)y, width, 1, d, db);
            if (rc) return rc;
        } else {
            u8 *p = ex->dv.base + (u32)y * ex->dv.pitch + dx * df.cpp;
            for (u32 i = 0; i < db; ++i) p[i] = d[i];
        }
    }
    return 0;
}
