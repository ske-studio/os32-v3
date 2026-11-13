/* Fixed INDEX8 OF scanout. CPU rendering uses common sysmem surfaces. */
#include "gfx/v2/gfx2_int.h"
#include "backend_of_fb.h"

/* Native backends carry their v1 backend as context. */
static struct gfx2_adapter *adapter_for(void *c)
{
    for (u32 i = 0; i < GFX2_MAX_ADAPTERS; i++) {
        struct gfx2_adapter *a = gfx2_adapter_get(i);
        if (a && a->v1 == c) return a;
    }
    return 0;
}

static int shim_caps(void *c, GFX2_Caps *out)
{
    int rc = gfx2_caps_from_v1(adapter_for(c), out);
    out->fmt_surface = out->fmt_render2d = out->fmt_src_stretch =
        out->fmt_scanout = 1u << GFX2_FMT_INDEX8;
    out->palette_entries = 0;
    return rc;
}

static int shim_heap_range(void *c, u32 *base, u32 *size, u32 *phys)
{
    (void)c;
    *base = *size = *phys = 0;
    return 0;
}

static int shim_fence_state(void *c, u32 *done, u32 *fail)
{
    (void)c;
    *done = *fail = 0;
    return 0;
}

static int shim_enum_head(void *c, u32 head, GFX2_HeadInfo *out)
{
    const GfxBackend *b = c;
    return gfx2_v1_enum_head(b, head, out);
}

static int shim_enum_conn(void *c, u32 conn, GFX2_ConnectorInfo *out)
{
    const GfxBackend *b = c;
    return gfx2_v1_enum_conn(b, conn, out);
}

static int shim_enum_mode(void *c, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *out)
{
    const GfxBackend *b = c;
    return gfx2_v1_enum_mode(b, head, fmt, i, out);
}

/* No surface pointer survives a call: core owns scan references, detach and
 * LOST. v1_shim discards s at mode_set and needs a page_flip hook, so it
 * cannot connect an ordinary sysmem scanout (design N6). */
static int surface_copy(void *c, u32 head, const struct gfx2_surf *s)
{
    struct gfx2_adapter *a = adapter_for(c);
    const GfxMode *m = gfx_backend_mode(a->v1);
    if (!a || a->state != GFX2_AD_OK || !of_fb_ready()) return OS32_ERR_STALE;
    if (head || !s || !m || s->adapter != a->index ||
        s->state == GFX2_ST_LOST || s->state == GFX2_ST_FREE ||
        s->pool != GFX2_POOL_SYSMEM || !s->kva ||
        s->fmt != GFX2_FMT_INDEX8 || s->w != m->width || s->h != m->height)
        return OS32_ERR_INVAL;
    return of_fb_copy(s->kva, s->pitch, s->bytes, 0, 0, s->w, s->h);
}
static int shim_mode_set(void *c, u32 head, u32 cm,
                         const GFX2_ModeInfo *m, const struct gfx2_surf *s)
{
    const GfxMode *fixed = gfx_backend_mode(c);
    if (!m || !fixed || m->id != 1 || cm != 1 || m->w != fixed->width ||
        m->h != fixed->height || m->fmt != GFX2_FMT_INDEX8) return OS32_ERR_NOSYS;
    return surface_copy(c, head, s);
}
static int shim_flip(void *c, u32 head, const struct gfx2_surf *s, u32 flags, u32 dl)
{
    (void)dl;
    if (flags) return OS32_ERR_NOSYS;
    return surface_copy(c, head, s);
}

static int shim_vblank_wait(void *c, u32 head, u32 after, u32 dl, u32 *seen)
{
    GfxBackend *b = c;
    return !b->vsync ? OS32_ERR_NOSYS : b->vsync(head, after, dl, seen);
}

static int shim_palette(void *c, u32 first, u32 count, const u32 *xrgb)
{ (void)c; (void)first; (void)count; (void)xrgb; return OS32_ERR_NOSYS; }

static int shim_gamma(void *c, u32 head, const u16 *rgb)
{
    GfxBackend *b = c;
    return !b->gamma ? OS32_ERR_NOSYS :
        b->gamma(head, (const u16 (*)[256])rgb);
}

