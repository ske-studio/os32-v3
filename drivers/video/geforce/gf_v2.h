/* HAL v2 A0 declarations; HAL_V2_DESIGN §11. */
#ifndef DRIVERS_VIDEO_GEFORCE_GF_V2_H
#define DRIVERS_VIDEO_GEFORCE_GF_V2_H
#include "gfx/v2/gfx2_sw.h"
#include "hwdev/gf/accel.h"
#define GF_V2_RING 256
struct gf_v2_ring_e { u32 v2; struct gf2_fence f; };
struct gf_v2_ring { u32 head, tail, count; struct gf_v2_ring_e e[GF_V2_RING]; };
struct gf_v2 {                                   /* 実体は G1 (gf_v2_ops.c の static 1 つ) */
    struct gf3_ctx *g3; struct gf2_chan *ch;
    struct gf_v2_ring ring;                      /* G4 だけが書く */
    u32 completed, failed_upto;                  /* G4 */
    struct gf_vram_reservation_node node[GFX2_MAX_SURFACES]; /* G9 (surf の priv[2] が番号) */
    u32 gart_handle, gart_base, gart_pages;      /* G5 */
};
int gf_v2_fence_emit(struct gf_v2 *, u32 v2, u32 dl);            /* G4、§6-3 のコード */
int gf_v2_fence_state(struct gf_v2 *, u32 *completed, u32 *failed_upto); /* G4 */
int gf_v2_fence_wait(struct gf_v2 *, u32 v2, u32 dl);            /* G4 */
int gf_v2_alloc(struct gf_v2 *, struct gfx2_surf *);             /* G9: gf_vram_reserve、priv[0..2] */
int gf_v2_release(struct gf_v2 *, struct gfx2_surf *);           /* G9: gf_vram_release */
int gf_v2_gart_bind(struct gf_v2 *, const u32 *pfn, u32 n, u32 *gpu_off);  /* G5 */
int gf_v2_gart_unbind(struct gf_v2 *, u32 gpu_off, u32 n);       /* G5 */
int gf_v2_gart_range(void *opaque, u32 handle, u32 off, u32 bytes);  /* G5。opaque は struct gf_v2 *。
                                                    handle が GART_DMA でなければ EINVAL、[off, off+bytes) が bind 済みの
                                                    頁に収まれば 0、外は ERANGE (u32 のあふれ込み)。G8 が callback 経由で呼ぶ */
/* G2 (gf_v2_display.c)。GfxBackendV2 の同名の hook の型そのもので、ctx は struct gf_v2 * */
int gf_v2_enum_head(void *, u32 head, GFX2_HeadInfo *);
int gf_v2_enum_conn(void *, u32 conn, GFX2_ConnectorInfo *);
int gf_v2_enum_mode(void *, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *);
int gf_v2_edid(void *, u32 conn, u32 block, u8 *out128);
int gf_v2_mode_set(void *, u32 head, u32 conn_mask, const GFX2_ModeInfo *, const struct gfx2_surf *);
int gf_v2_flip(void *, u32 head, const struct gfx2_surf *, u32 flags, u32 dl_us);
int gf_v2_flip_done(void *, u32 head, u32 *retired_sid);
int gf_v2_vblank_wait(void *, u32 head, u32 after, u32 dl_us, u32 *seen);
int gf_v2_palette(void *, u32 first, u32 count, const u32 *xrgb);
int gf_v2_gamma(void *, u32 head, const u16 *rgb768);
int gf_v2_cursor_image(void *, u32 head, const struct gfx2_surf *);
int gf_v2_cursor_move(void *, u32 head, i32 x, i32 y);
int gf_v2_cursor_show(void *, u32 head, u32 show);
int gf_v2_dpms(void *, u32 head, u32 state);

#endif
