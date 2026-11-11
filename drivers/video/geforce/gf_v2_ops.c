/* HAL_V2_DESIGN sections 4, 11-6: adapt checked HAL operations to layer 3.
 * ROP state/draw separation follows MIT xf86-video-nouveau nv04_exa.c:31-85,
 * 141-160. Hardware methods remain in hwdev/gf; no MMIO here. */
#include "gf_v2.h"

struct gf_v2 gf_v2_inst;
#define GFX2_GART_KB 8192u
#define GF_V2_MAX_DIM 4096u
#define GF_V2_PITCH_ALIGN 64u
#define GF_V2_FMT_MASK 0x1eu
#define GF_V2_OP_MASK 0x7feu

void gf_v2_stopped(void)
{
    if (gf_v2_inst.ch && gf_v2_inst.ch->state == GF2_OFF)
        gf_v2_gart_reset(&gf_v2_inst);
}

static int caps(void *ctx, GFX2_Caps *o)
{
    struct gf_v2 *g = ctx;
    *o = (GFX2_Caps){0};
    o->size = sizeof(*o); o->backend_kind = GFX2_KIND_GEFORCE;
    o->tier3d_hw = 3;
    o->caps2d_hw = GFX2_CAP2D(GFX2_OP_FILL) | GFX2_CAP2D(GFX2_OP_COPY) |
        GFX2_CAP2D(GFX2_OP_STRETCH) | GFX2_CAP2D(GFX2_OP_MONO) |
        GFX2_CAP2D(GFX2_OP_HOSTDATA) | GFX2_CAP2D(GFX2_OP_READBACK);
    o->caps2d_sw = GF_V2_OP_MASK;
    o->exact = GF_V2_OP_MASK & ~(GFX2_CAP2D(GFX2_OP_STRETCH) | GFX2_CAP2D(GFX2_OP_ALPHA));
    o->fmt_surface = o->fmt_render2d = GF_V2_FMT_MASK;
    o->fmt_scanout = GF_V2_FMT_MASK & ~(1u << GFX2_FMT_ARGB8888);
    o->fmt_src_stretch = GF_V2_FMT_MASK;
    o->max_w = o->max_h = GF_V2_MAX_DIM; o->max_pitch = 0xffffu;
    o->pitch_align = GF_V2_PITCH_ALIGN; o->offset_align = 256u;
    o->scanout_align = MEM_PAGE_SIZE;
    o->stretch_min_q16 = 1; o->stretch_max_q16 = 0x1000000u;
    o->max_cmds = GFX2_MAX_CMDS;
    if (g->ch && g->ch->f) o->vram_bytes = g->ch->f->imem.vram_bytes;
    o->gart_bytes = GFX2_GART_KB * 1024u; o->gart_page = MEM_PAGE_SIZE;
    o->fence_hw = o->async = 1; o->heads = o->connectors = 2;
    o->palette_entries = 256; o->gamma_bits = 8;
    o->cursor_w = o->cursor_h = 64; o->cursor_fmt = 1u << GFX2_FMT_ARGB8888;
    o->flip_vblank = 1;
    return 0;
}

static int supported_surface(const struct gfx2_surf *s)
{
    return s && (s->pool == GFX2_POOL_VRAM || s->pool == GFX2_POOL_GART) &&
        s->fmt >= GFX2_FMT_INDEX8 && s->fmt <= GFX2_FMT_ARGB8888;
}

