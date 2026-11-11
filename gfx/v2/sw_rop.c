/* Software ROP3 for FILL and COPY; inputs and clipping are checked by submit. */
#include "gfx2_sw.h"

static u32 rop3(u32 p, u32 s, u32 d, u32 rop)
{
    u32 out = 0;
    for (u32 i = 0; i < 8; ++i) {
        u32 mask = (i & 4 ? p : ~p) & (i & 2 ? s : ~s) & (i & 1 ? d : ~d);
        if (rop & (1u << i)) out |= mask;
    }
    return out;
}

int gfx2_sw_rop(const struct gfx2_exec *ex, struct gfx2_rect *touched)
{
    const GFX2_Op *op = ex->op;
    const struct gfx2_rect r = ex->clip;
    struct gfx2_fmt_info df, sf;
    int copy = op->op == GFX2_OP_COPY;
    int reverse = copy && ex->src == ex->dst && op->dy > op->sy;
    int rc;
    *touched = (struct gfx2_rect){0, 0, 0, 0};
    if (r.w <= 0 || r.h <= 0) return 0;
    rc = gfx2_fmt_info(ex->dv.fmt, &df);
    if (rc) return rc;
    if (copy) {
        rc = gfx2_fmt_info(ex->sv.fmt, &sf);
        if (rc) return rc;
    }
    for (i32 row = 0; row < r.h; ++row) {
        i32 y = r.y + (reverse ? r.h - 1 - row : row);
        u32 cap = GFX2_SW_ROW_BYTES / (copy && sf.cpp > df.cpp ? sf.cpp : df.cpp);
        int backwards = copy && ex->src == ex->dst && op->dx > op->sx;
        for (u32 pos = 0; pos < (u32)r.w;) {
            u32 n = (u32)r.w - pos;
            if (n > cap) n = cap;
            u32 off = backwards ? (u32)r.w - pos - n : pos;
            i32 left = r.x + (i32)off;
            u32 sx = (u32)(op->sx + left - op->dx);
            u32 sy = (u32)(op->sy + y - op->dy);
            u8 *d = ex->dv.sync ? gfx2_sw_rowbuf[0] : ex->dv.base + y * ex->dv.pitch + left * df.cpp;
            u8 *s = 0;
            if (ex->dv.sync) {
                rc = gfxsync_read_rows(ex->dv.sync, left, y, n, 1, d, n * df.cpp);
                if (rc) return rc;
            }
            if (copy) {
                s = gfx2_sw_rowbuf[1];
                if (ex->sv.sync) {
                    rc = gfxsync_read_rows(ex->sv.sync, sx, sy, n, 1, s, n * sf.cpp);
                    if (rc) return rc;
                } else {
                    const u8 *src = ex->sv.base + sy * ex->sv.pitch + sx * sf.cpp;
                    for (u32 i = 0; i < n * sf.cpp; ++i) s[i] = src[i];
                }
            }
            i32 first = -1, last = -1;
            for (i32 x = 0; x < (i32)n; ++x) {
                u32 p = op->fg.v;
                if (op->flags & GFX2_F_PAT) {
                    const struct gfx2_pattern *pat = ex->pat;
                    u32 px = (u32)(left + x) & 7, py = (u32)y & 7;
                    if (pat->kind == 1) {
                        if (pat->mono[py] & (0x80u >> px)) p = pat->fg.v;
                        else if (pat->bg.x & GFX2_CX_TRANSPARENT) continue;
                        else p = pat->bg.v;
                    } else p = pat->color[py * 8 + px];
                }
                u32 old = gfx2_px_read(d + x * df.cpp, ex->dv.fmt);
                u32 src = copy ? gfx2_px_read(s + x * sf.cpp, ex->sv.fmt) : 0;
                gfx2_px_write(d + x * df.cpp, ex->dv.fmt, rop3(p, src, old, op->rop));
                if (first < 0) first = x;
                last = x;
            }
            if (ex->dv.sync && first >= 0) {
                rc = gfxsync_write_rows(ex->dv.sync, left, y, n, 1, d, n * df.cpp);
                if (rc) return rc;
            }
            if (first >= 0) {
                i32 x0 = left + first, x1 = left + last + 1;
                if (!touched->h) *touched = (struct gfx2_rect){x0, y, x1 - x0, 1};
                else {
                    i32 right = touched->x + touched->w, bottom = touched->y + touched->h;
                    if (x0 < touched->x) touched->x = x0;
                    if (x1 > right) right = x1;
                    if (y < touched->y) touched->y = y;
                    if (y + 1 > bottom) bottom = y + 1;
                    touched->w = right - touched->x; touched->h = bottom - touched->y;
                }
            }
            pos += n;
        }
    }
    return 0;
}
