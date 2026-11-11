/* A0 placeholders; replaced by strong definitions in the owning stages. */
#include "gf_v2.h"
/* G1 replaces this placeholder with the adapter instance. */
__attribute__((weak)) struct gf_v2 gf_v2_inst;
__attribute__((weak)) int gf_v2_fence_emit(struct gf_v2 * a0, u32 v2, u32 dl)
{
    (void)a0;
    (void)v2;
    (void)dl;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_fence_state(struct gf_v2 * a0, u32 *completed, u32 *failed_upto)
{
    (void)a0;
    (void)completed;
    (void)failed_upto;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_fence_wait(struct gf_v2 * a0, u32 v2, u32 dl)
{
    (void)a0;
    (void)v2;
    (void)dl;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_alloc(struct gf_v2 * a0, struct gfx2_surf * a1)
{
    (void)a0;
    (void)a1;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_release(struct gf_v2 * a0, struct gfx2_surf * a1)
{
    (void)a0;
    (void)a1;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_gart_bind(struct gf_v2 * a0, const u32 *pfn, u32 n, u32 *gpu_off)
{
    (void)a0;
    (void)pfn;
    (void)n;
    (void)gpu_off;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_gart_unbind(struct gf_v2 * a0, u32 gpu_off, u32 n)
{
    (void)a0;
    (void)gpu_off;
    (void)n;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_gart_range(void *opaque, u32 handle, u32 off, u32 bytes)
{
    (void)opaque;
    (void)handle;
    (void)off;
    (void)bytes;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_gart_start(struct gf_v2 * a0, u32 dl)
{
    (void)a0;
    (void)dl;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) void gf_v2_gart_reset(struct gf_v2 * a0)
{
    (void)a0;
}
__attribute__((weak)) int gf_v2_enum_head(void * a0, u32 head, GFX2_HeadInfo * a2)
{
    (void)a0;
    (void)head;
    (void)a2;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_enum_conn(void * a0, u32 conn, GFX2_ConnectorInfo * a2)
{
    (void)a0;
    (void)conn;
    (void)a2;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_enum_mode(void * a0, u32 head, u32 fmt, u32 i, GFX2_ModeInfo * a4)
{
    (void)a0;
    (void)head;
    (void)fmt;
    (void)i;
    (void)a4;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_edid(void * a0, u32 conn, u32 block, u8 *out128)
{
    (void)a0;
    (void)conn;
    (void)block;
    (void)out128;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_mode_set(void * a0, u32 head, u32 conn_mask, const GFX2_ModeInfo * a3, const struct gfx2_surf * a4)
{
    (void)a0;
    (void)head;
    (void)conn_mask;
    (void)a3;
    (void)a4;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_flip(void * a0, u32 head, const struct gfx2_surf * a2, u32 flags, u32 dl_us)
{
    (void)a0;
    (void)head;
    (void)a2;
    (void)flags;
    (void)dl_us;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_flip_done(void * a0, u32 head, u32 *retired_sid)
{
    (void)a0;
    (void)head;
    (void)retired_sid;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_vblank_wait(void * a0, u32 head, u32 after, u32 dl_us, u32 *seen)
{
    (void)a0;
    (void)head;
    (void)after;
    (void)dl_us;
    (void)seen;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_palette(void * a0, u32 first, u32 count, const u32 *xrgb)
{
    (void)a0;
    (void)first;
    (void)count;
    (void)xrgb;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_gamma(void * a0, u32 head, const u16 *rgb768)
{
    (void)a0;
    (void)head;
    (void)rgb768;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_cursor_image(void * a0, u32 head, const struct gfx2_surf * a2)
{
    (void)a0;
    (void)head;
    (void)a2;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_cursor_move(void * a0, u32 head, i32 x, i32 y)
{
    (void)a0;
    (void)head;
    (void)x;
    (void)y;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_cursor_show(void * a0, u32 head, u32 show)
{
    (void)a0;
    (void)head;
    (void)show;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_dpms(void * a0, u32 head, u32 state)
{
    (void)a0;
    (void)head;
    (void)state;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf3_fill_emit(struct gf3_ctx * a0, const struct gf3_surface * a1, struct gf3_rect a2, u32 color, u8 alu, u32 mask, u32 dl)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)color;
    (void)alu;
    (void)mask;
    (void)dl;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf3_copy_emit(struct gf3_ctx * a0, const struct gf3_surface * a1, const struct gf3_surface * a2, struct gf3_rect a3, u32 dx, u32 dy, u8 alu, u32 mask, u32 dl)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    (void)dx;
    (void)dy;
    (void)alu;
    (void)mask;
    (void)dl;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf3_scale_emit(struct gf3_ctx * a0, const struct gf3_surface * a1, const struct gf3_surface * a2, struct gf3_rect a3, struct gf3_rect a4, struct gf3_rect a5, bool a6, u32 dl)
{
    (void)a0;
    (void)a1;
    (void)a2;
    (void)a3;
    (void)a4;
    (void)a5;
    (void)a6;
    (void)dl;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) void gf3_range_query_set(struct gf3_ctx * a0, gf3_range_fn a1, void *opaque)
{
    (void)a0;
    (void)a1;
    (void)opaque;
}
__attribute__((weak)) int gf3_ckey_set(struct gf3_ctx * a0, bool on, u32 key, enum gf3_fmt format, u32 dl)
{
    (void)a0;
    (void)on;
    (void)key;
    (void)format;
    (void)dl;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf3_alpha_set(struct gf3_ctx * a0, u32 const_a, bool per_pixel, u32 dl)
{
    (void)a0;
    (void)const_a;
    (void)per_pixel;
    (void)dl;
    return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf3_rop3_set(struct gf3_ctx * a0, u8 subc, u8 rop3, const struct gf3_pattern * a3, enum gf3_fmt format, u32 dl)
{
    (void)a0;
    (void)subc;
    (void)rop3;
    (void)a3;
    (void)format;
    (void)dl;
    return OS32_ERR_NOSYS;
}

__attribute__((weak)) int gf_v2_start(struct gf_v2 *a, u32 dl)
{
    (void)a; (void)dl; return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_stop(void *a)
{
    (void)a; return OS32_ERR_NOSYS;
}
__attribute__((weak)) const struct gf4_test_ops *gf_v2_disp(void)
{
    return NULL;
}
__attribute__((weak)) int gf_v2_deadline(struct gf2_chan *a, u32 us, u32 *dl)
{
    (void)a; (void)us; (void)dl; return OS32_ERR_NOSYS;
}
__attribute__((weak)) int gf_v2_rc(int rc)
{
    (void)rc; return OS32_ERR_IO;
}
__attribute__((weak)) void gf_v2_fence_reset(struct gf_v2 *a)
{
    (void)a;
}
__attribute__((weak)) u32 gf_v2_route_index(const struct gf4_ctx *a, const struct gf4_route *b)
{
    (void)a; (void)b; return 0;
}
__attribute__((weak)) int gf_v2_owns(const struct gf3_surface *a)
{
    (void)a; return 0;
}