static int check(void *ctx, const struct gfx2_exec *ex)
{
    const struct gf_v2 *g = ctx;
    if (g->lost || !g->g3) return OS32_ERR_NOSYS;
    const GFX2_Op *op = ex->op;
    const struct gfx2_surf *d = ex->dst, *s = ex->src;
    if (op->flags & (GFX2_F_SRCKEY | GFX2_F_DSTKEY | GFX2_F_ALPHA_PIXEL)) return OS32_ERR_NOSYS;
    if (op->op == GFX2_OP_READBACK)
        return supported_surface(s) && s->pool == GFX2_POOL_VRAM ? 0 : OS32_ERR_NOSYS;
    if (!supported_surface(d)) return OS32_ERR_NOSYS;
    if (d->pool == GFX2_POOL_GART)
        return op->op == GFX2_OP_COPY && op->rop == 0xcc &&
            !(op->flags & (GFX2_F_PAT | GFX2_F_FILTER)) &&
            supported_surface(s) && s->pool == GFX2_POOL_VRAM && s->fmt == d->fmt
            ? 0 : OS32_ERR_NOSYS;
    switch (op->op) {
    case GFX2_OP_COPY: case GFX2_OP_STRETCH:
        if (!supported_surface(s) || s->fmt != d->fmt) return OS32_ERR_NOSYS;
        if (op->op == GFX2_OP_STRETCH)
            return d->fmt != GFX2_FMT_INDEX8 && op->rop == 0xcc ? 0 : OS32_ERR_NOSYS;
        /* fall through */
    case GFX2_OP_FILL:
        if (op->flags & GFX2_F_FILTER) return OS32_ERR_NOSYS;
        if (ex->pat && (op->flags & GFX2_F_PAT) &&
            (ex->pat->kind > GF3_PAT_COLOR || (ex->pat->bg.x & GFX2_CX_TRANSPARENT)))
            return OS32_ERR_NOSYS;
        if (!(op->flags & GFX2_F_PAT) && op->rop == (op->op == GFX2_OP_FILL ? 0xf0 : 0xcc)) return 0;
        return d->fmt <= GFX2_FMT_RGB565 ? 0 : OS32_ERR_NOSYS;
    case GFX2_OP_MONO:
        return d->fmt <= GFX2_FMT_RGB565 && !(op->bg.x & GFX2_CX_TRANSPARENT) ? 0 : OS32_ERR_NOSYS;
    case GFX2_OP_HOSTDATA:
        return d->pool == GFX2_POOL_VRAM ? 0 : OS32_ERR_NOSYS;
    default: return OS32_ERR_NOSYS;
    }
}

/* The core heap is one interval: take the largest unreserved hole below
 * the push buffer. Existing display/test reservations remain with v1. */
static int heap_range(void *ctx, u32 *base, u32 *size, u32 *phys)
{
    struct gf_v2 *g = ctx;
    *base = *size = *phys = 0;
    if (!g->ch) return OS32_ERR_IO;
    *phys = g->ch->bar1.base;
    u32 at = 0, end = g->ch->B;
    while (at < end) {
        u32 cursor = 0, next = end, occupied = at;
        struct gf_vram_reservation r;
        int rc;
        while ((rc = gf_vram_next(g->ch, &cursor, &r)) > 0) {
            if (r.off <= at && r.bytes > at - r.off) {
                u32 last = r.off + r.bytes;
                if (last > occupied) occupied = last;
            } else if (r.off > at && r.off < next) next = r.off;
        }
        if (rc < 0) return gf_v2_rc(rc);
        if (occupied > at) { at = occupied; continue; }
        if (next - at > *size) { *base = at; *size = next - at; }
        at = next;
    }
    return 0;
}

/* No persistent map exists for the free VRAM heap. User mappings use phys
 * from heap_range; kernel SW must use staging/readback instead of a cast. */
static int kva(void *ctx, const struct gfx2_surf *s, u8 **va)
{ (void)ctx; (void)s; *va = 0; return OS32_ERR_NOSYS; }
static int reservation_rc(int rc)
{
    if (rc == OS32_ERR_INVAL || rc == OS32_ERR_NOSPC || rc == OS32_ERR_NOSYS) return rc;
    return gf_v2_rc(rc);
}
static int alloc(void *c, struct gfx2_surf *s) { return reservation_rc(gf_v2_alloc(c, s)); }
static int release(void *c, struct gfx2_surf *s) { return reservation_rc(gf_v2_release(c, s)); }
static int gart_bind(void *c, const u32 *p, u32 n, u32 *off) { return gf_v2_gart_bind(c, p, n, off); }
static int gart_unbind(void *c, u32 off, u32 n) { return gf_v2_gart_unbind(c, off, n); }

/* C14 aliases borrow v1 storage, bypassing G9. Resolve the current reservation
 * for every HW operand; never cache an identity across a v1 rebind. Ordinary
 * allocations retain their own identity, and layer 3 still validates both. */
