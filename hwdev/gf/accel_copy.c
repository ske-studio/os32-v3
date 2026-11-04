/* IMAGE_BLIT copy: xf86-video-nouveau/src/nv04_exa.c:170-200,255-258.
 * OS32's alias admission rules are in GF_FEAT3_DESIGN.md B4 addendum. */
#include "accel.h"

#define GF3_BLIT_POINT_IN 0x0300U /* nv04_exa.c:255-258 */

static bool copy_overlap(const struct gf3_span *a, const struct gf3_span *b)
{
    u32 ai = 0, bi = 0, ap = a->off, bp = b->off;
    while (ai < a->rows && bi < b->rows) {
        u32 ae = ap + a->rowbytes, be = bp + b->rowbytes;
        if (ap < be && bp < ae) return true;
        if (ae <= bp) {
            if (++ai < a->rows) ap += a->pitch;
        } else {
            if (++bi < b->rows) bp += b->pitch;
        }
    }
    return false;
}

int gf3_copy(struct gf3_ctx *c, const struct gf3_surface *src,
             const struct gf3_surface *dst, struct gf3_rect sr,
             u32 dx, u32 dy, u8 alu, u32 mask, u32 dl)
{
    struct gf3_format sf, df;
    struct gf3_span ss, ds, full;
    struct gf2_fence fence;
    int rc;
    if (!c) return GF3_EINVAL;
    if (!c->ch || !src || !dst) { rc = GF3_EINVAL; goto out; }
    if (!c->ready || c->epoch != c->ch->epoch) {
        rc = GF3_ESTATE; goto out;
    }
    rc = gf2_fault_check(c->ch);
    if (rc) goto out;
    if (c->ch->state != GF2_RUN) { rc = GF3_ESTATE; goto out; }
    if (!sr.w || !sr.h) { rc = 0; goto out; }

    rc = gf3_surface_check(c, src, (struct gf3_rect){0, 0, src->width, src->height}, GF3_USE_2D, &sf, &full);
    if (rc) goto out;
    rc = gf3_surface_check(c, src, sr, GF3_USE_2D, &sf, &ss);
    if (rc) goto out;
    rc = gf3_surface_check(c, dst, (struct gf3_rect){0, 0, dst->width, dst->height}, GF3_USE_2D, &df, &full);
    if (rc) goto out;
    rc = gf3_surface_check(c, dst, (struct gf3_rect){dx, dy, sr.w, sr.h}, GF3_USE_2D, &df, &ds);
    if (rc) goto out;
    if (src->fmt != dst->fmt) { rc = GF3_EFORMAT; goto out; }
    if (copy_overlap(&ss, &ds) &&
        (src->reservation_id != dst->reservation_id || src->generation != dst->generation ||
         src->off != dst->off || src->pitch != dst->pitch || src->fmt != dst->fmt)) {
        rc = GF3_EUNSUPPORTED; goto out;
    }
    rc = gf3_rop_set(c, GF3_SUBC_BLIT, alu, mask, dst->fmt, dl);
    if (rc) goto out;
    rc = gf3_surface_set(c, src, dst, false, dl);
    if (rc) goto out;
    u32 words[3] = { (sr.y << 16) | sr.x, (dy << 16) | dx, (sr.h << 16) | sr.w };
    rc = gf2_emit(c->ch, GF3_SUBC_BLIT, GF3_BLIT_POINT_IN, false, words, 3, dl);
    if (rc) goto out;
    rc = gf2_fence_emit(c->ch, &fence, dl);
    if (!rc) rc = gf2_fence_wait(c->ch, fence, dl);
out:
    c->last_rc = rc;
    return rc;
}
