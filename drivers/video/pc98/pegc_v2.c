/* HAL_V2_DESIGN P1: PEGC has CPU rendering and a SHADOW scanout. */
#include "../../../gfx/v2/gfx2_int.h"
#include "pegc.h"

static int pegc2_caps(void *c, GFX2_Caps *out)
{
    (void)c;
    *out = (GFX2_Caps){0};
    const u32 formats = (1u << GFX2_FMT_INDEX8) | (1u << GFX2_FMT_RGB565) |
                        (1u << GFX2_FMT_XRGB8888) | (1u << GFX2_FMT_ARGB8888);
    const u32 ops = ((1u << (GFX2_OP_MAX + 1)) - 1u) & ~1u;
    out->size = sizeof(*out);
    out->backend_kind = GFX2_KIND_PEGC;
    out->caps2d_sw = ops;
    out->exact = ops & ~(GFX2_CAP2D(GFX2_OP_STRETCH) | GFX2_CAP2D(GFX2_OP_ALPHA));
    out->fmt_surface = out->fmt_render2d = out->fmt_src_stretch = formats;
    out->fmt_scanout = 1u << GFX2_FMT_INDEX8;
    out->max_w = PEGC_WIDTH;
    out->max_h = PEGC_HEIGHT_480;
    out->max_pitch = PEGC_WIDTH * sizeof(u32);
    out->pitch_align = out->offset_align = out->scanout_align = sizeof(u32);
    out->max_cmds = GFX2_MAX_CMDS;
    out->heads = out->connectors = 1;
    out->palette_entries = GFX_PAL_ENTRIES(GFX_FMT_PACKED8_BPP);
    return 0;
}

static int pegc2_heap_range(void *c, u32 *base, u32 *size, u32 *phys)
{
    (void)c;
    *base = *size = *phys = 0;
    return 0;
}

static int pegc2_fence_state(void *c, u32 *done, u32 *fail)
{
    (void)c;
    *done = 0;
    *fail = 0;
    return 0;
}

static int pegc2_enum_head(void *c, u32 head, GFX2_HeadInfo *out)
{
    const GfxBackend *v1 = c;
    return gfx2_v1_enum_head(v1, head, out);
}

static int pegc2_enum_conn(void *c, u32 conn, GFX2_ConnectorInfo *out)
{
    const GfxBackend *v1 = c;
    return gfx2_v1_enum_conn(v1, conn, out);
}

static int pegc2_enum_mode(void *c, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *out)
{
    const GfxBackend *v1 = c;
    return gfx2_v1_enum_mode(v1, head, fmt, i, out);
}

static int pegc2_mode_set(void *c, u32 head, u32 cm,
                         const GFX2_ModeInfo *m, const struct gfx2_surf *s)
{
    GfxBackend *v1 = c;
    (void)cm;
    (void)s;
    return head || !v1->set_mode ? OS32_ERR_NOSYS : v1->set_mode(m->id);
}

static int pegc2_flip(void *c, u32 head, const struct gfx2_surf *s, u32 flags, u32 dl)
{
    GfxBackend *v1 = c;
    if (!v1->page_flip) return OS32_ERR_NOSYS;
    const struct GfxScanout scan = {
        .reservation_id = s->priv[0], .offset = s->gpu_off, .bytes = s->bytes,
        .pitch = s->pitch, .width = s->w, .height = s->h, .format = s->fmt,
    };
    return v1->page_flip(head, &scan, !!(flags & GFX2_FLIP_VBLANK), dl);
}

static int pegc2_vblank_wait(void *c, u32 head, u32 after, u32 dl, u32 *seen)
{
    GfxBackend *v1 = c;
    return !v1->vsync ? OS32_ERR_NOSYS : v1->vsync(head, after, dl, seen);
}

static int pegc2_palette(void *c, u32 first, u32 count, const u32 *xrgb)
{
    GfxBackend *v1 = c;
    enum { PALETTE_ENTRIES = 256, RGB_COMPONENTS = 3 };
    u8 rgb[PALETTE_ENTRIES * RGB_COMPONENTS];
    if (!v1->set_palette) return OS32_ERR_NOSYS;
    /* v1 components are 0..15; its backend expands them by 17. */
    for (u32 i = 0; i < count; ++i) {
        rgb[3 * i] = (xrgb[i] >> 20) & 15;
        rgb[3 * i + 1] = (xrgb[i] >> 12) & 15;
        rgb[3 * i + 2] = (xrgb[i] >> 4) & 15;
    }
    v1->set_palette(first, count, rgb);
    return 0;
}

static int pegc2_gamma(void *c, u32 head, const u16 *rgb)
{
    GfxBackend *v1 = c;
    return !v1->gamma ? OS32_ERR_NOSYS :
        v1->gamma(head, (const u16 (*)[256])rgb);
}