static int surface(struct gf_v2 *g, const struct gfx2_surf *in, struct gf3_surface *out)
{
    struct gf3_surface s = {in->gpu_off, in->bytes, in->pitch, in->w, in->h,
        (enum gf3_fmt)(in->fmt - GFX2_FMT_INDEX8), in->priv[0], in->priv[1],
        in->pool == GFX2_POOL_GART ? GF3_HANDLE_GART_DMA : 0};
    if ((in->flags & GFX2_SF_ALIAS) && in->pool == GFX2_POOL_VRAM) {
        struct gf_vram_reservation v;
        u32 cursor = 0;
        int rc;
        while ((rc = gf_vram_next(g->ch, &cursor, &v)) > 0) {
            if (s.off >= v.off && s.off - v.off <= v.bytes &&
                s.bytes && s.bytes <= v.bytes - (s.off - v.off)) {
                s.reservation_id = v.reservation_id;
                s.generation = v.generation;
                break;
            }
        }
        if (rc <= 0) return rc ? rc : GF3_ESTATE;
    }
    *out = s;
    return 0;
}
static struct gf3_rect rect(struct gfx2_rect r)
{ return (struct gf3_rect){r.x, r.y, r.w, r.h}; }

static struct gf3_pattern pattern(const struct gfx2_exec *ex)
{
    struct gf3_pattern p = {.kind = GF3_PAT_MONO, .fg = ex->op->fg.v, .bg = ex->op->fg.v};
    if (!(ex->op->flags & GFX2_F_PAT)) return p;
    const struct gfx2_pattern *in = ex->pat;
    p.kind = in->kind; p.fg = in->fg.v; p.bg = in->bg.v;
    for (u32 i = 0; i < 8; i++)
        p.mono[i / 4] |= (u32)in->mono[i] << ((i % 4) * 8);
    for (u32 i = 0; i < 64; i++) p.color[i] = in->color[i];
    return p;
}

static int emit(void *ctx, const struct gfx2_exec *ex, u32 fence)
{
    struct gf_v2 *g = ctx;
    const GFX2_Op *op = ex->op;
    struct gf3_surface dst, src;
    u32 dl;
    (void)fence;
    if (check(ctx, ex)) return OS32_ERR_NOSYS;
    if (ex->clip.w <= 0 || ex->clip.h <= 0) return 0;
    int rc = gf_v2_deadline(g->ch, GF3_TIMEOUT_US, &dl);
    if (rc) return gf_v2_rc(rc);
    rc = surface(g, ex->dst, &dst);
    if (rc) return gf_v2_rc(rc);
    struct gf3_rect r = rect(ex->clip);
    struct gf3_rect sr = {op->sx + ex->clip.x - op->dx, op->sy + ex->clip.y - op->dy, r.w, r.h};
    if (ex->src) {
        rc = surface(g, ex->src, &src);
        if (rc) return gf_v2_rc(rc);
    }
    if (op->op == GFX2_OP_COPY && ex->dst->pool == GFX2_POOL_GART) {
        u32 cpp = dst.fmt == GF3_INDEX8 ? 1 : dst.fmt == GF3_RGB565 ? 2 : 4;
        return gf_v2_rc(gf3_m2mf(g->g3, GF3_HANDLE_VRAM_DMA, GF3_HANDLE_GART_DMA,
            src.off + sr.y * src.pitch + sr.x * cpp,
            dst.off + r.y * dst.pitch + r.x * cpp, src.pitch, dst.pitch,
            r.w * cpp, r.h, dl));
    }
    if (op->op == GFX2_OP_FILL || op->op == GFX2_OP_COPY) {
        u8 alu = 3, subc = op->op == GFX2_OP_FILL ? GF3_SUBC_GDI : GF3_SUBC_BLIT;
        bool custom = (op->flags & GFX2_F_PAT) || op->rop != (op->op == GFX2_OP_FILL ? 0xf0 : 0xcc);
        if (custom) {
            struct gf3_pattern p = pattern(ex);
            rc = gf3_rop3_set(g->g3, subc, op->rop, &p, dst.fmt, dl);
            if (rc) return OS32_ERR_IO; /* partial state must take LOST */
            alu = GF3_ALU_KEEP;
        }
        rc = op->op == GFX2_OP_FILL
            ? gf3_fill_emit(g->g3, &dst, r, op->fg.v, alu, ~0u, dl)
            : gf3_copy_emit(g->g3, &src, &dst, sr, r.x, r.y, alu, ~0u, dl);
        if (custom) {
            int restore = gf3_rop3_set(g->g3, subc, 0xcc, 0, dst.fmt, dl);
            if (restore) return OS32_ERR_IO;
        }
        return gf_v2_rc(rc);
    }
    if (op->op == GFX2_OP_STRETCH)
        return gf_v2_rc(gf3_scale_emit(g->g3, &src, &dst,
            (struct gf3_rect){op->sx, op->sy, op->sw, op->sh},
            (struct gf3_rect){op->dx, op->dy, op->dw, op->dh}, r, !!(op->flags & GFX2_F_FILTER), dl));
    struct gf3_ram ram = {(void *)ex->cpu, ex->cpu_pitch * op->dh, ex->cpu_pitch};
    if (op->op == GFX2_OP_MONO) {
        /* Layer 3 clips mono while preserving its original bit origin. */
        if (op->dx < 0 || op->dy < 0 || (u32)op->dx + op->dw > dst.width || (u32)op->dy + op->dh > dst.height)
            return OS32_ERR_NOSYS;
        return gf_v2_rc(gf3_mono(g->g3, &dst, (struct gf3_rect){op->dx, op->dy, op->dw, op->dh},
            &ram, op->fg.v, op->bg.v, r, dl));
    }
    u32 cpp = dst.fmt == GF3_INDEX8 ? 1 : dst.fmt == GF3_RGB565 ? 2 : 4;
    ram.ptr = (void *)(ex->cpu + (r.y - op->dy) * ex->cpu_pitch + (r.x - op->dx) * cpp);
    ram.bytes -= (const u8 *)ram.ptr - ex->cpu;
    return gf_v2_rc(gf3_upload(g->g3, &dst, r, &ram, false, dl));
}

