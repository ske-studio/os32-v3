/* Surface formats and byte bounds: GF_FEAT3_DESIGN section 9 and G2.
 * Values: xf86-video-nouveau nv_accel_common.c:254-276,
 * nv04_exa.c:97-129,180-200,302-319, nv04_xv_blit.c:68-117. */
#include "accel.h"

/* xf86-video-nouveau/src/nv04_exa.c:193-200: SF2D FORMAT method. */
#define GF3_SF2D_FORMAT 0x0300U

/* nouveau_exa.c:434-435; nouveau_xv.c:929-942 (linear NV4x).
 * These are drawing contracts, independent of M2MF and scanout. */
#define GF3_DRAW_PITCH_ALIGN 64U
#define GF3_DRAW_BASE_ALIGN 256U

static const struct gf3_format formats[] = {
    {1, 1, 1, 3, 3, 0, 0}, {2, 4, 4, 1, 1, 1, 7},
    {4, 6, 6, 3, 3, 4, 4}, {4, 10, 11, 3, 3, 4, 3},
    {1, 1, 1, 3, 3, 0, 8}, {2, 0, 0, 0, 0, 0, 5},
    {2, 0, 0, 0, 0, 0, 6}
};
int gf3_format_get(enum gf3_fmt fmt, struct gf3_format *out)
{
    if (!out) return GF3_EINVAL;
    if ((unsigned)fmt >= sizeof(formats)/sizeof(formats[0])) return GF3_EFORMAT;
    *out = formats[fmt];
    return 0;
}
int gf3_span_check(u32 off, u32 capacity, u32 pitch, u32 rowbytes,
                   u32 rows, struct gf3_span *out)
{
    if (!out) return GF3_EINVAL;
    if (off > capacity || pitch < rowbytes) return GF3_ERANGE;
    u32 bytes = 0;
    if (rowbytes && rows) {
        if (rowbytes > capacity-off || rows-1 > (capacity-off-rowbytes)/pitch)
            return GF3_ERANGE;
        bytes = (rows-1)*pitch+rowbytes;
    }
    *out = (struct gf3_span){off, rowbytes, pitch, rows, bytes};
    return 0;
}
int gf3_surface_check(struct gf3_ctx *c, const struct gf3_surface *s,
                      struct gf3_rect r, enum gf3_surface_use use,
                      struct gf3_format *fmt, struct gf3_span *span)
{
    struct gf_vram_reservation key;
    struct gf3_format f;
    struct gf3_span full, part;
    if (!c || !c->ch || !s || !fmt || !span || (unsigned)use > GF3_USE_SIFM_SRC)
        return GF3_EINVAL;
    int rc = gf_vram_match(c->ch, 0, s->reservation_id, s->generation,
                           s->off, s->bytes, &key);
    if (rc) return rc;
    rc = gf3_format_get(s->fmt, &f);
    if (rc) return rc;
    if (!s->width || !s->height || s->width > U32_MAX/f.cpp ||
        s->off > U32_MAX-s->bytes) return GF3_ERANGE;
    rc = gf3_span_check(0, s->bytes, s->pitch, s->width*f.cpp, s->height, &full);
    if (rc) return rc;
    if (r.x > s->width || r.w > s->width-r.x ||
        r.y > s->height || r.h > s->height-r.y) return GF3_ERANGE;
    if ((use == GF3_USE_2D && !f.sf) || (use == GF3_USE_GDI && (!f.sf_gdi || !f.gdi)) ||
        (use == GF3_USE_IFC && !f.ifc) || (use == GF3_USE_SIFM_SRC && !f.sifm))
        return GF3_EFORMAT;
    if (use != GF3_USE_BYTES) {
        if (s->pitch > U16_MAX ||
            (s->pitch & (GF3_DRAW_PITCH_ALIGN - 1U)) ||
            (s->off & (GF3_DRAW_BASE_ALIGN - 1U))) return GF3_ERANGE;
        if (use == GF3_USE_SIFM_SRC) {
            if (s->width > U16_MAX || s->height > U16_MAX ||
                r.x > (U16_MAX >> 4) || r.y > (U16_MAX >> 4)) return GF3_ERANGE;
            if ((s->fmt == GF3_YUY2 || s->fmt == GF3_UYVY) &&
                ((s->width | r.x | r.w) & 1U)) return GF3_ERANGE;
        } else if (r.x > U16_MAX || r.y > U16_MAX || r.w > U16_MAX || r.h > U16_MAX)
            return GF3_ERANGE;
    }
    u32 xbytes = r.x*f.cpp;
    if (r.y && s->pitch > U32_MAX/r.y) return GF3_ERANGE;
    u32 rel = r.y*s->pitch;
    if (xbytes > U32_MAX-rel) return GF3_ERANGE;
    rel += xbytes;
    rc = gf3_span_check(rel, s->bytes, s->pitch, r.w*f.cpp, r.h, &part);
    if (rc) return rc;
    part.off += s->off;
    *fmt = f; *span = part;
    return 0;
}
int gf3_ram_check(const struct gf3_ram *ram, u32 rowbytes, u32 rows,
                  struct gf3_span *out)
{
    if (!ram || !out || (rowbytes && rows && !ram->ptr)) return GF3_EINVAL;
    return gf3_span_check(0, ram->bytes, ram->pitch, rowbytes, rows, out);
}
int gf3_surface_set(struct gf3_ctx *c, const struct gf3_surface *src,
                    const struct gf3_surface *dst, bool gdi, u32 dl)
{
    if (!c || !c->ch || !src || !dst) return GF3_EINVAL;
    struct gf3_format sf, df;
    struct gf3_span ss, ds;
    enum gf3_surface_use use = gdi ? GF3_USE_GDI : GF3_USE_2D;
    int rc = gf3_surface_check(c, src, (struct gf3_rect){0,0,src->width,src->height}, use, &sf, &ss);
    if (rc) return rc;
    rc = gf3_surface_check(c, dst, (struct gf3_rect){0,0,dst->width,dst->height}, use, &df, &ds);
    if (rc) return rc;
    if (src->fmt != dst->fmt) return GF3_EFORMAT;
    /* nv04_exa.c:118-129,180-200: SF format, packed pitches, offsets. */
    u32 values[4] = {gdi ? sf.sf_gdi : sf.sf,
                     (dst->pitch << 16) | src->pitch, src->off, dst->off};
    return gf2_emit(c->ch, GF3_SUBC_SURFACE2D, GF3_SF2D_FORMAT, false, values, 4, dl);
}
