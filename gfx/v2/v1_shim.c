/* HAL_V2_DESIGN §9-2, §11-7: v1 adapters and detached adapters. */
#include "gfx2_int.h"

static int shim_caps(void *c, GFX2_Caps *out)
{
    return gfx2_caps_from_v1(c, out);
}

static int shim_heap_range(void *c, u32 *base, u32 *size, u32 *phys)
{
    (void)c;
    *base = *size = *phys = 0;
    return 0;
}

static int shim_fence_state(void *c, u32 *done, u32 *fail)
{
    const struct gfx2_adapter *a = c;
    *done = a->fence.issued;
    *fail = 0;
    return 0;
}

static int lost_fence_state(void *c, u32 *done, u32 *fail)
{
    const struct gfx2_adapter *a = c;
    *done = *fail = a->fence.issued;
    return 0;
}

static int shim_enum_head(void *c, u32 head, GFX2_HeadInfo *out)
{
    const struct gfx2_adapter *a = c;
    return gfx2_v1_enum_head(a->v1, head, out);
}

static int shim_enum_conn(void *c, u32 conn, GFX2_ConnectorInfo *out)
{
    const struct gfx2_adapter *a = c;
    return gfx2_v1_enum_conn(a->v1, conn, out);
}

static int shim_enum_mode(void *c, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *out)
{
    const struct gfx2_adapter *a = c;
    return gfx2_v1_enum_mode(a->v1, head, fmt, i, out);
}

static int shim_mode_set(void *c, u32 head, u32 cm,
                         const GFX2_ModeInfo *m, const struct gfx2_surf *s)
{
    struct gfx2_adapter *a = c;
    (void)cm;
    (void)s;
    return head || !a->v1->set_mode ? OS32_ERR_NOSYS : a->v1->set_mode(m->id);
}

static int shim_flip(void *c, u32 head, const struct gfx2_surf *s, u32 flags, u32 dl)
{
    struct gfx2_adapter *a = c;
    if (!a->v1->page_flip) return OS32_ERR_NOSYS;
    const struct GfxScanout scan = {
        .reservation_id = s->priv[0], .offset = s->gpu_off, .bytes = s->bytes,
        .pitch = s->pitch, .width = s->w, .height = s->h, .format = s->fmt,
    };
    return a->v1->page_flip(head, &scan, !!(flags & GFX2_FLIP_VBLANK), dl);
}

static int shim_vblank_wait(void *c, u32 head, u32 after, u32 dl, u32 *seen)
{
    struct gfx2_adapter *a = c;
    return !a->v1->vsync ? OS32_ERR_NOSYS : a->v1->vsync(head, after, dl, seen);
}

static int shim_palette(void *c, u32 first, u32 count, const u32 *xrgb)
{
    struct gfx2_adapter *a = c;
    enum { PALETTE_ENTRIES = 256, RGB_COMPONENTS = 3 };
    u8 rgb[PALETTE_ENTRIES * RGB_COMPONENTS];
    if (!a->v1->set_palette) return OS32_ERR_NOSYS;
    /* v1 components are 0..15; its backend expands them by 17. */
    for (u32 i = 0; i < count; ++i) {
        rgb[3 * i] = (xrgb[i] >> 20) & 15;
        rgb[3 * i + 1] = (xrgb[i] >> 12) & 15;
        rgb[3 * i + 2] = (xrgb[i] >> 4) & 15;
    }
    a->v1->set_palette(first, count, rgb);
    return 0;
}

static int shim_gamma(void *c, u32 head, const u16 *rgb)
{
    struct gfx2_adapter *a = c;
    return !a->v1->gamma ? OS32_ERR_NOSYS :
        a->v1->gamma(head, (const u16 (*)[256])rgb);
}

