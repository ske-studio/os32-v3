/* Layer 3 host fixture; include in the test TU only, alongside one product
 * TU. GF_FEAT3_DESIGN.md G1 owns the API and UNIT dependency matrix.
 * Reference images use little-endian pixels; callers exclude XRGB's X byte
 * from comparisons. All image/trace storage except staging is borrowed. */
#ifndef TOOLS_TESTS_GF_ACCEL_HOST_H
#define TOOLS_TESTS_GF_ACCEL_HOST_H
#include "gf_command_runtime_host.h"
#include "../../hwdev/gf/accel.h"
#if !defined(GF3_HOST_UNIT) || GF3_HOST_UNIT < 0 || GF3_HOST_UNIT > 12
#error "GF3_HOST_UNIT must select B0..B10, C0, or no stubs (0..12)"
#endif

enum gf3_host_op { H_EMIT, H_FENCE, H_WAIT, H_FAULT, H_PAUSE, H_RESUME, H_STOP, H_ALLOC, H_FREE, H_DMA_NEW, H_GR_NEW, H_DEL, H_MATCH, H_FORMAT, H_SPAN, H_SURFACE_CHECK, H_RAM_CHECK, H_SURFACE_SET, H_ROP, H_IFC, H_M2MF, H_UPLOAD, H_FILL, H_COPY, H_READ, H_WRITE, H_CLOCK, H_SLEEP, H_OP_COUNT };
struct gf3_host_reg { u32 bar, off, value; };
struct gf3_host_packet { u32 first, deadline; u16 method, count; u8 subc; bool noninc; };
struct gf3_host_event { enum gf3_host_op op; u32 nth, a[12]; const void *p[4]; int rc; };
struct gf3_host_image { u8 *ptr; u32 bytes, pitch, width, height; enum gf3_fmt fmt; };
struct gf3_host {
    struct gf_foundation f; struct gf2_chan ch; struct gf3_ctx ctx;
    struct gf3_host_reg *regs, *writes; u32 cregs, cwrites, nregs, nwrites;
    struct gf3_host_packet *packets; u32 cpackets, npackets; u32 *words; u32 cwords, nwords;
    struct gf3_host_event *events; u32 cevents, nevents, calls[H_OP_COUNT], fail_at[H_OP_COUNT]; int fail_rc[H_OP_COUNT];
    u8 staging[GF3_STAGING_BYTES]; u8 *vram; u32 vram_bytes, now, complete_after, waits;
    struct gf_vram_reservation_node *reservations; bool paused; u32 live_objects, alloc_off;
    void (*observe)(struct gf3_host *, const struct gf3_host_event *);
};
/* Only one fixture is active in a host process. Some APIs have no owner. */
static struct gf3_host *gf3_host_current;
static inline struct gf3_host_event *gf3_host_event_begin(struct gf3_host *h, enum gf3_host_op op)
{
    assert(h && (unsigned)op < H_OP_COUNT && h->events && h->nevents < h->cevents);
    struct gf3_host_event *e = &h->events[h->nevents++];
    gf2_host_memset(e, 0, sizeof(*e));
    e->op = op; e->nth = ++h->calls[op];
    if (h->fail_rc[op] && (!h->fail_at[op] || h->fail_at[op] == e->nth))
        e->rc = h->fail_rc[op];
    return e;
}
static inline int gf3_host_event_end(struct gf3_host *h, struct gf3_host_event *e)
{
    if (h->observe) h->observe(h, e);
    return e->rc;
}
static inline void gf3_host_trace_clear(struct gf3_host *h)
{
    assert(h);
    h->nwrites = h->npackets = h->nwords = h->nevents = h->waits = 0;
    gf2_host_memset(h->calls, 0, sizeof(h->calls));
}
static inline void gf3_host_fail_at(struct gf3_host *h, enum gf3_host_op op, u32 nth, int rc)
{
    assert(h && (unsigned)op < H_OP_COUNT && op != H_FREE);
    h->fail_at[op] = nth; h->fail_rc[op] = rc;
}
static inline void gf3_host_set(struct gf3_host *h, u32 bar, u32 off, u32 value)
{
    assert(h);
    for (u32 i = 0; i < h->nregs; ++i)
        if (h->regs[i].bar == bar && h->regs[i].off == off) { h->regs[i].value = value; return; }
    assert(h->regs && h->nregs < h->cregs);
    h->regs[h->nregs++] = (struct gf3_host_reg){bar, off, value};
}
static inline int gf3_host_read(void *owner, u32 bar, u32 off, u32 width, u32 *out)
{
    struct gf3_host *h = owner; assert(h && out && width == 4);
    struct gf3_host_event *e = gf3_host_event_begin(h, H_READ);
    e->p[0] = owner; e->p[1] = out; e->a[0] = bar; e->a[1] = off; e->a[2] = width;
    if (!e->rc) { *out = 0; for (u32 i = 0; i < h->nregs; ++i)
        if (h->regs[i].bar == bar && h->regs[i].off == off) { *out = h->regs[i].value; break; } }
    return gf3_host_event_end(h, e);
}
static inline int gf3_host_write(void *owner, u32 bar, u32 off, u32 width, u32 value)
{
    struct gf3_host *h = owner; assert(h && width == 4);
    struct gf3_host_event *e = gf3_host_event_begin(h, H_WRITE);
    e->p[0] = owner; e->a[0] = bar; e->a[1] = off; e->a[2] = width; e->a[3] = value;
    if (!e->rc) { assert(h->writes && h->nwrites < h->cwrites);
        h->writes[h->nwrites++] = (struct gf3_host_reg){bar, off, value}; gf3_host_set(h, bar, off, value); }
    return gf3_host_event_end(h, e);
}
static inline int gf3_host_clock(void *owner, u32 *now)
{
    struct gf3_host *h = owner; assert(h && now);
    struct gf3_host_event *e = gf3_host_event_begin(h, H_CLOCK); e->p[0] = owner; e->p[1] = now;
    if (!e->rc) *now = h->now;
    return gf3_host_event_end(h, e);
}
static inline int gf3_host_wait(void *owner, u32 us)
{
    struct gf3_host *h = owner; assert(h);
    struct gf3_host_event *e = gf3_host_event_begin(h, H_SLEEP); e->p[0] = owner; e->a[0] = us;
    if (!e->rc) { h->now += us; ++h->waits; }
    return gf3_host_event_end(h, e);
}
static inline void gf3_host_init(struct gf3_host *h)
{
    assert(h); gf2_host_memset(h, 0, sizeof(*h)); gf3_host_current = h;
    h->f.io.opaque = h; h->f.io.read = gf3_host_read; h->f.io.write = gf3_host_write;
    h->f.io.timing.clock_us = gf3_host_clock; h->f.io.timing.wait_us = gf3_host_wait;
    h->ch.f = &h->f; h->ch.state = GF2_RUN; h->ch.epoch = 1;
    h->ch.staging_live = true; h->ch.staging.va = h->staging; h->ch.staging.size = sizeof(h->staging);
    h->ch.vram_dma.handle = GF3_HANDLE_VRAM_DMA; h->ch.pci_dma.handle = GF3_HANDLE_PCI_DMA;
    h->ctx.ch = &h->ch; h->ctx.epoch = h->ch.epoch; h->ctx.ready = true;
    for (u32 i = 0; i < GF3_OBJECT_COUNT; ++i) h->ctx.obj[i].handle = GF2_HANDLE_LAYER3_FIRST + i;
}
static inline u32 gf3_host_cpp(enum gf3_fmt fmt)
{
    switch (fmt) { case GF3_INDEX8: case GF3_Y8: return 1;
    case GF3_RGB565: case GF3_YUY2: case GF3_UYVY: return 2;
    case GF3_XRGB8888: case GF3_ARGB8888: return 4; default: assert(0); return 0; }
}
static inline void gf3_host_image_check(const struct gf3_host_image *im)
{
    assert(im && im->ptr); u32 cpp = gf3_host_cpp(im->fmt);
    assert(im->width && im->height && im->width <= U32_MAX / cpp && im->pitch >= im->width * cpp);
    assert(im->bytes >= im->width * cpp &&
        im->height - 1 <= (im->bytes - im->width * cpp) / im->pitch);
    assert((uptr)im->ptr <= U32_MAX - (im->bytes - 1));
}
/* Compare byte envelopes as integers: unrelated C pointers are not ordered. */
static inline bool gf3_host_overlap(const void *a, u32 na, const void *b, u32 nb)
{
    if (!na || !nb) return false;
    uptr pa = (uptr)a, pb = (uptr)b;
    assert(a && b && pa <= U32_MAX - (na - 1) && pb <= U32_MAX - (nb - 1));
    return pa <= pb ? pb - pa < na : pa - pb < nb;
}
static inline void gf3_host_rect_check(const struct gf3_host_image *im, struct gf3_rect r)
{
    gf3_host_image_check(im); assert(r.x <= im->width && r.w <= im->width-r.x && r.y <= im->height && r.h <= im->height-r.y);
}
static inline u8 gf3_host_rop_byte(u8 rop, u8 pattern, u8 src, u8 dst)
{
    u8 v = 0; for (unsigned b = 0; b < 8; ++b) {
        unsigned i = (((pattern >> b) & 1U) << 2) | (((src >> b) & 1U) << 1) | ((dst >> b) & 1U);
        v |= (u8)(((rop >> i) & 1U) << b);
    } return v;
}
static inline void gf3_host_ref_fill(struct gf3_host_image *dst, struct gf3_rect r, u32 color, u32 mask, u8 rop)
{
    gf3_host_rect_check(dst, r); u32 cpp = gf3_host_cpp(dst->fmt);
    assert(dst->fmt != GF3_YUY2 && dst->fmt != GF3_UYVY);
    assert(cpp != 4 || (rop == 0xcc && mask == U32_MAX));
    for (u32 y=0; y<r.h; ++y) for (u32 x=0; x<r.w; ++x) for (u32 b=0; b<cpp; ++b) {
        u8 *p = dst->ptr + (r.y+y)*dst->pitch + (r.x+x)*cpp+b;
        *p = gf3_host_rop_byte(rop, (u8)(mask>>(8*b)), (u8)(color>>(8*b)), *p);
    }
}
static inline void gf3_host_ref_copy(const struct gf3_host_image *before, struct gf3_host_image *dst, struct gf3_rect sr, u32 dx, u32 dy, u32 mask, u8 rop)
{
    gf3_host_rect_check(before, sr); gf3_host_rect_check(dst, (struct gf3_rect){dx,dy,sr.w,sr.h});
    assert(!gf3_host_overlap(before->ptr, before->bytes, dst->ptr, dst->bytes) && before->fmt == dst->fmt);
    u32 cpp=gf3_host_cpp(dst->fmt);
    assert(dst->fmt != GF3_YUY2 && dst->fmt != GF3_UYVY);
    assert(cpp != 4 || (rop == 0xcc && mask == U32_MAX));
    for (u32 y=0; y<sr.h; ++y) for (u32 x=0; x<sr.w; ++x) for (u32 b=0; b<cpp; ++b) {
        u8 *p=dst->ptr+(dy+y)*dst->pitch+(dx+x)*cpp+b;
        u8 s=before->ptr[(sr.y+y)*before->pitch+(sr.x+x)*cpp+b];
        *p=gf3_host_rop_byte(rop,(u8)(mask>>(8*b)),s,*p);
    }
}
static inline void gf3_host_ref_mono(struct gf3_host_image *dst, struct gf3_rect r, const struct gf3_ram *bits, u32 fg, u32 bg, struct gf3_rect clip)
{
    gf3_host_image_check(dst);
    assert(bits && bits->ptr && r.w && r.h && r.w <= U32_MAX-7);
    assert(r.x <= U32_MAX-r.w && r.y <= U32_MAX-r.h);
    u32 row=(r.w+7)/8, cpp=gf3_host_cpp(dst->fmt);
    assert(dst->fmt != GF3_YUY2 && dst->fmt != GF3_UYVY);
    assert(bits->pitch>=row && (!r.h || (bits->bytes>=row && r.h-1 <= (bits->bytes-row)/bits->pitch)));
    assert((uptr)bits->ptr <= U32_MAX - (bits->bytes - 1));
    assert(clip.x <= U32_MAX-clip.w && clip.y <= U32_MAX-clip.h);
    for (u32 y=0;y<r.h;++y) for (u32 x=0;x<r.w;++x) {
        u32 px=r.x+x, py=r.y+y;
        if (px>=dst->width || py>=dst->height || px<clip.x || px>=clip.x+clip.w || py<clip.y || py>=clip.y+clip.h) continue;
        u32 c=(((u8 *)bits->ptr)[y*bits->pitch+x/8] & (0x80U>>(x%8))) ? fg:bg;
        for (u32 b=0;b<cpp;++b) dst->ptr[py*dst->pitch+px*cpp+b]=(u8)(c>>(8*b));
    }
}
static inline void gf3_host_ref_ifc(struct gf3_host_image *dst, struct gf3_rect r, const struct gf3_ram *ram)
{
    gf3_host_rect_check(dst,r); assert(ram && ram->ptr && r.w && r.h); u32 row=r.w*gf3_host_cpp(dst->fmt);
    assert(ram->pitch>=row && (!r.h || (ram->bytes>=row && r.h-1 <= (ram->bytes-row)/ram->pitch)));
    assert((uptr)ram->ptr <= U32_MAX - (ram->bytes - 1));
    for (u32 y=0;y<r.h;++y) gf2_host_memcpy(dst->ptr+(r.y+y)*dst->pitch+r.x*gf3_host_cpp(dst->fmt),(u8 *)ram->ptr+y*ram->pitch,row);
}
static inline void gf3_host_ref_m2mf(const struct gf3_ram *src, u32 src_off, struct gf3_ram *dst, u32 dst_off, u32 rowbytes, u32 rows)
{
    assert(src && dst && src->ptr && dst->ptr && src->pitch>=rowbytes && dst->pitch>=rowbytes);
    assert(src_off<=src->bytes && dst_off<=dst->bytes);
    if (!rows || !rowbytes) return;
    assert(rowbytes<=src->bytes-src_off && rows-1 <= (src->bytes-src_off-rowbytes)/src->pitch);
    assert(rowbytes<=dst->bytes-dst_off && rows-1 <= (dst->bytes-dst_off-rowbytes)/dst->pitch);
    u32 ns = (rows-1)*src->pitch+rowbytes, nd = (rows-1)*dst->pitch+rowbytes;
    assert((uptr)src->ptr <= U32_MAX-src_off && (uptr)dst->ptr <= U32_MAX-dst_off);
    assert(!gf3_host_overlap((u8 *)src->ptr+src_off, ns, (u8 *)dst->ptr+dst_off, nd));
    for (u32 y=0;y<rows;++y) gf2_host_memcpy((u8 *)dst->ptr+dst_off+y*dst->pitch,(u8 *)src->ptr+src_off+y*src->pitch,rowbytes);
}
/* Binary long division avoids an i386 libgcc dependency in -nostdlib tests. */
static inline unsigned long long gf3_host_div_u64_u32(unsigned long long n, u32 d)
{
    assert(d);
    unsigned long long q = 0, r = 0;
    for (unsigned bit = 64; bit; ) {
        --bit;
        r = (r << 1) | ((n >> bit) & 1ULL);
        q <<= 1;
        if (r >= d) { r -= d; q |= 1ULL; }
    }
    return q;
}
/* The fixed-point reference accepts RGB point samples and constant-color
 * bilinear inputs only; the hardware packet remains independently tested. */