static int pegc2_cursor_image(void *c, u32 head, const struct gfx2_surf *s)
{
    GfxBackend *v1 = c;
    if (!v1->cursor_image || !s->kva) return OS32_ERR_NOSYS;
    const struct GfxCursorImage image = {
        .argb_pm = (const u32 *)s->kva, .bytes = s->bytes, .pitch = s->pitch,
        .width = s->w, .height = s->h, .hot_x = 0, .hot_y = 0,
    };
    return v1->cursor_image(head, &image);
}

static int pegc2_cursor_move(void *c, u32 head, i32 x, i32 y)
{
    GfxBackend *v1 = c;
    return !v1->cursor_move ? OS32_ERR_NOSYS : v1->cursor_move(head, x, y);
}

static int pegc2_cursor_show(void *c, u32 head, u32 show)
{
    GfxBackend *v1 = c;
    return !v1->cursor_show ? OS32_ERR_NOSYS : v1->cursor_show(head, show);
}

static int pegc2_dpms(void *c, u32 head, u32 state)
{
    GfxBackend *v1 = c;
    return !v1->dpms ? OS32_ERR_NOSYS : v1->dpms(head, state);
}

static int pegc2_check(void *c, const struct gfx2_exec *e)
{
    (void)c;
    (void)e;
    return OS32_ERR_NOSYS;
}

static int pegc2_alloc(void *c, struct gfx2_surf *s)
{
    (void)c;
    (void)s;
    return OS32_ERR_NOSPC;
}

static int pegc2_release(void *c, struct gfx2_surf *s)
{
    (void)c;
    (void)s;
    return 0;
}

static int pegc2_kva(void *c, const struct gfx2_surf *s, u8 **va)
{
    (void)c;
    if (!s->kva) return OS32_ERR_NOSYS;
    *va = s->kva;
    return 0;
}

static int pegc2_gart_bind(void *c, const u32 *pfn, u32 n, u32 *off)
{
    (void)c;
    (void)pfn;
    (void)n;
    (void)off;
    return OS32_ERR_NOSYS;
}

static int pegc2_gart_unbind(void *c, u32 off, u32 n)
{
    (void)c;
    (void)off;
    (void)n;
    return 0;
}

static int pegc2_emit(void *c, const struct gfx2_exec *e, u32 fence)
{
    (void)c;
    (void)e;
    (void)fence;
    return OS32_ERR_NOSYS;
}

static int pegc2_readback(void *c, const struct gfx2_exec *e, u8 *buf)
{
    (void)c;
    (void)e;
    (void)buf;
    return OS32_ERR_NOSYS;
}

static int pegc2_kick(void *c)
{
    (void)c;
    return 0;
}

static int pegc2_fence_emit(void *c, u32 fence, u32 dl)
{
    (void)c;
    (void)fence;
    (void)dl;
    return 0;
}

static int pegc2_fence_wait(void *c, u32 fence, u32 dl)
{
    (void)c;
    (void)fence;
    (void)dl;
    return 0;
}

static int pegc2_edid(void *c, u32 conn, u32 block, u8 *out)
{
    (void)c;
    (void)conn;
    (void)block;
    (void)out;
    return OS32_ERR_NOSYS;
}

static int pegc2_flip_done(void *c, u32 head, u32 *retired)
{
    (void)c;
    (void)head;
    (void)retired;
    return OS32_ERR_NOSYS;
}

static int pegc2_stop(void *c)
{
    (void)c;
    return 0;
}

const GfxBackendV2 pegc_backend_v2 = {
    .abi = GFX_HAL2_ABI,
    .ctx = &gfx_backend_pegc,
    .caps = pegc2_caps,
    .check = pegc2_check,
    .heap_range = pegc2_heap_range,
    .alloc = pegc2_alloc,
    .release = pegc2_release,
    .kva = pegc2_kva,
    .gart_bind = pegc2_gart_bind,
    .gart_unbind = pegc2_gart_unbind,
    .emit = pegc2_emit,
    .readback = pegc2_readback,
    .kick = pegc2_kick,
    .fence_emit = pegc2_fence_emit,
    .fence_state = pegc2_fence_state,
    .fence_wait = pegc2_fence_wait,
    .enum_head = pegc2_enum_head,
    .enum_conn = pegc2_enum_conn,
    .enum_mode = pegc2_enum_mode,
    .edid = pegc2_edid,
    .mode_set = pegc2_mode_set,
    .flip = pegc2_flip,
    .flip_done = pegc2_flip_done,
    .vblank_wait = pegc2_vblank_wait,
    .palette = pegc2_palette,
    .gamma = pegc2_gamma,
    .cursor_image = pegc2_cursor_image,
    .cursor_move = pegc2_cursor_move,
    .cursor_show = pegc2_cursor_show,
    .dpms = pegc2_dpms,
    .stop = pegc2_stop,
};

