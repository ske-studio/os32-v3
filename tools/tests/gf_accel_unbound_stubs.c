/* Dependencies for display/HAL fixtures with no bound layer-3 service.
 * Any layer-3 invocation is a test failure (illegal instruction), never fake
 * success. The real service lookup runs; binding/lifecycle uses the separate
 * gf_accel_c1/c2 hosts with real F1/F2/F3 products. Weak definitions allow a
 * fixture's explicit lower-layer model or real backend to take precedence. */
#include "hwdev/gf/accel.h"
#include "hwdev/gf/feature5.h"
/* F3-only fixtures never bind the foreground F5 context. */
__attribute__((weak)) i32 gf5_service_poll(struct gf5 *g)
{ (void)g; __builtin_trap(); }
__attribute__((weak)) int gf3_init(struct gf3_ctx * a0, struct gf2_chan * a1, u32 dl)
{ (void)a0;(void)a1;(void)dl; __builtin_trap(); }
__attribute__((weak)) int gf3_fini(struct gf3_ctx * a0, u32 dl)
{ (void)a0;(void)dl; __builtin_trap(); }
__attribute__((weak)) int gf3_fill(struct gf3_ctx * a0, const struct gf3_surface * a1, struct gf3_rect a2, u32 color, u8 alu, u32 mask, u32 dl)
{ (void)a0;(void)a1;(void)a2;(void)color;(void)alu;(void)mask;(void)dl; __builtin_trap(); }
__attribute__((weak)) int gf3_copy(struct gf3_ctx * a0, const struct gf3_surface *src, const struct gf3_surface *dst, struct gf3_rect sr, u32 dx, u32 dy, u8 alu, u32 mask, u32 dl)
{ (void)a0;(void)src;(void)dst;(void)sr;(void)dx;(void)dy;(void)alu;(void)mask;(void)dl; __builtin_trap(); }
__attribute__((weak)) int gf3_mono(struct gf3_ctx * a0, const struct gf3_surface * a1, struct gf3_rect a2, const struct gf3_ram *bits, u32 fg, u32 bg, struct gf3_rect clip, u32 dl)
{ (void)a0;(void)a1;(void)a2;(void)bits;(void)fg;(void)bg;(void)clip;(void)dl; __builtin_trap(); }
__attribute__((weak)) int gf3_ifc(struct gf3_ctx * a0, const struct gf3_surface * a1, struct gf3_rect a2, const struct gf3_ram * a3, u32 dl)
{ (void)a0;(void)a1;(void)a2;(void)a3;(void)dl; __builtin_trap(); }
__attribute__((weak)) int gf3_upload(struct gf3_ctx * a0, const struct gf3_surface * a1, struct gf3_rect a2, const struct gf3_ram * a3, bool force_m2mf, u32 dl)
{ (void)a0;(void)a1;(void)a2;(void)a3;(void)force_m2mf;(void)dl; __builtin_trap(); }
__attribute__((weak)) int gf3_download(struct gf3_ctx * a0, const struct gf3_surface * a1, struct gf3_rect a2, struct gf3_ram * a3, u32 dl)
{ (void)a0;(void)a1;(void)a2;(void)a3;(void)dl; __builtin_trap(); }
__attribute__((weak)) int gf3_scale(struct gf3_ctx * a0, const struct gf3_surface *src, const struct gf3_surface *dst, struct gf3_rect sr, struct gf3_rect dr, struct gf3_rect clip, bool bilinear, u32 dl)
{ (void)a0;(void)src;(void)dst;(void)sr;(void)dr;(void)clip;(void)bilinear;(void)dl; __builtin_trap(); }
__attribute__((weak)) int gf3_present_m2mf(struct gf3_ctx * a0, const struct gf3_surface *scan, struct gf3_rect a2, const struct gf3_ram * a3, u32 dl)
{ (void)a0;(void)scan;(void)a2;(void)a3;(void)dl; __builtin_trap(); }
__attribute__((weak)) void gf_hook_ctx_bind(struct gf3_ctx * a0)
{ (void)a0; __builtin_trap(); }
__attribute__((weak)) void gf_hook_scan_bind(bool active, const struct gf3_surface * a1)
{ if (active || a1) __builtin_trap(); }
__attribute__((weak)) void geforce_backend_framebuffer_info(struct video_fb *out, u32 *width, u32 *height)
{ (void)out;(void)width;(void)height; __builtin_trap(); }
__attribute__((weak)) int gf_vram_view_map(struct gf_vram_view *view, struct gf2_chan *ch, u32 off, u32 len, volatile u8 **out)
{ (void)view;(void)ch;(void)off;(void)len;(void)out; __builtin_trap(); }
__attribute__((weak)) int gf_vram_view_unmap(struct gf_vram_view *view)
{ (void)view; __builtin_trap(); }
__attribute__((weak)) int gf_vram_registry_init(struct gf2_chan *ch)
{ (void)ch; __builtin_trap(); }
__attribute__((weak)) int gf_vram_reserve(struct gf2_chan *ch, const void *owner, u32 off, u32 bytes, struct gf_vram_reservation_node *node)
{ (void)ch;(void)owner;(void)off;(void)bytes;(void)node; __builtin_trap(); }
__attribute__((weak)) int gf_vram_release(struct gf2_chan *ch, const struct gf_vram_reservation *key)
{ (void)ch;(void)key; __builtin_trap(); }
__attribute__((weak)) i32 gf_vram_views_close(struct gf2_chan *ch)
{ (void)ch; __builtin_trap(); }
__attribute__((weak)) int gf2_stop(struct gf2_chan * a0)
{ (void)a0; __builtin_trap(); }
__attribute__((weak)) int gf2_recover(struct gf2_chan * a0)
{ (void)a0; __builtin_trap(); }
__attribute__((weak)) int geforce_fill_rect(int x, int y, int w, int h, u8 color)
{ (void)x;(void)y;(void)w;(void)h;(void)color; __builtin_trap(); }
__attribute__((weak)) int geforce_blit(int dx, int dy, int sx, int sy, int w, int h)
{ (void)dx;(void)dy;(void)sx;(void)sy;(void)w;(void)h; __builtin_trap(); }

/* Legacy F3 fixtures never bind display4; a routed F4 start is unexpected. */
struct gf4_ctx;
__attribute__((weak)) i32 gf4_service_start(struct gf4_ctx *ctx, u32 dl)
{ (void)ctx; (void)dl; __builtin_trap(); }

__attribute__((weak)) int gf_vram_match(struct gf2_chan *ch,const void *owner,
    u32 id,u32 gen,u32 off,u32 bytes,struct gf_vram_reservation *out)
{ (void)ch;(void)owner;(void)id;(void)gen;(void)off;(void)bytes;(void)out;__builtin_trap(); }