static int shim_cursor_image(void *c, u32 head, const struct gfx2_surf *s)
{
    struct gfx2_adapter *a = c;
    if (!a->v1->cursor_image || !s->kva) return OS32_ERR_NOSYS;
    const struct GfxCursorImage image = {
        .argb_pm = (const u32 *)s->kva, .bytes = s->bytes, .pitch = s->pitch,
        .width = s->w, .height = s->h, .hot_x = 0, .hot_y = 0,
    };
    return a->v1->cursor_image(head, &image);
}

static int shim_cursor_move(void *c, u32 head, i32 x, i32 y)
{
    struct gfx2_adapter *a = c;
    return !a->v1->cursor_move ? OS32_ERR_NOSYS : a->v1->cursor_move(head, x, y);
}

static int shim_cursor_show(void *c, u32 head, u32 show)
{
    struct gfx2_adapter *a = c;
    return !a->v1->cursor_show ? OS32_ERR_NOSYS : a->v1->cursor_show(head, show);
}

static int shim_dpms(void *c, u32 head, u32 state)
{
    struct gfx2_adapter *a = c;
    return !a->v1->dpms ? OS32_ERR_NOSYS : a->v1->dpms(head, state);
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
    return OS32_ERR_NOSPC;
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
    return 0;
}

static int shim_fence_wait(void *c, u32 fence, u32 dl)
{
    (void)c;
    (void)fence;
    (void)dl;
    return 0;
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
    (void)head;
    (void)retired;
    return OS32_ERR_NOSYS;
}

static int shim_stop(void *c)
{
    (void)c;
    return 0;
}