static int shim_cursor_image(void *c, u32 head, const struct gfx2_surf *s)
{
    GfxBackend *b = c;
    if (!b->cursor_image || !s->kva) return OS32_ERR_NOSYS;
    const struct GfxCursorImage image = {
        .argb_pm = (const u32 *)s->kva, .bytes = s->bytes, .pitch = s->pitch,
        .width = s->w, .height = s->h, .hot_x = 0, .hot_y = 0,
    };
    return b->cursor_image(head, &image);
}

static int shim_cursor_move(void *c, u32 head, i32 x, i32 y)
{
    GfxBackend *b = c;
    return !b->cursor_move ? OS32_ERR_NOSYS : b->cursor_move(head, x, y);
}

static int shim_cursor_show(void *c, u32 head, u32 show)
{
    GfxBackend *b = c;
    return !b->cursor_show ? OS32_ERR_NOSYS : b->cursor_show(head, show);
}

static int shim_dpms(void *c, u32 head, u32 state)
{
    GfxBackend *b = c;
    return !b->dpms ? OS32_ERR_NOSYS : b->dpms(head, state);
}

static int shim_check(void *c, const struct gfx2_exec *e)
{
    (void)c;
    (void)e;
    return OS32_ERR_NOSYS;
}

static int shim_alloc(void *c, struct gfx2_surf *s)
{
    (void)c;
    (void)s;
    return OS32_ERR_NOSYS;
}

static int shim_release(void *c, struct gfx2_surf *s)
{
    (void)c;
    (void)s;
    return 0;
}

static int shim_kva(void *c, const struct gfx2_surf *s, u8 **va)
{
    (void)c;
    (void)s;
    (void)va;
    return OS32_ERR_NOSYS;
}

static int shim_gart_bind(void *c, const u32 *pfn, u32 n, u32 *off)
{
    (void)c;
    (void)pfn;
    (void)n;
    (void)off;
    return OS32_ERR_NOSYS;
}

static int shim_gart_unbind(void *c, u32 off, u32 n)
{
    (void)c;
    (void)off;
    (void)n;
    return 0;
}

static int shim_emit(void *c, const struct gfx2_exec *e, u32 fence)
{
    (void)c;
    (void)e;
    (void)fence;
    return OS32_ERR_NOSYS;
}

static int shim_readback(void *c, const struct gfx2_exec *e, u8 *buf)
{
    (void)c;
    (void)e;
    (void)buf;
    return OS32_ERR_NOSYS;
}

static int shim_kick(void *c)
{
    (void)c;
    return 0;
}

static int shim_fence_emit(void *c, u32 fence, u32 dl)
{
    (void)c;
    (void)fence;
    (void)dl;
    return OS32_ERR_NOSYS;
}

static int shim_fence_wait(void *c, u32 fence, u32 dl)
{
    (void)c;
    (void)fence;
    (void)dl;
    return OS32_ERR_NOSYS;
}

static int shim_edid(void *c, u32 conn, u32 block, u8 *out)
{
    (void)c;
    (void)conn;
    (void)block;
    (void)out;
    return OS32_ERR_NOSYS;
}

static int shim_flip_done(void *c, u32 head, u32 *retired)
{
    (void)c;
    if (head) return OS32_ERR_INVAL;
    *retired = 1;
    return 0;
}

static int shim_stop(void *c)
{
    (void)c;
    of_fb_unbind();
    return 0;
}

const GfxBackendV2 of_fb_v2 = {
    .abi = GFX_HAL2_ABI,
    .ctx = &gfx_backend_of_fb,
    .caps = shim_caps,
    .check = shim_check,
    .heap_range = shim_heap_range,
    .alloc = shim_alloc,
    .release = shim_release,
    .kva = shim_kva,
    .gart_bind = shim_gart_bind,
    .gart_unbind = shim_gart_unbind,
    .emit = shim_emit,
    .readback = shim_readback,
    .kick = shim_kick,
    .fence_emit = shim_fence_emit,
    .fence_state = shim_fence_state,
    .fence_wait = shim_fence_wait,
    .enum_head = shim_enum_head,
    .enum_conn = shim_enum_conn,
    .enum_mode = shim_enum_mode,
    .edid = shim_edid,
    .mode_set = shim_mode_set,
    .flip = shim_flip,
    .flip_done = shim_flip_done,
    .vblank_wait = shim_vblank_wait,
    .palette = shim_palette,
    .gamma = shim_gamma,
    .cursor_image = shim_cursor_image,
    .cursor_move = shim_cursor_move,
    .cursor_show = shim_cursor_show,
    .dpms = shim_dpms,
    .stop = shim_stop,
};