static int readback(void *ctx, const struct gfx2_exec *ex, u8 *buf)
{
    struct gf_v2 *g = ctx;
    u32 dl;
    int rc = gf_v2_deadline(g->ch, GF3_TIMEOUT_US, &dl);
    if (rc) return gf_v2_rc(rc);
    struct gf3_surface s;
    rc = surface(g, ex->src, &s);
    if (rc) return gf_v2_rc(rc);
    /* VRAM -> PCI staging M2MF, as in MIT
     * xf86-video-nouveau/src/nouveau_exa.c:256-280. */
    const GFX2_Op *op = ex->op;
    struct gf3_ram ram = {buf, op->pitch * op->sh, op->pitch};
    return gf_v2_rc(gf3_download(g->g3, &s, (struct gf3_rect){op->sx, op->sy, op->sw, op->sh}, &ram, dl));
}
static int kick(void *c) { return gf_v2_rc(gf2_kick(((struct gf_v2 *)c)->ch)); }
static int fence_emit(void *c, u32 f, u32 us)
{
    u32 dl; int rc = gf_v2_deadline(((struct gf_v2 *)c)->ch, us, &dl);
    return gf_v2_rc(rc ? rc : gf_v2_fence_emit(c, f, dl));
}
static int fence_wait(void *c, u32 f, u32 us)
{
    u32 dl; int rc = gf_v2_deadline(((struct gf_v2 *)c)->ch, us, &dl);
    return gf_v2_rc(rc ? rc : gf_v2_fence_wait(c, f, dl));
}
static int fence_state(void *c, u32 *done, u32 *failed)
{ return gf_v2_rc(gf_v2_fence_state(c, done, failed)); }
const GfxBackendV2 gf_backend_v2 = {
    .abi = GFX_HAL2_ABI, .ctx = &gf_v2_inst,
    .caps = caps, .check = check, .heap_range = heap_range,
    .alloc = alloc, .release = release, .kva = kva, .gart_bind = gart_bind, .gart_unbind = gart_unbind,
    .emit = emit, .readback = readback, .kick = kick,
    .fence_emit = fence_emit, .fence_state = fence_state, .fence_wait = fence_wait,
    .enum_head = gf_v2_enum_head, .enum_conn = gf_v2_enum_conn, .enum_mode = gf_v2_enum_mode, .edid = gf_v2_edid,
    .mode_set = gf_v2_mode_set, .flip = gf_v2_flip, .flip_done = gf_v2_flip_done, .vblank_wait = gf_v2_vblank_wait,
    .palette = gf_v2_palette, .gamma = gf_v2_gamma, .cursor_image = gf_v2_cursor_image,
    .cursor_move = gf_v2_cursor_move, .cursor_show = gf_v2_cursor_show, .dpms = gf_v2_dpms, .stop = gf_v2_stop
};