static inline void gf3_host_ref_scale(const struct gf3_host_image *src, struct gf3_host_image *dst, struct gf3_rect sr, struct gf3_rect dr, struct gf3_rect clip, bool bilinear)
{
    gf3_host_rect_check(src, sr); gf3_host_rect_check(dst, dr);
    assert(sr.w && sr.h && dr.w && dr.h && src->fmt == dst->fmt);
    assert(src->fmt == GF3_RGB565 || src->fmt == GF3_XRGB8888 || src->fmt == GF3_ARGB8888);
    assert(clip.x <= U32_MAX-clip.w && clip.y <= U32_MAX-clip.h);
    gf3_host_rect_check(dst, clip);
    assert(!gf3_host_overlap(src->ptr, src->bytes, dst->ptr, dst->bytes));
    u32 cpp=gf3_host_cpp(src->fmt);
    if (bilinear) for (u32 y=0;y<src->height;++y) for (u32 x=0;x<src->width;++x)
        for (u32 b=0;b<cpp;++b)
            if (src->fmt != GF3_XRGB8888 || b != 3)
                assert(src->ptr[y*src->pitch+x*cpp+b] == src->ptr[b]);
    unsigned long long du=gf3_host_div_u64_u32((unsigned long long)sr.w << 20, dr.w);
    unsigned long long dv=gf3_host_div_u64_u32((unsigned long long)sr.h << 20, dr.h);
    assert(du && dv && du <= U32_MAX && dv <= U32_MAX);
    for (u32 y=0;y<dr.h;++y) for (u32 x=0;x<dr.w;++x) {
        u32 px=dr.x+x, py=dr.y+y;
        if (px<clip.x || px>=clip.x+clip.w || py<clip.y || py>=clip.y+clip.h) continue;
        u32 sx=sr.x+(u32)((x*du)>>20), sy=sr.y+(u32)((y*dv)>>20);
        for (u32 b=0;b<cpp;++b) dst->ptr[py*dst->pitch+px*cpp+b]=src->ptr[sy*src->pitch+sx*cpp+b];
    }
}
/* These value helpers never call a product helper or create dependency events.
 * A SURFACE_CHECK event describes one dependency call, not a B1 call tree. */