const GfxBackendV2 gfx2_v1_shim = {
    .abi = GFX_HAL2_ABI,
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

static int lost_caps(void *c, GFX2_Caps *out)
{
    (void)c;
    (void)out;
    return OS32_ERR_STALE;
}

static int lost_check(void *c, const struct gfx2_exec *e)
{
    (void)c;
    (void)e;
    return OS32_ERR_STALE;
}

static int lost_heap_range(void *c, u32 *base, u32 *size, u32 *phys)
{
    (void)c;
    (void)base;
    (void)size;
    (void)phys;
    return OS32_ERR_STALE;
}

static int lost_alloc(void *c, struct gfx2_surf *s)
{
    (void)c;
    (void)s;
    return OS32_ERR_STALE;
}

static int lost_release(void *c, struct gfx2_surf *s)
{
    (void)c;
    (void)s;
    return 0;
}

static int lost_kva(void *c, const struct gfx2_surf *s, u8 **va)
{
    (void)c;
    (void)s;
    (void)va;
    return OS32_ERR_STALE;
}

static int lost_gart_bind(void *c, const u32 *pfn, u32 n, u32 *off)
{
    (void)c;
    (void)pfn;
    (void)n;
    (void)off;
    return OS32_ERR_STALE;
}

static int lost_gart_unbind(void *c, u32 off, u32 n)
{
    (void)c;
    (void)off;
    (void)n;
    return 0;
}

static int lost_emit(void *c, const struct gfx2_exec *e, u32 fence)
{
    (void)c;
    (void)e;
    (void)fence;
    return OS32_ERR_STALE;
}

static int lost_readback(void *c, const struct gfx2_exec *e, u8 *buf)
{
    (void)c;
    (void)e;
    (void)buf;
    return OS32_ERR_STALE;
}

static int lost_kick(void *c)
{
    (void)c;
    return 0;
}

static int lost_fence_emit(void *c, u32 fence, u32 dl)
{
    (void)c;
    (void)fence;
    (void)dl;
    return OS32_ERR_STALE;
}

static int lost_fence_wait(void *c, u32 fence, u32 dl)
{
    (void)c;
    (void)fence;
    (void)dl;
    return OS32_ERR_STALE;
}

static int lost_enum_head(void *c, u32 head, GFX2_HeadInfo *out)
{
    (void)c;
    (void)head;
    (void)out;
    return OS32_ERR_STALE;
}

static int lost_enum_conn(void *c, u32 conn, GFX2_ConnectorInfo *out)
{
    (void)c;
    (void)conn;
    (void)out;
    return OS32_ERR_STALE;
}

static int lost_enum_mode(void *c, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *out)
{
    (void)c;
    (void)head;
    (void)fmt;
    (void)i;
    (void)out;
    return OS32_ERR_STALE;
}

static int lost_edid(void *c, u32 conn, u32 block, u8 *out)
{
    (void)c;
    (void)conn;
    (void)block;
    (void)out;
    return OS32_ERR_STALE;
}

static int lost_mode_set(void *c, u32 head, u32 cm, const GFX2_ModeInfo *m, const struct gfx2_surf *s)
{
    (void)c;
    (void)head;
    (void)cm;
    (void)m;
    (void)s;
    return OS32_ERR_STALE;
}

static int lost_flip(void *c, u32 head, const struct gfx2_surf *s, u32 flags, u32 dl)
{
    (void)c;
    (void)head;
    (void)s;
    (void)flags;
    (void)dl;
    return OS32_ERR_STALE;
}

static int lost_flip_done(void *c, u32 head, u32 *retired)
{
    (void)c;
    (void)head;
    (void)retired;
    return OS32_ERR_STALE;
}

static int lost_vblank_wait(void *c, u32 head, u32 after, u32 dl, u32 *seen)
{
    (void)c;
    (void)head;
    (void)after;
    (void)dl;
    (void)seen;
    return OS32_ERR_STALE;
}

static int lost_palette(void *c, u32 first, u32 count, const u32 *rgb)
{
    (void)c;
    (void)first;
    (void)count;
    (void)rgb;
    return OS32_ERR_STALE;
}

static int lost_gamma(void *c, u32 head, const u16 *rgb)
{
    (void)c;
    (void)head;
    (void)rgb;
    return OS32_ERR_STALE;
}

static int lost_cursor_image(void *c, u32 head, const struct gfx2_surf *s)
{
    (void)c;
    (void)head;
    (void)s;
    return OS32_ERR_STALE;
}

static int lost_cursor_move(void *c, u32 head, i32 x, i32 y)
{
    (void)c;
    (void)head;
    (void)x;
    (void)y;
    return OS32_ERR_STALE;
}

static int lost_cursor_show(void *c, u32 head, u32 show)
{
    (void)c;
    (void)head;
    (void)show;
    return OS32_ERR_STALE;
}

static int lost_dpms(void *c, u32 head, u32 state)
{
    (void)c;
    (void)head;
    (void)state;
    return OS32_ERR_STALE;
}

static int lost_stop(void *c)
{
    (void)c;
    return 0;
}

const GfxBackendV2 gfx2_lost_be = {
    .abi = GFX_HAL2_ABI,
    .caps = lost_caps,
    .check = lost_check,
    .heap_range = lost_heap_range,
    .alloc = lost_alloc,
    .release = lost_release,
    .kva = lost_kva,
    .gart_bind = lost_gart_bind,
    .gart_unbind = lost_gart_unbind,
    .emit = lost_emit,
    .readback = lost_readback,
    .kick = lost_kick,
    .fence_emit = lost_fence_emit,
    .fence_state = lost_fence_state,
    .fence_wait = lost_fence_wait,
    .enum_head = lost_enum_head,
    .enum_conn = lost_enum_conn,
    .enum_mode = lost_enum_mode,
    .edid = lost_edid,
    .mode_set = lost_mode_set,
    .flip = lost_flip,
    .flip_done = lost_flip_done,
    .vblank_wait = lost_vblank_wait,
    .palette = lost_palette,
    .gamma = lost_gamma,
    .cursor_image = lost_cursor_image,
    .cursor_move = lost_cursor_move,
    .cursor_show = lost_cursor_show,
    .dpms = lost_dpms,
    .stop = lost_stop,
};
