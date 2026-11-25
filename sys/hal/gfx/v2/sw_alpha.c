/* HAL v2 premultiplied source-over, HAL_V2_DESIGN section 3-3. */
#include "gfx2_sw.h"

/* C9 owns this storage and will put its declaration in gfx2_sw.h. */
#define ALPHA_ROW_BYTES 16384u
extern u8 gfx2_sw_rowbuf[2][ALPHA_ROW_BYTES] __attribute__((weak));

static u32 alpha_component(u32 s, u32 d, u32 ca, u32 inv)
{
    return gfx2_div255(s * ca) + gfx2_div255(d * inv);
}

static u32 nearest(u32 i, u32 sw, u32 dw)
{
    u32 n = i * sw;
    return n / dw + ((n % dw) * 2u + sw) / (2u * dw);
}

int gfx2_sw_alpha(const struct gfx2_exec *ex, struct gfx2_rect *touched)
{
    const GFX2_Op *op = ex->op;
    struct gfx2_fmt_info sf, df;
    i32 x, y;
    u32 ca = op->fg.x & 255u;
    int rc;

    *touched = (struct gfx2_rect){0, 0, 0, 0};
    if ((op->flags & GFX2_F_FILTER) || !op->dw || !op->dh ||
        gfx2_fmt_info(ex->sv.fmt, &sf) || gfx2_fmt_info(ex->dv.fmt, &df))
        return OS32_ERR_INVAL;
    if (ex->clip.w <= 0 || ex->clip.h <= 0) return 0;
    if ((ex->sv.sync || ex->dv.sync) && !gfx2_sw_rowbuf) return OS32_ERR_NOSYS;
    if ((ex->dv.sync && (u32)ex->clip.w * df.cpp > ALPHA_ROW_BYTES) ||
        (ex->sv.sync && (u32)op->sw * sf.cpp > ALPHA_ROW_BYTES)) return OS32_ERR_INVAL;
    for (y = ex->clip.y; y < ex->clip.y + ex->clip.h; y++) {
        u32 sy = (u32)op->sy + nearest((u32)(y - op->dy), op->sh, op->dh);
        const u8 *src;
        u8 *dst;
        if (ex->sv.sync) {
            rc = gfxsync_read_rows(ex->sv.sync, op->sx, sy, op->sw, 1, gfx2_sw_rowbuf[0], op->sw * sf.cpp);
            if (rc) return rc;
            src = gfx2_sw_rowbuf[0];
        } else src = ex->sv.base + sy * ex->sv.pitch + op->sx * sf.cpp;
        if (ex->dv.sync) {
            rc = gfxsync_read_rows(ex->dv.sync, ex->clip.x, y, ex->clip.w, 1, gfx2_sw_rowbuf[1], ex->clip.w * df.cpp);
            if (rc) return rc;
            dst = gfx2_sw_rowbuf[1];
        } else dst = ex->dv.base + (u32)y * ex->dv.pitch + (u32)ex->clip.x * df.cpp;
        for (x = ex->clip.x; x < ex->clip.x + ex->clip.w; x++) {
            u32 sx = nearest((u32)(x - op->dx), op->sw, op->dw);
            u8 *dp = dst + (u32)(x - ex->clip.x) * df.cpp;
            u32 s = gfx2_to_argb(ex->sv.fmt, gfx2_px_read(src + sx * sf.cpp, ex->sv.fmt), ex->sv.pal);
            u32 d = gfx2_to_argb(ex->dv.fmt, gfx2_px_read(dp, ex->dv.fmt), ex->dv.pal);
            u32 a = (op->flags & GFX2_F_ALPHA_PIXEL) ? s >> 24 : 255u;
            u32 inv = 255u - gfx2_div255(a * ca);
            u32 out = 0, raw, shift;
            for (shift = 0; shift < 32; shift += 8) {
                u32 sc = shift == 24 ? a : (s >> shift) & 255u;
                u32 c = alpha_component(sc, (d >> shift) & 255u, ca, inv);
                if (c > 255u) c = 255u;
                out |= c << shift;
            }
            rc = gfx2_from_argb(ex->dv.fmt, out, &raw);
            if (rc) return rc;
            gfx2_px_write(dp, ex->dv.fmt, raw);
        }
        if (ex->dv.sync) {
            rc = gfxsync_write_rows(ex->dv.sync, ex->clip.x, y, ex->clip.w, 1, dst, ex->clip.w * df.cpp);
            if (rc) return rc;
        }
    }
    *touched = ex->clip;
    return 0;
}
