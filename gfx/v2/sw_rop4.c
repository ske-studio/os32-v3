/* Software ROP4: mask selects the foreground or background ROP3. */
#include "gfx2_sw.h"

static int row_read(const struct gfx2_view *v, u32 x, u32 y, u32 w,
                    u32 cpp, u8 *buf)
{
    u32 i, n = w * cpp;
    if (v->sync)
        return gfxsync_read_rows(v->sync, x, y, w, 1, buf, n);
    for (i = 0; i < n; ++i)
        buf[i] = v->base[y * v->pitch + x * cpp + i];
    return 0;
}

static int row_write(const struct gfx2_view *v, u32 x, u32 y, u32 w,
                     u32 cpp, const u8 *buf)
{
    u32 i, n = w * cpp;
    if (v->sync)
        return gfxsync_write_rows(v->sync, x, y, w, 1, buf, n);
    for (i = 0; i < n; ++i)
        v->base[y * v->pitch + x * cpp + i] = buf[i];
    return 0;
}

static u32 rop3(u8 rop, u32 p, u32 s, u32 d)
{
    u32 r = 0, i;
    for (i = 0; i < 8; ++i)
        if (rop & (1u << i))
            r |= (i & 4 ? p : ~p) & (i & 2 ? s : ~s) &
                 (i & 1 ? d : ~d);
    return r;
}

int gfx2_sw_rop4(const struct gfx2_exec *ex, struct gfx2_rect *touched)
{
    const GFX2_Op *op = ex->op;
    const struct gfx2_rect *c = &ex->clip;
    struct gfx2_fmt_info si, di;
    i32 row, step, end, x, minx = 0, miny = 0, maxx = 0, maxy = 0;
    int wrote = 0, rc;
    u32 sx, sy, n;

    *touched = (struct gfx2_rect){0, 0, 0, 0};
    if (c->w <= 0 || c->h <= 0)
        return 0;
    if (gfx2_fmt_info(ex->sv.fmt, &si) || gfx2_fmt_info(ex->dv.fmt, &di) ||
        !si.cpp || !di.cpp || (u32)c->w > GFX2_SW_ROW_BYTES / si.cpp ||
        (u32)c->w > GFX2_SW_ROW_BYTES / di.cpp)
        return OS32_ERR_INVAL;
    sx = (u32)(op->sx + c->x - op->dx);
    sy = (u32)(op->sy + c->y - op->dy);
    row = 0; step = 1; end = c->h;
    if (ex->src == ex->dst && c->y > (i32)sy) {
        row = c->h - 1; step = -1; end = -1;
    }
    for (; row != end; row += step) {
        u32 y = (u32)(c->y + row);
        const u8 *mask = ex->cpu + (u32)(c->y + row - op->dy) * ex->cpu_pitch;
        rc = row_read(&ex->sv, sx, sy + (u32)row, (u32)c->w, si.cpp, gfx2_sw_rowbuf[0]);
        if (rc) return rc;
        rc = row_read(&ex->dv, (u32)c->x, y, (u32)c->w, di.cpp, gfx2_sw_rowbuf[1]);
        if (rc) return rc;
        for (x = 0; x < c->w; ++x) {
            u32 px = (u32)(c->x + x), p = (u32)op->fg.v;
            u32 bit = (u32)(c->x + x - op->dx);
            u8 rop = (mask[bit >> 3] & (0x80u >> (bit & 7))) ?
                     (u8)op->rop : (u8)(op->rop >> 8);
            if (op->flags & GFX2_F_PAT) {
                n = (y & 7u) * 8u + (px & 7u);
                if (ex->pat->kind == 2) p = ex->pat->color[n];
                else if (ex->pat->mono[y & 7u] & (0x80u >> (px & 7u))) p = (u32)ex->pat->fg.v;
                else {
                    if (ex->pat->bg.x & GFX2_CX_TRANSPARENT) continue;
                    p = (u32)ex->pat->bg.v;
                }
            }
            gfx2_px_write(&gfx2_sw_rowbuf[1][(u32)x * di.cpp], ex->dv.fmt,
                rop3(rop, p, gfx2_px_read(&gfx2_sw_rowbuf[0][(u32)x * si.cpp], ex->sv.fmt),
                     gfx2_px_read(&gfx2_sw_rowbuf[1][(u32)x * di.cpp], ex->dv.fmt)));
            if (!wrote) { minx = maxx = (i32)px; miny = maxy = (i32)y; wrote = 1; }
            else {
                if ((i32)px < minx) minx = (i32)px;
                if ((i32)px > maxx) maxx = (i32)px;
                if ((i32)y < miny) miny = (i32)y;
                if ((i32)y > maxy) maxy = (i32)y;
            }
        }
        rc = row_write(&ex->dv, (u32)c->x, y, (u32)c->w, di.cpp, gfx2_sw_rowbuf[1]);
        if (rc) return rc;
    }
    if (wrote) *touched = (struct gfx2_rect){minx, miny, maxx - minx + 1, maxy - miny + 1};
    return 0;
}
