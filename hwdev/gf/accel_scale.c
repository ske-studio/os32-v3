/* SIFM sequence: xf86-video-nouveau/src/nv04_xv_blit.c:74-154 (MIT). */
#include "accel.h"

/* xf86-video-nouveau/src/nv04_xv_blit.c:74-154; hwdefs/nv01_2d.xml.h:890-934. */
enum { SCALE_FRAC_BITS = 20, SCALE_INTEGER_BITS = 12,
       SIFM_BIND = 0x0000, SIFM_DMA_IMAGE = 0x0184,
       SIFM_COLOR_FORMAT = 0x0300, GDI_COLOR1_A = 0x03fc,
       SIFM_CLIP_POINT = 0x0308, SIFM_SIZE = 0x0400,
       SIFM_SRCCOPY = 3, SIFM_ORIGIN_CENTER = 0x00010000,
       SIFM_FILTER_BILINEAR = 0x01000000 };

/* The quotient has 20 fractional bits. Long division avoids a runtime
 * 64-bit division dependency while computing floor((u64)n << 20)/d. */
static int scale_ratio(u32 n, u32 d, u32 *out)
{
    u32 whole = n / d, rem = n % d, fraction = 0;
    if (whole >= (1U << SCALE_INTEGER_BITS)) return GF3_ERANGE;
    for (unsigned i = 0; i < SCALE_FRAC_BITS; ++i) {
        rem <<= 1;
        fraction <<= 1;
        if (rem >= d) { rem -= d; fraction |= 1; }
    }
    *out = (whole << SCALE_FRAC_BITS) | fraction;
    return *out ? 0 : GF3_ERANGE;
}

int gf3_scale_emit(struct gf3_ctx *c, const struct gf3_surface *src,
              const struct gf3_surface *dst, struct gf3_rect sr,
              struct gf3_rect dr, struct gf3_rect clip, bool bilinear, u32 dl)
{
    struct gf3_format sf, df, cf;
    struct gf3_span ss, ds, cs;
    u32 du, dv, v[6];
    int rc;
    if (!c) return GF3_EINVAL;
    if (!c->ch || !src || !dst) { rc = GF3_EINVAL; goto done; }
    rc = gf2_fault_check(c->ch); if (rc) goto done;
    if (!c->ready || c->epoch != c->ch->epoch || c->ch->state != GF2_RUN) {
        rc = GF3_ESTATE; goto done;
    }
    if (!sr.w || !sr.h || !dr.w || !dr.h || !clip.w || !clip.h) { rc = 0; goto done; }
    rc = gf3_surface_check(c, src, sr, GF3_USE_SIFM_SRC, &sf, &ss); if (rc) goto done;
    rc = gf3_surface_check(c, dst, dr, GF3_USE_2D, &df, &ds); if (rc) goto done;
    rc = gf3_surface_check(c, dst, clip, GF3_USE_2D, &cf, &cs); if (rc) goto done;
    if (dst->fmt == GF3_INDEX8) { rc = GF3_EFORMAT; goto done; }
    if ((src->dma ? src->dma : GF3_HANDLE_VRAM_DMA) ==
        (dst->dma ? dst->dma : GF3_HANDLE_VRAM_DMA) &&
        ((src->off <= dst->off && dst->off - src->off < src->bytes) ||
         (dst->off < src->off && src->off - dst->off < dst->bytes))) {
        rc = GF3_EUNSUPPORTED; goto done;
    }
    rc = scale_ratio(sr.w, dr.w, &du); if (rc) goto done;
    rc = scale_ratio(sr.h, dr.h, &dv); if (rc) goto done;
    rc = gf3_surface_set(c, dst, dst, false, dl); if (rc) goto done;
    v[0] = c->obj[GF3_OBJ_SIFM].handle;
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, SIFM_BIND, false, v, 1, dl); if (rc) goto done;
    v[0] = src->dma ? src->dma : GF3_HANDLE_VRAM_DMA;
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, SIFM_DMA_IMAGE, false, v, 1, dl); if (rc) goto done;
    v[0] = sf.sifm; v[1] = SIFM_SRCCOPY; /* nv04_xv_blit.c:115-117 SRCCOPY */
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, SIFM_COLOR_FORMAT, false, v, 2, dl); if (rc) goto done;
    v[0] = 0;
    rc = gf2_emit(c->ch, GF3_SUBC_GDI, GDI_COLOR1_A, false, v, 1, dl); if (rc) goto done;
    v[0] = (clip.y << 16) | clip.x; v[1] = (clip.h << 16) | clip.w;
    v[2] = (dr.y << 16) | dr.x; v[3] = (dr.h << 16) | dr.w;
    v[4] = du; v[5] = dv;
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, SIFM_CLIP_POINT, false, v, 6, dl); if (rc) goto done;
    v[0] = (src->height << 16) | src->width;
    v[1] = src->pitch | SIFM_ORIGIN_CENTER | (bilinear ? SIFM_FILTER_BILINEAR : 0);
    v[2] = src->off; v[3] = ((sr.y << 4) << 16) | (sr.x << 4);
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, SIFM_SIZE, false, v, 4, dl); if (rc) goto done;
done:
    c->last_rc = rc;
    return rc;
}

int gf3_scale(struct gf3_ctx *c, const struct gf3_surface *src,
              const struct gf3_surface *dst, struct gf3_rect sr,
              struct gf3_rect dr, struct gf3_rect clip, bool bilinear, u32 dl)
{
    int rc = gf3_scale_emit(c, src, dst, sr, dr, clip, bilinear, dl);
    struct gf2_fence fence;
    if (rc || !sr.w || !sr.h || !dr.w || !dr.h || !clip.w || !clip.h) return rc;
    rc = gf2_fence_emit(c->ch, &fence, dl);
    if (!rc) rc = gf2_fence_wait(c->ch, fence, dl);
    return c->last_rc = rc;
}
