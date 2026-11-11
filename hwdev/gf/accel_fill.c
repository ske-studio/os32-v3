/* Solid rectangles: xf86-video-nouveau/src/nv04_exa.c:118-129,153-157;
 * nouveau-v6.1/nv04_fbcon.c:64-71. See GF_FEAT3_DESIGN.md section 4. */
#include "accel.h"

/* NV04 GDI methods, nv04_exa.c:123-129,153-157. */
#define GF3_GDI_COLOR_FORMAT 0x0300U
#define GF3_GDI_COLOR        0x03fcU
#define GF3_GDI_RECTANGLE    0x0400U

int gf3_fill_emit(struct gf3_ctx *c, const struct gf3_surface *s, struct gf3_rect r,
             u32 color, u8 alu, u32 mask, u32 dl)
{
    struct gf3_format fmt;
    struct gf3_span span;
    u32 v[2];
    int rc;

    if (!c) return GF3_EINVAL;
    if (!c->ch || !s) return c->last_rc = GF3_EINVAL;
    if (!c->ready || c->epoch != c->ch->epoch)
        return c->last_rc = GF3_ESTATE;
    rc = gf2_fault_check(c->ch);
    if (rc) return c->last_rc = rc;
    if (c->ch->state != GF2_RUN) return c->last_rc = GF3_ESTATE;
    if (!r.w || !r.h) return c->last_rc = GF3_OK;

    rc = gf3_surface_check(c, s, r, GF3_USE_GDI, &fmt, &span);
    if (rc) return c->last_rc = rc;
    rc = gf3_rop_set(c, GF3_SUBC_GDI, alu, mask, s->fmt, dl);
    if (rc) return c->last_rc = rc;
    rc = gf3_surface_set(c, s, s, true, dl);
    if (rc) return c->last_rc = rc;
    v[0] = fmt.gdi;
    rc = gf2_emit(c->ch, GF3_SUBC_GDI, GF3_GDI_COLOR_FORMAT, false, v, 1, dl);
    if (rc) return c->last_rc = rc;
    v[0] = color;
    rc = gf2_emit(c->ch, GF3_SUBC_GDI, GF3_GDI_COLOR, false, v, 1, dl);
    if (rc) return c->last_rc = rc;
    v[0] = (r.x << 16) | r.y;
    v[1] = (r.w << 16) | r.h;
    rc = gf2_emit(c->ch, GF3_SUBC_GDI, GF3_GDI_RECTANGLE, false, v, 2, dl);
    if (rc) return c->last_rc = rc;
    return c->last_rc = GF3_OK;
}

int gf3_fill(struct gf3_ctx *c, const struct gf3_surface *s, struct gf3_rect r,
             u32 color, u8 alu, u32 mask, u32 dl)
{
    int rc = gf3_fill_emit(c, s, r, color, alu, mask, dl);
    struct gf2_fence fence;
    if (rc || !r.w || !r.h) return rc;
    rc = gf2_fence_emit(c->ch, &fence, dl);
    if (!rc) rc = gf2_fence_wait(c->ch, fence, dl);
    return c->last_rc = rc;
}