static inline int gf3_host_format(enum gf3_fmt fmt, struct gf3_format *out)
{
    if (!out) return GF3_EINVAL;
    /* Independent expected rows: design section 9; nv_accel_common.c:254-276,
     * nv04_exa.c:97-129,302-319 and nv04_xv_blit.c:68-93,99-117. */
    switch (fmt) {
    case GF3_INDEX8:   *out = (struct gf3_format){1, 1, 1, 3, 3, 0, 0}; break;
    case GF3_RGB565:   *out = (struct gf3_format){2, 4, 4, 1, 1, 1, 7}; break;
    case GF3_XRGB8888: *out = (struct gf3_format){4, 6, 6, 3, 3, 4, 4}; break;
    case GF3_ARGB8888: *out = (struct gf3_format){4, 10, 11, 3, 3, 4, 3}; break;
    case GF3_Y8:       *out = (struct gf3_format){1, 1, 1, 3, 3, 0, 8}; break;
    case GF3_YUY2:     *out = (struct gf3_format){2, 0, 0, 0, 0, 0, 5}; break;
    case GF3_UYVY:     *out = (struct gf3_format){2, 0, 0, 0, 0, 0, 6}; break;
    default: return GF3_EFORMAT;
    }
    return 0;
}
static inline int gf3_host_span(u32 off, u32 capacity, u32 pitch,
                               u32 rowbytes, u32 rows, struct gf3_span *out)
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
static inline int gf3_host_match(struct gf3_host *h, struct gf2_chan *ch,
                                const void *owner, u32 id, u32 generation,
                                u32 off, u32 bytes, struct gf_vram_reservation *out)
{
    if (!ch || !out || !bytes) return GF3_EINVAL;
    if (ch != &h->ch) return GF3_ESTATE;
    for (struct gf_vram_reservation_node *n = h->reservations; n; n = n->next) {
        const struct gf_vram_reservation *v = &n->value;
        if (!v->owner || !id || !generation || v->reservation_id != id ||
            v->generation != generation || (owner && owner != v->owner)) continue;
        if (off < v->off || off-v->off > v->bytes || bytes > v->bytes-(off-v->off))
            return GF3_ERANGE;
        *out = *v;
        return 0;
    }
    return GF3_ESTATE;
}
static inline int gf3_host_surface(struct gf3_host *h, struct gf3_ctx *c,
                                  const struct gf3_surface *s, struct gf3_rect r,
                                  enum gf3_surface_use use, struct gf3_format *fmt,
                                  struct gf3_span *span)
{
    struct gf_vram_reservation key;
    struct gf3_format f;
    struct gf3_span full, part;
    if (!c || !c->ch || !s || !fmt || !span || (unsigned)use > GF3_USE_SIFM_SRC)
        return GF3_EINVAL;
    int rc = gf3_host_match(h, c->ch, 0, s->reservation_id, s->generation,
                            s->off, s->bytes, &key);
    if (rc) return rc;
    rc = gf3_host_format(s->fmt, &f);
    if (rc) return rc;
    if (!s->width || !s->height || s->width > U32_MAX/f.cpp ||
        s->off > U32_MAX-s->bytes) return GF3_ERANGE;
    rc = gf3_host_span(0, s->bytes, s->pitch, s->width*f.cpp, s->height, &full);
    if (rc) return rc;
    if (r.x > s->width || r.w > s->width-r.x ||
        r.y > s->height || r.h > s->height-r.y) return GF3_ERANGE;
    if ((use == GF3_USE_2D && !f.sf) || (use == GF3_USE_GDI && !f.sf_gdi) ||
        (use == GF3_USE_IFC && !f.ifc) || (use == GF3_USE_SIFM_SRC && !f.sifm))
        return GF3_EFORMAT;
    if (use != GF3_USE_BYTES) {
        if (s->pitch > U16_MAX) return GF3_ERANGE;
        if (use == GF3_USE_SIFM_SRC) {
            /* nv04_xv_blit.c:74-93: 16bit full size, 12.4 origin. */
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
    rc = gf3_host_span(rel, s->bytes, s->pitch, r.w*f.cpp, r.h, &part);
    if (rc) return rc;
    part.off += s->off;
    *fmt = f;
    *span = part;
    return 0;
}
static inline int gf3_host_ram(const struct gf3_ram *ram, u32 rowbytes, u32 rows,
                              struct gf3_span *out)
{
    if (!ram || !out || (rowbytes && rows && !ram->ptr)) return GF3_EINVAL;
    return gf3_host_span(0, ram->bytes, ram->pitch, rowbytes, rows, out);
}
static inline struct gf3_host_image gf3_host_surface_image(struct gf3_host *h,
                                                         const struct gf3_surface *s)
{
    assert(s && h->vram && s->off <= h->vram_bytes && s->bytes <= h->vram_bytes-s->off);
    assert(h->vram_bytes && (uptr)h->vram <= U32_MAX-(h->vram_bytes-1));
    struct gf3_host_image im = {h->vram+s->off, s->bytes, s->pitch, s->width, s->height, s->fmt};
    gf3_host_image_check(&im);
    return im;
}
static inline void gf3_host_event_rect(struct gf3_host_event *e, u32 first,
                                      struct gf3_rect r)
{
    assert(first <= 12-4);
    e->a[first] = r.x; e->a[first+1] = r.y;
    e->a[first+2] = r.w; e->a[first+3] = r.h;
}

#if GF3_HOST_UNIT == 0
/* F1 instmem.c:8-59: immovable linked nodes, rounded size, and free retains
 * off/size. alloc_off replaces F1's fixed aperture start in this fixture. */
static inline int gf3_host_inst_alloc(struct gf3_host *h, u32 bytes, u32 align,
                                     bool zero, struct gf_inst *out)
{
    assert(out);
    if (!bytes || bytes > U32_MAX-3 || (align && (align & (align-1))))
        return GF1_EINVAL;
    bytes = (bytes+3) & ~3U;
    if (align < sizeof(u32)) align = sizeof(u32);
    struct gf_inst **link = &h->f.imem.used;
    u32 pos = h->alloc_off;
    for (struct gf_inst *n = *link; n; n = n->next) assert(n != out);
    for (;;) {
        if (pos > U32_MAX-(align-1)) return GF1_ENOSPC;
        pos = (pos+align-1) & ~(align-1);
        if (pos > h->f.imem.aperture_bytes || bytes > h->f.imem.aperture_bytes-pos)
            return GF1_ENOSPC;
        if (!*link || (pos <= (*link)->off && bytes <= (*link)->off-pos)) break;
        assert((*link)->off <= U32_MAX-(*link)->size);
        if (pos < (*link)->off+(*link)->size) pos = (*link)->off+(*link)->size;
        link = &(*link)->next;
    }
    if (zero) {
        assert(h->f.bar2);
        for (u32 i = 0; i < bytes; ++i) h->f.bar2[pos+i] = 0;
    }
    *out = (struct gf_inst){pos, bytes, *link};
    *link = out;
    return 0;
}
static inline bool gf3_host_inst_free(struct gf3_host *h, struct gf_inst *obj)
{
    struct gf_imem *im = &h->f.imem;
    if (obj == &im->vbios || obj == &im->ramht || obj == &im->ramro || obj == &im->ramfc)
        return false;
    struct gf_inst **link = &im->used;
    while (*link && *link != obj) link = &(*link)->next;
    if (!*link) return false;
    *link = obj->next;
    obj->next = 0;
    return true;
}
static inline u32 gf3_host_cookie(struct gf3_host *h)
{
    for (u32 i = 0; i < GF2_RAMHT_ENTRIES; ++i) {
        u8 mask = (u8)(1U << (i%8));
        if (!(h->ch.bitmap[i/8] & mask)) {
            h->ch.bitmap[i/8] |= mask;
            return i+1;
        }
    }
    return 0;
}
int gf2_fifo_pause(struct gf2_chan *ch)
{
    struct gf3_host *h = gf3_host_current;
    assert(h && ch == &h->ch);
    struct gf3_host_event *e = gf3_host_event_begin(h, H_PAUSE);
    e->p[0] = ch;
    if (!e->rc) h->paused = true;
    return gf3_host_event_end(h, e);
}
void gf2_fifo_resume(struct gf2_chan *ch)
{
    struct gf3_host *h = gf3_host_current;
    assert(h && ch == &h->ch);
    struct gf3_host_event *e = gf3_host_event_begin(h, H_RESUME);
    e->p[0] = ch;
    if (!e->rc) e->rc = gf2_fault_check(ch);
    /* fifo.c:232-238: a callback failure records last_rc, never latches. */
    if (e->rc) ch->last_rc = e->rc;
    else h->paused = false;
    (void)gf3_host_event_end(h, e);
}
int gf2_stop(struct gf2_chan *ch)
{
    struct gf3_host *h = gf3_host_current;
    assert(h && ch == &h->ch);
    struct gf3_host_event *e = gf3_host_event_begin(h, H_STOP);
    e->p[0] = ch;
    if (e->rc) ch->state = GF2_FAULT;
    else {
        /* command.c:46-76: unlink instances without clearing their fields.
         * No context/VRAM reservation belongs to the F2 stop stub. */
        while (h->f.imem.used) {
            struct gf_inst *inst = h->f.imem.used;
            h->f.imem.used = inst->next;
            inst->next = 0;
        }
        h->live_objects = 0;
        gf2_host_memset(ch->bitmap, 0, sizeof(ch->bitmap));
        ch->staging_live = false;
        ch->staging = (struct gf2_dma_buf){0};
        ch->mapped = ch->vram_reserved = false;
        ch->va = 0; ch->map_bytes = 0;
        ch->push_extent = (struct recover_bar_extent){0};
        h->f.running = h->f.registered = h->f.bm = h->f.mapped = false;
        h->f.bar2 = 0; h->f.imem.aperture_bytes = 0;
        h->paused = true;
        ch->state = GF2_OFF;
    }
    ch->last_rc = e->rc;
    return gf3_host_event_end(h, e);
}
int gf_inst_alloc(struct gf_foundation *f, u32 bytes, u32 align, bool zero,
                  struct gf_inst *out)
{
    struct gf3_host *h = gf3_host_current;
    assert(h && f == &h->f);
    struct gf3_host_event *e = gf3_host_event_begin(h, H_ALLOC);
    e->p[0] = f; e->p[1] = out;
    e->a[0] = bytes; e->a[1] = align; e->a[2] = zero;
    if (!e->rc) e->rc = gf3_host_inst_alloc(h, bytes, align, zero, out);
    return gf3_host_event_end(h, e);
}
void gf_inst_free(struct gf_foundation *f, struct gf_inst *obj)
{
    struct gf3_host *h = gf3_host_current;
    assert(h && f == &h->f && obj);
    struct gf3_host_event *e = gf3_host_event_begin(h, H_FREE);
    e->p[0] = f; e->p[1] = obj;
    (void)gf3_host_inst_free(h, obj);
    (void)gf3_host_event_end(h, e);
}
int gf2_dma_new(struct gf2_chan *ch, u32 handle, u16 class_id, u8 target,
                u8 access, u32 start, u32 limit, struct gf2_object *obj)
{
    struct gf3_host *h = gf3_host_current;
    assert(h && ch == &h->ch && obj && h->paused);
    struct gf3_host_event *e = gf3_host_event_begin(h, H_DMA_NEW);
    e->p[0] = ch; e->p[1] = obj;
    e->a[0] = handle; e->a[1] = class_id; e->a[2] = target;
    e->a[3] = access; e->a[4] = start; e->a[5] = limit;
    if (!e->rc && (!handle || (class_id != GF2_DMA_FROM_MEMORY &&
        class_id != GF2_DMA_TO_MEMORY && class_id != GF2_DMA_IN_MEMORY) ||
        target > GF2_DMA_TARGET_PCI || access > GF2_DMA_ACCESS_RW || start > limit))
        e->rc = GF2_EINVAL;
    /* ENOSPC models allocation failure; EIO models descriptor writes.
     * dmaobj.c:33-46 sets metadata before writes, frees but retains inst. */
    if (e->rc != GF2_ENOSPC && e->rc != GF2_EINVAL && e->rc != GF2_ECLASS) {
        int rc = gf3_host_inst_alloc(h, GF2_DMA_OBJECT_BYTES, GF2_OBJECT_ALIGN, false, &obj->inst);
        if (rc) {
            if (!e->rc) e->rc = rc == GF1_ENOSPC ? GF2_ENOSPC : GF2_EIO;
        }
        else {
            obj->handle = handle; obj->class_id = class_id; obj->cookie = 0;
            if (!e->rc && handle != GF2_HANDLE_PUSH_DMA) {
                obj->cookie = gf3_host_cookie(h);
                if (!obj->cookie) e->rc = GF2_ENOSPC;
            }
            if (e->rc) {
                (void)gf3_host_inst_free(h, &obj->inst);
                obj->handle = 0;
            } else ++h->live_objects;
        }
    }
    return gf3_host_event_end(h, e);
}
int gf2_gr_object_new(struct gf2_chan *ch, u32 handle, u16 class_id, struct gf2_object *obj)
{
    struct gf3_host *h = gf3_host_current;
    assert(h && ch == &h->ch && obj && h->paused);
    struct gf3_host_event *e = gf3_host_event_begin(h, H_GR_NEW);
    e->p[0] = ch; e->p[1] = obj; e->a[0] = handle; e->a[1] = class_id;
    bool supported = false;
    /* gr.c:157-164 / nouveau nv44.c:83-100: the complete class whitelist. */
    switch (class_id) {
    case GF2_GR_BETA1: case GF2_GR_CLIP: case GF2_GR_NULL: case GF2_GR_M2MF:
    case GF2_GR_ROP: case GF2_GR_PATTERN: case GF2_GR_GDI: case GF2_GR_SURFACE2D:
    case GF2_GR_BETA4: case GF2_GR_SIFM: case GF2_GR_IFC: case GF2_GR_IMAGEBLIT:
    case GF2_GR_SURFACE2D_NV30: case GF2_GR_SIFM_NV30: case GF2_GR_SWZSURF:
    case GF2_GR_CURIE: supported = true; break;
    }
    if (!e->rc && !supported) e->rc = GF2_ECLASS;
    if (e->rc != GF2_ENOSPC && e->rc != GF2_ECLASS && e->rc != GF2_EINVAL) {
        int rc = gf3_host_inst_alloc(h, GF2_GR_OBJECT_BYTES, GF2_OBJECT_ALIGN, false, &obj->inst);
        if (rc) {
            if (!e->rc) e->rc = rc == GF1_ENOSPC ? GF2_ENOSPC : GF2_EIO;
        }
        else {
            if (!e->rc) {
                u32 cookie = gf3_host_cookie(h);
                if (!cookie) e->rc = GF2_ENOSPC;
                else obj->cookie = cookie;
            }
            if (e->rc) {
                (void)gf3_host_inst_free(h, &obj->inst);
                obj->inst = (struct gf_inst){0};
            } else {
                obj->handle = handle; obj->class_id = class_id;
                ++h->live_objects;
            }
        }
    }
    return gf3_host_event_end(h, e);
}
int gf2_object_del(struct gf2_chan *ch, struct gf2_object *obj)
{
    struct gf3_host *h = gf3_host_current;
    assert(h && ch == &h->ch && obj && h->paused);
    struct gf3_host_event *e = gf3_host_event_begin(h, H_DEL);
    e->p[0] = ch; e->p[1] = obj;
    if (!e->rc) {
        if (obj->cookie) {
            assert(obj->cookie <= GF2_RAMHT_ENTRIES);
            u32 slot = obj->cookie-1;
            if (!(ch->bitmap[slot/8] & (1U << (slot%8)))) e->rc = GF2_EINVAL;
            else {
                ch->bitmap[slot/8] &= (u8)~(1U << (slot%8));
                obj->cookie = 0;
            }
        }
        if (!e->rc) {
            if (gf3_host_inst_free(h, &obj->inst)) {
                assert(h->live_objects);
                --h->live_objects;
            }
            obj->handle = 0;
        }
    }
    return gf3_host_event_end(h, e);
}
#endif

#if GF3_HOST_UNIT <= 9
int gf2_fault_check(struct gf2_chan *ch)
{
    struct gf3_host *h=ch->f->io.opaque; assert(ch==&h->ch);
    struct gf3_host_event *e=gf3_host_event_begin(h,H_FAULT); e->p[0]=ch;
    if (e->rc) { ch->fault.latched=true; ch->state=GF2_FAULT; ch->last_rc=e->rc; }
    else if (ch->fault.latched || ch->state==GF2_FAULT) e->rc=GF2_EFAULT;
    return gf3_host_event_end(h,e);
}
#endif
#if GF3_HOST_UNIT <= 7 || GF3_HOST_UNIT == 9
int gf2_emit(struct gf2_chan *ch, u8 subc, u16 method, bool noninc, const u32 *data, u16 count, u32 deadline)
{
    struct gf3_host *h=ch->f->io.opaque; assert(ch==&h->ch && data && count);
    assert(count <= GF2_METHOD_COUNT_MAX && subc <= GF2_SUBCHANNEL_MAX &&
           !(method & (sizeof(u32)-1)) && method <= GF2_METHOD_MAX &&
           (noninc || method+(count-1U)*sizeof(u32) <= GF2_METHOD_MAX));
    struct gf3_host_event *e=gf3_host_event_begin(h,H_EMIT);
    e->p[0]=ch; e->p[1]=data; e->a[0]=subc; e->a[1]=method; e->a[2]=noninc; e->a[3]=count; e->a[4]=deadline;
    if (!e->rc && (ch->fault.latched || ch->state == GF2_FAULT)) e->rc = GF2_EFAULT;
    if (!e->rc && ch->state != GF2_RUN) e->rc = GF2_ESTATE;
    if (!e->rc) {
        assert(h->packets && h->npackets<h->cpackets && h->words && h->nwords<=h->cwords && count<=h->cwords-h->nwords);
        h->packets[h->npackets++]=(struct gf3_host_packet){h->nwords,deadline,method,count,subc,noninc};
        for (u32 i=0;i<count;++i) h->words[h->nwords++]=data[i];
    }
    return gf3_host_event_end(h,e);
}
#endif
#if GF3_HOST_UNIT == 0 || (GF3_HOST_UNIT >= 3 && GF3_HOST_UNIT <= 8) || GF3_HOST_UNIT == 9
int gf2_fence_emit(struct gf2_chan *ch, struct gf2_fence *out, u32 deadline)
{
    struct gf3_host *h=ch->f->io.opaque; assert(ch==&h->ch && out);
    struct gf3_host_event *e=gf3_host_event_begin(h,H_FENCE); e->p[0]=ch; e->p[1]=out; e->a[0]=deadline;
    /* fence.c:21-39: state failures precede ++seq; submission failures
     * consume the sequence, but never publish an output fence. */
    if (!e->rc && (ch->fault.latched || ch->state == GF2_FAULT)) e->rc = GF2_EFAULT;
    if (!e->rc && ch->state != GF2_INIT && ch->state != GF2_RUN && ch->state != GF2_DRAIN)
        e->rc = GF2_ESTATE;
    if (e->rc != GF2_EFAULT && e->rc != GF2_ESTATE) {
        u32 seq = ++ch->seq;
        if (!e->rc) *out=(struct gf2_fence){ch->epoch,seq};
    }
    return gf3_host_event_end(h,e);
}
int gf2_fence_wait(struct gf2_chan *ch, struct gf2_fence fence, u32 deadline)
{
    struct gf3_host *h=ch->f->io.opaque; assert(ch==&h->ch);
    struct gf3_host_event *e=gf3_host_event_begin(h,H_WAIT); e->p[0]=ch; e->a[0]=fence.epoch; e->a[1]=fence.seq; e->a[2]=deadline;
    for (u32 n=0; !e->rc; ++n) {
        u32 now;
        int rc=gf2_fault_check(ch); if (rc) { e->rc=rc; break; }
        if (ch->state!=GF2_INIT && ch->state!=GF2_RUN && ch->state!=GF2_DRAIN) { e->rc=GF2_ESTATE; break; }
        if (fence.epoch!=ch->epoch) { e->rc=GF2_EFAULT; break; }
        if (gf3_host_clock(h,&now)) { e->rc=GF2_EIO; break; }
        if ((i32)(now-deadline)>=0) { e->rc=GF2_ETIME; break; }
        if (n>=h->complete_after && h->complete_after!=U32_MAX) break;
        if (gf3_host_wait(h,1)) { e->rc=GF2_EIO; break; }
    }
    return gf3_host_event_end(h,e);
}
#endif

#if GF3_HOST_UNIT == 1
int gf_vram_match(struct gf2_chan *ch, const void *owner, u32 id, u32 generation,
                  u32 off, u32 bytes, struct gf_vram_reservation *out)
{
    struct gf3_host *h = gf3_host_current;
    struct gf3_host_event *e = gf3_host_event_begin(h, H_MATCH);
    e->p[0] = ch; e->p[1] = owner; e->p[2] = out;
    e->a[0] = id; e->a[1] = generation; e->a[2] = off; e->a[3] = bytes;
    if (!e->rc) e->rc = gf3_host_match(h, ch, owner, id, generation, off, bytes, out);
    return gf3_host_event_end(h, e);
}
#endif
#if (GF3_HOST_UNIT >= 2 && GF3_HOST_UNIT <= 6) || GF3_HOST_UNIT == 8 || GF3_HOST_UNIT == 9
int gf3_format_get(enum gf3_fmt fmt, struct gf3_format *out)
{
    struct gf3_host *h = gf3_host_current;
    struct gf3_host_event *e = gf3_host_event_begin(h, H_FORMAT);
    e->p[0] = out; e->a[0] = fmt;
    if (!e->rc) e->rc = gf3_host_format(fmt, out);
    return gf3_host_event_end(h, e);
}
#endif
#if GF3_HOST_UNIT >= 3 && GF3_HOST_UNIT <= 9
int gf3_span_check(u32 off, u32 capacity, u32 pitch, u32 rowbytes, u32 rows,
                   struct gf3_span *out)
{
    struct gf3_host *h = gf3_host_current;
    struct gf3_host_event *e = gf3_host_event_begin(h, H_SPAN);
    e->p[0] = out; e->a[0] = off; e->a[1] = capacity;
    e->a[2] = pitch; e->a[3] = rowbytes; e->a[4] = rows;
    if (!e->rc) e->rc = gf3_host_span(off, capacity, pitch, rowbytes, rows, out);
    return gf3_host_event_end(h, e);
}
#endif
#if (GF3_HOST_UNIT >= 3 && GF3_HOST_UNIT <= 6) || GF3_HOST_UNIT == 8 || GF3_HOST_UNIT == 9
int gf3_surface_check(struct gf3_ctx *c, const struct gf3_surface *s,
                      struct gf3_rect r, enum gf3_surface_use use,
                      struct gf3_format *fmt, struct gf3_span *span)
{
    struct gf3_host *h = gf3_host_current;
    struct gf3_host_event *e = gf3_host_event_begin(h, H_SURFACE_CHECK);
    e->p[0] = c; e->p[1] = s; e->p[2] = fmt; e->p[3] = span;
    gf3_host_event_rect(e, 0, r); e->a[4] = use;
    if (!e->rc) e->rc = gf3_host_surface(h, c, s, r, use, fmt, span);
    return gf3_host_event_end(h, e);
}
int gf3_ram_check(const struct gf3_ram *ram, u32 rowbytes, u32 rows, struct gf3_span *out)
{
    struct gf3_host *h = gf3_host_current;
    struct gf3_host_event *e = gf3_host_event_begin(h, H_RAM_CHECK);
    e->p[0] = ram; e->p[1] = out; e->a[0] = rowbytes; e->a[1] = rows;
    if (!e->rc) e->rc = gf3_host_ram(ram, rowbytes, rows, out);
    return gf3_host_event_end(h, e);
}
#endif
#if (GF3_HOST_UNIT >= 3 && GF3_HOST_UNIT <= 6) || GF3_HOST_UNIT == 9
int gf3_surface_set(struct gf3_ctx *c, const struct gf3_surface *src,
                    const struct gf3_surface *dst, bool gdi, u32 dl)
{
    struct gf3_host *h = gf3_host_current;
    struct gf3_host_event *e = gf3_host_event_begin(h, H_SURFACE_SET);
    e->p[0] = c; e->p[1] = src; e->p[2] = dst; e->a[0] = gdi; e->a[1] = dl;
    return gf3_host_event_end(h, e);
}
#endif
#if GF3_HOST_UNIT == 3 || GF3_HOST_UNIT == 4
int gf3_rop_set(struct gf3_ctx *c, u8 subc, u8 alu, u32 mask, enum gf3_fmt fmt, u32 dl)
{
    struct gf3_host *h = gf3_host_current;
    struct gf3_host_event *e = gf3_host_event_begin(h, H_ROP);
    e->p[0] = c; e->a[0] = subc; e->a[1] = alu;
    e->a[2] = mask; e->a[3] = fmt; e->a[4] = dl;
    return gf3_host_event_end(h, e);
}
#endif

#if GF3_HOST_UNIT == 8
static inline int gf3_host_sync(struct gf3_ctx *c, u32 dl)
{
    struct gf2_fence fence;
    int rc = gf2_fence_emit(c->ch, &fence, dl);
    return rc ? rc : gf2_fence_wait(c->ch, fence, dl);
}
int gf3_ifc(struct gf3_ctx *c, const struct gf3_surface *s, struct gf3_rect r,
            const struct gf3_ram *ram, u32 dl)
{
    struct gf3_host *h = gf3_host_current;
    struct gf3_host_event *e = gf3_host_event_begin(h, H_IFC);
    e->p[0] = c; e->p[1] = s; e->p[2] = ram;
    gf3_host_event_rect(e, 0, r); e->a[4] = dl;
    assert(c && c->ch == &h->ch && s && ram);
    if (!e->rc && r.w && r.h) {
        struct gf3_host_image im = gf3_host_surface_image(h, s);
        gf3_host_rect_check(&im, r);
        u32 rowbytes = r.w*gf3_host_cpp(s->fmt);
        /* Direct IFC conditions, design section 7 / nv04_exa.c:296-319.
         * The 16KiB heuristic belongs only to the real B8 upload selector. */
        if (s->fmt != GF3_RGB565 && s->fmt != GF3_XRGB8888 && s->fmt != GF3_ARGB8888)
            e->rc = GF3_EFORMAT;
        else if (rowbytes < GF3_IFC_MIN_ROW_BYTES || r.h > GF3_IFC_MAX_HEIGHT ||
            rowbytes > U32_MAX-(GF3_IFC_ROW_ALIGN-1) ||
            ((rowbytes+GF3_IFC_ROW_ALIGN-1) & ~(GF3_IFC_ROW_ALIGN-1))/sizeof(u32) > GF3_SCRATCH_WORDS)
            e->rc = GF3_EUNSUPPORTED;
        else {
            struct gf3_span span;
            assert(!gf3_host_ram(ram, rowbytes, r.h, &span));
            e->rc = gf3_host_sync(c, dl);
            if (!e->rc) gf3_host_ref_ifc(&im, r, ram);
        }
    }
    c->last_rc = e->rc;
    return gf3_host_event_end(h, e);
}
static inline struct gf3_ram gf3_host_dma_ram(struct gf3_host *h, u32 handle, u32 pitch)
{
    if (handle == GF3_HANDLE_VRAM_DMA) {
        assert(h->vram && h->f.imem.reserved <= h->f.imem.vram_bytes);
        u32 bytes = h->f.imem.vram_bytes-h->f.imem.reserved;
        if (bytes > h->ch.B) bytes = h->ch.B;
        assert(bytes <= h->vram_bytes);
        assert(bytes && (uptr)h->vram <= U32_MAX-(bytes-1));
        return (struct gf3_ram){h->vram, bytes, pitch};
    }
    assert(handle == GF3_HANDLE_PCI_DMA && h->ch.staging_live &&
           h->ch.staging.va && h->ch.staging.size <= GF3_STAGING_BYTES);
    assert(h->ch.staging.size && (uptr)h->ch.staging.va <= U32_MAX-(h->ch.staging.size-1));
    return (struct gf3_ram){h->ch.staging.va, h->ch.staging.size, pitch};
}
int gf3_m2mf(struct gf3_ctx *c, u32 in_handle, u32 out_handle, u32 in_off,
             u32 out_off, u32 in_pitch, u32 out_pitch, u32 rowbytes, u32 rows, u32 dl)
{
    struct gf3_host *h = gf3_host_current;
    struct gf3_host_event *e = gf3_host_event_begin(h, H_M2MF);
    e->p[0] = c;
    e->a[0] = in_handle; e->a[1] = out_handle; e->a[2] = in_off;
    e->a[3] = out_off; e->a[4] = in_pitch; e->a[5] = out_pitch;
    e->a[6] = rowbytes; e->a[7] = rows; e->a[8] = dl;
    assert(c && c->ch == &h->ch);
    if (!e->rc && rowbytes && rows) {
        if ((in_handle != GF3_HANDLE_VRAM_DMA && in_handle != GF3_HANDLE_PCI_DMA) ||
            (out_handle != GF3_HANDLE_VRAM_DMA && out_handle != GF3_HANDLE_PCI_DMA))
            e->rc = GF3_EINVAL;
        else if ((in_handle == GF3_HANDLE_PCI_DMA || out_handle == GF3_HANDLE_PCI_DMA) &&
                 (!h->ch.staging_live || !h->ch.staging.va || !h->ch.staging.size))
            e->rc = GF2_ENOSPC;
        else {
            struct gf3_ram src = gf3_host_dma_ram(h, in_handle, in_pitch);
            struct gf3_ram dst = gf3_host_dma_ram(h, out_handle, out_pitch);
            struct gf3_span in, out;
            /* A malformed fragment from B8 is a fixture assertion, not a
             * synthetic product ENOSPC that could hide a split bug. */
            assert(!gf3_host_span(in_off, src.bytes, src.pitch, rowbytes, rows, &in));
            assert(!gf3_host_span(out_off, dst.bytes, dst.pitch, rowbytes, rows, &out));
            assert(!gf3_host_overlap((u8 *)src.ptr+in_off, in.bytes,
                                     (u8 *)dst.ptr+out_off, out.bytes));
            e->rc = gf3_host_sync(c, dl);
            if (!e->rc) gf3_host_ref_m2mf(&src, in_off, &dst, out_off, rowbytes, rows);
        }
    }
    c->last_rc = e->rc;
    return gf3_host_event_end(h, e);
}
#endif

#if GF3_HOST_UNIT == 10
int gf3_upload(struct gf3_ctx *c, const struct gf3_surface *s, struct gf3_rect r,
               const struct gf3_ram *ram, bool force_m2mf, u32 dl)
{
    struct gf3_host *h = gf3_host_current;
    struct gf3_host_event *e = gf3_host_event_begin(h, H_UPLOAD);
    e->p[0] = c; e->p[1] = s; e->p[2] = ram;
    gf3_host_event_rect(e, 0, r); e->a[4] = force_m2mf; e->a[5] = dl;
    assert(c && c->ch == &h->ch && s && ram);
    if (!e->rc && r.w && r.h) {
        struct gf3_host_image im = gf3_host_surface_image(h, s);
        gf3_host_ref_ifc(&im, r, ram);
    }
    c->last_rc = e->rc;
    return gf3_host_event_end(h, e);
}
#endif

#if GF3_HOST_UNIT == 11
/* GX is a four-bit truth table of (S,D). Expand it independently rather
 * than importing B2's NVROP table; clear/invert/set ignore planemask. */
static inline u8 gf3_host_alu_rop(u8 alu, u32 mask, u32 cpp)
{
    assert(alu < 16);
    u32 full = cpp == 4 ? U32_MAX : ((1U << (cpp*8))-1);
    assert(cpp != 4 || (alu == 3 && (mask & full) == full));
    u8 rop = 0;
    for (unsigned i = 0; i < 8; ++i) {
        unsigned s = (i >> 1) & 1U, d = i & 1U;
        unsigned result = (alu >> (((1U-s) << 1) | (1U-d))) & 1U;
        bool independent = (mask & full) == full || alu == 0 || alu == 10 || alu == 15;
        if (!independent && !(i & 4U)) result = d;
        rop |= (u8)(result << i);
    }
    return rop;
}
int gf3_fill(struct gf3_ctx *c, const struct gf3_surface *s, struct gf3_rect r,
             u32 color, u8 alu, u32 mask, u32 dl)
{
    struct gf3_host *h = gf3_host_current;
    struct gf3_host_event *e = gf3_host_event_begin(h, H_FILL);
    e->p[0] = c; e->p[1] = s; gf3_host_event_rect(e, 0, r);
    e->a[4] = color; e->a[5] = alu; e->a[6] = mask; e->a[7] = dl;
    assert(c && c->ch == &h->ch && s);
    if (!e->rc && r.w && r.h) {
        struct gf3_host_image im = gf3_host_surface_image(h, s);
        u8 rop = gf3_host_alu_rop(alu, mask, gf3_host_cpp(s->fmt));
        gf3_host_ref_fill(&im, r, color, mask, rop);
    }
    c->last_rc = e->rc;
    return gf3_host_event_end(h, e);
}
int gf3_copy(struct gf3_ctx *c, const struct gf3_surface *src,
             const struct gf3_surface *dst, struct gf3_rect sr, u32 dx,
             u32 dy, u8 alu, u32 mask, u32 dl)
{
    struct gf3_host *h = gf3_host_current;
    struct gf3_host_event *e = gf3_host_event_begin(h, H_COPY);
    e->p[0] = c; e->p[1] = src; e->p[2] = dst; gf3_host_event_rect(e, 0, sr);
    e->a[4] = dx; e->a[5] = dy; e->a[6] = alu; e->a[7] = mask; e->a[8] = dl;
    assert(c && c->ch == &h->ch && src && dst);
    if (!e->rc && sr.w && sr.h) {
        struct gf3_host_image si = gf3_host_surface_image(h, src);
        struct gf3_host_image di = gf3_host_surface_image(h, dst);
        gf3_host_rect_check(&si, sr);
        gf3_host_rect_check(&di, (struct gf3_rect){dx, dy, sr.w, sr.h});
        assert(si.fmt == di.fmt);
        u32 cpp = gf3_host_cpp(si.fmt);
        u8 rop = gf3_host_alu_rop(alu, mask, cpp);
        bool alias = gf3_host_overlap(si.ptr, si.bytes, di.ptr, di.bytes);
        if (!alias) gf3_host_ref_copy(&si, &di, sr, dx, dy, mask, rop);
        else {
            /* C0 uses the same scan surface. Preserve a pixel before the
             * write, walking in memmove order; no heap/full-image scratch. */
            assert(si.ptr == di.ptr && si.pitch == di.pitch);
            bool reverse = dy > sr.y || (dy == sr.y && dx > sr.x);
            for (u32 iy = 0; iy < sr.h; ++iy) for (u32 ix = 0; ix < sr.w; ++ix) {
                u32 y = reverse ? sr.h-1-iy : iy, x = reverse ? sr.w-1-ix : ix;
                u8 saved[4];
                for (u32 b = 0; b < cpp; ++b)
                    saved[b] = si.ptr[(sr.y+y)*si.pitch+(sr.x+x)*cpp+b];
                struct gf3_host_image pixel = {saved, cpp, cpp, 1, 1, si.fmt};
                gf3_host_ref_copy(&pixel, &di, (struct gf3_rect){0, 0, 1, 1}, dx+x, dy+y, mask, rop);
            }
        }
    }
    c->last_rc = e->rc;
    return gf3_host_event_end(h, e);
}
#endif
#endif
