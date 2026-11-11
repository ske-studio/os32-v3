/* HAL v2 software stretch: coordinates refer to the unclipped destination. */
#include "gfx2_sw.h"
#include "os32_kapi_shared.h"

static int sample(const struct gfx2_view *v, u32 x, u32 y, u32 cpp, u32 *raw, u32 *argb)
{
    u8 bytes[4];
    const u8 *p;
    u32 start = x, width = 1, n = cpp;
    int rc;
    if (v->fmt == GFX2_FMT_YUY2 || v->fmt == GFX2_FMT_UYVY) {
        start = x & ~1u; width = 2; n = 4;
    }
    if (v->sync) {
        rc = gfxsync_read_rows(v->sync, start, y, width, 1, bytes, n);
        if (rc) return rc;
        p = bytes;
    } else {
        p = v->base + y * v->pitch + start * cpp;
    }
    if (v->fmt == GFX2_FMT_YUY2) {
        *argb = gfx2_yuv_argb(p[(x & 1u) ? 2 : 0], p[1], p[3]);
    } else if (v->fmt == GFX2_FMT_UYVY) {
        *argb = gfx2_yuv_argb(p[(x & 1u) ? 3 : 1], p[0], p[2]);
    } else {
        *raw = gfx2_px_read(p, v->fmt);
        *argb = gfx2_to_argb(v->fmt, *raw, v->pal);
    }
    return 0;
}

static u32 blend(u32 a, u32 b, u32 w)
{
    u32 out = 0, shift;
    for (shift = 0; shift < 32; shift += 8) {
        u32 av = (a >> shift) & 255u, bv = (b >> shift) & 255u;
        out |= ((av * (256u - w) + bv * w + 128u) >> 8) << shift;
    }
    return out;
}

static u32 position(u32 i, u32 src, u32 dst)
{
    unsigned long long f = ((unsigned long long)(2u * i + 1u) * src * 65536u) / (2u * dst);
    return f < 32768u ? 0 : (u32)(f - 32768u);
}

int gfx2_sw_stretch(const struct gfx2_exec *ex, struct gfx2_rect *touched)
{
    const GFX2_Op *op = ex->op;
    struct gfx2_fmt_info sf, df;
    i32 x, y;
    int rc;
    if (gfx2_fmt_info(ex->sv.fmt, &sf) || gfx2_fmt_info(ex->dv.fmt, &df)) return OS32_ERR_INVAL;
    *touched = ex->clip;
    for (y = ex->clip.y; y < ex->clip.y + ex->clip.h; ++y) {
        for (x = ex->clip.x; x < ex->clip.x + ex->clip.w; ++x) {
            u32 ix = (u32)(x - op->dx), iy = (u32)(y - op->dy);
            u32 sx, sy, raw = 0, color = 0, value;
            u8 bytes[4];
            if (op->flags & GFX2_F_FILTER) {
                u32 fx = position(ix, op->sw, op->dw), fy = position(iy, op->sh, op->dh);
                u32 x0 = fx >> 16, y0 = fy >> 16, x1, y1, a, b, c, d;
                if (x0 >= op->sw) x0 = op->sw - 1;
                if (y0 >= op->sh) y0 = op->sh - 1;
                x1 = x0 + 1 < op->sw ? x0 + 1 : x0;
                y1 = y0 + 1 < op->sh ? y0 + 1 : y0;
                rc = sample(&ex->sv, op->sx + x0, op->sy + y0, sf.cpp, &raw, &a); if (rc) return rc;
                rc = sample(&ex->sv, op->sx + x1, op->sy + y0, sf.cpp, &raw, &b); if (rc) return rc;
                rc = sample(&ex->sv, op->sx + x0, op->sy + y1, sf.cpp, &raw, &c); if (rc) return rc;
                rc = sample(&ex->sv, op->sx + x1, op->sy + y1, sf.cpp, &raw, &d); if (rc) return rc;
                color = blend(blend(a, b, (fx >> 8) & 255u), blend(c, d, (fx >> 8) & 255u), (fy >> 8) & 255u);
            } else {
                sx = op->sx + (u32)(((unsigned long long)(2u * ix + 1u) * op->sw) / (2u * op->dw));
                sy = op->sy + (u32)(((unsigned long long)(2u * iy + 1u) * op->sh) / (2u * op->dh));
                rc = sample(&ex->sv, sx, sy, sf.cpp, &raw, &color); if (rc) return rc;
            }
            if (!(op->flags & GFX2_F_FILTER) && ex->sv.fmt == ex->dv.fmt) value = raw;
            else { rc = gfx2_from_argb(ex->dv.fmt, color, &value); if (rc) return rc; }
            if (ex->dv.sync) {
                gfx2_px_write(bytes, ex->dv.fmt, value);
                rc = gfxsync_write_rows(ex->dv.sync, x, y, 1, 1, bytes, df.cpp);
                if (rc) return rc;
            } else gfx2_px_write(ex->dv.base + y * ex->dv.pitch + x * df.cpp, ex->dv.fmt, value);
        }
    }
    return 0;
}
