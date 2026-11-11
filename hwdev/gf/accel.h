/* GeForce layer 3 internal ABI. Canonical contract:
 * docs/decisions/realhw/GF_FEAT3_DESIGN.md sections 2-10 and A0 addendum.
 * Foreground only, one cooperative owner; no IRQ submission or reentry. */
#ifndef HWDEV_GF_ACCEL_H
#define HWDEV_GF_ACCEL_H
#include <stdbool.h>
#include "vram_view.h"

enum gf3_rc {
    GF3_OK=0, GF3_EINVAL=-301, GF3_EFORMAT=-302, GF3_ERANGE=-303,
    GF3_ESTATE=-304, GF3_EUNSUPPORTED=-305, GF3_EBUSY=-306
};
enum gf3_fmt {
    GF3_INDEX8, GF3_RGB565, GF3_XRGB8888, GF3_ARGB8888,
    GF3_Y8, GF3_YUY2, GF3_UYVY
};

/* Section 3: creation order and OS32 handles, starting at F2's reserved
 * layer-3 base. Classes: nouveau (c03c1a1), nvkm/engine/gr/nv44.c:83-100
 * (paths relative to drivers/gpu/drm/nouveau/). Use 0062/008a/3089. */
enum gf3_object_index {
    GF3_OBJ_NULL, GF3_OBJ_SURFACE2D, GF3_OBJ_BETA1, GF3_OBJ_BETA4,
    GF3_OBJ_PATTERN, GF3_OBJ_ROP, GF3_OBJ_GDI, GF3_OBJ_BLIT,
    GF3_OBJ_SIFM, GF3_OBJ_CLIP, GF3_OBJ_M2MF, GF3_OBJ_IFC,
    GF3_OBJECT_COUNT
};
#define GF3_HANDLE_NULL (GF2_HANDLE_LAYER3_FIRST + GF3_OBJ_NULL)
#define GF3_HANDLE_SURFACE2D (GF2_HANDLE_LAYER3_FIRST + GF3_OBJ_SURFACE2D)
#define GF3_HANDLE_BETA1 (GF2_HANDLE_LAYER3_FIRST + GF3_OBJ_BETA1)
#define GF3_HANDLE_BETA4 (GF2_HANDLE_LAYER3_FIRST + GF3_OBJ_BETA4)
#define GF3_HANDLE_PATTERN (GF2_HANDLE_LAYER3_FIRST + GF3_OBJ_PATTERN)
#define GF3_HANDLE_ROP (GF2_HANDLE_LAYER3_FIRST + GF3_OBJ_ROP)
#define GF3_HANDLE_GDI (GF2_HANDLE_LAYER3_FIRST + GF3_OBJ_GDI)
#define GF3_HANDLE_BLIT (GF2_HANDLE_LAYER3_FIRST + GF3_OBJ_BLIT)
#define GF3_HANDLE_SIFM (GF2_HANDLE_LAYER3_FIRST + GF3_OBJ_SIFM)
#define GF3_HANDLE_CLIP (GF2_HANDLE_LAYER3_FIRST + GF3_OBJ_CLIP)
#define GF3_HANDLE_M2MF (GF2_HANDLE_LAYER3_FIRST + GF3_OBJ_M2MF)
#define GF3_HANDLE_IFC (GF2_HANDLE_LAYER3_FIRST + GF3_OBJ_IFC)
#define GF3_HANDLE_NOTIFY (GF2_HANDLE_LAYER3_FIRST + GF3_OBJECT_COUNT)
#define GF3_HANDLE_GART_DMA (GF2_HANDLE_LAYER3_FIRST + GF3_OBJECT_COUNT + 1)
#define GF3_HANDLE_VRAM_DMA GF2_HANDLE_VRAM_DMA
#define GF3_HANDLE_PCI_DMA GF2_HANDLE_PCI_DMA
#define GF3_CLASS_NULL GF2_GR_NULL
#define GF3_CLASS_SURFACE2D GF2_GR_SURFACE2D
#define GF3_CLASS_BETA1 GF2_GR_BETA1
#define GF3_CLASS_BETA4 GF2_GR_BETA4
#define GF3_CLASS_PATTERN GF2_GR_PATTERN
#define GF3_CLASS_ROP GF2_GR_ROP
#define GF3_CLASS_GDI GF2_GR_GDI
#define GF3_CLASS_BLIT GF2_GR_IMAGEBLIT
#define GF3_CLASS_SIFM GF2_GR_SIFM_NV30
#define GF3_CLASS_CLIP GF2_GR_CLIP
#define GF3_CLASS_M2MF GF2_GR_M2MF
#define GF3_CLASS_IFC GF2_GR_IFC
#define GF3_CLASS_NOTIFY GF2_DMA_IN_MEMORY
#define GF3_DMA_TARGET_VRAM GF2_DMA_TARGET_VRAM
#define GF3_DMA_ACCESS_RW GF2_DMA_ACCESS_RW

/* xf86-video-nouveau/src/nv04_accel.h:23-47; section 3 moves only M2MF
 * from subc0 to subc1 because F2 owns reference on subc0. Subc7 is unused. */
enum gf3_subchannel {
    GF3_SUBC_REFERENCE=GF2_REFERENCE_SUBCHANNEL, GF3_SUBC_M2MF=1,
    GF3_SUBC_SURFACE2D=2, GF3_SUBC_GDI=3, GF3_SUBC_BLIT=4,
    GF3_SUBC_IFC=5, GF3_SUBC_MISC=6
};
#define GF3_TIMEOUT_US GF2_TIMEOUT_US
/* OS32 section 3 notifier storage: 32B, separate from its 16B ctxdma.
 * nvkm/subdev/instmem/nv40.c:86-89 returns an aperture-relative offset;
 * absolute VRAM offset is vram_bytes-reserved+notify_storage.off. */
#define GF3_NOTIFY_BYTES 32U
/* xf86-video-nouveau/src/nv04_exa.c:296-319: IFC height, row padding and
 * push limit; the one context scratch array also serves mono packing. */
#define GF3_IFC_MAX_HEIGHT 1024U
#define GF3_IFC_MIN_ROW_BYTES 4U
#define GF3_IFC_ROW_ALIGN 8U
#define GF3_SCRATCH_WORDS 1792U
/* nouveau-v6.1/drivers/gpu/drm/nouveau/nv04_fbcon.c:114-122. */
#define GF3_MONO_BATCH_WORDS 128U
/* Section 7 / xf86-video-nouveau/src/nv04_exa.c:399-427.
 * Row limit and F2's one-page staging capacity are separate constraints. */
#define GF3_M2MF_MAX_ROWS GF2_METHOD_COUNT_MAX
#define GF3_STAGING_BYTES GF2_STAGING_BYTES

/* Offsets/bytes are VRAM bytes. ID/generation come from a live reservation;
 * INDEX8 is a palette index, Y8 is luminance. B1 matches the identity first. */
struct gf3_surface {
    u32 off, bytes, pitch, width, height;
    enum gf3_fmt fmt;
    u32 reservation_id, generation;
    u32 dma;
};
struct gf3_rect { u32 x, y, w, h; };
struct gf3_ram { void *ptr; u32 bytes, pitch; }; /* borrowed kernel RAM */
/* Production owns one immovable context. Inst nodes must never be copied.
 * Failed stop retains ownership; recover rebuilds objects for the new epoch. */
typedef int (*gf3_range_fn)(void *opaque, u32 handle, u32 off, u32 bytes);
struct gf3_ctx {
    struct gf2_chan *ch;
    struct gf2_object obj[GF3_OBJECT_COUNT];
    struct gf_inst notify_storage;
    struct gf2_object notify;
    u32 notify_off;
    u32 epoch;
    int last_rc;
    bool ready;
    u32 scratch[GF3_SCRATCH_WORDS];
    gf3_range_fn range_q;
    void *range_opaque;
};

/* G2 addendum: B1 owns the seven-row format table; callers receive copies.
 * xf86-video-nouveau/src/nv_accel_common.c:254-276 and nv04_exa.c:97-129,
 * 180-200,302-319; SIFM: nv04_xv_blit.c:68-93,99-117. Unsupported fields
 * are NONE; INDEX8 and luminance Y8 retain distinct meanings. */
#define GF3_FMT_NONE 0U
struct gf3_format { u32 cpp, sf, sf_gdi, gdi, pat, ifc, sifm; };
enum gf3_surface_use {
    GF3_USE_BYTES, GF3_USE_2D, GF3_USE_GDI, GF3_USE_IFC,
    GF3_USE_SIFM_SRC
};
/* bytes is the envelope through the last valid byte, including row gaps. */
struct gf3_span { u32 off, rowbytes, pitch, rows, bytes; };
/* Required out: NULL is EINVAL; unknown fmt is EFORMAT. YUY2/UYVY have
 * cpp=2 and only sifm set; Y8 has no IFC format. */
int gf3_format_get(enum gf3_fmt fmt, struct gf3_format *out);
/* off is relative to a capacity-byte base. Require off<=capacity and
 * pitch>=rowbytes; validate (rows-1)*pitch+rowbytes <= capacity-off without
 * overflowing. Either zero rowbytes or zero rows gives bytes=0. NULL out
 * is EINVAL; other range errors are ERANGE. Publish only on success. */
int gf3_span_check(u32 off, u32 capacity, u32 pitch, u32 rowbytes,
                   u32 rows, struct gf3_span *out);
/* NULL c/ch/s/fmt/span or unknown use is EINVAL. Match the live reservation
 * first with gf_vram_match(ch,NULL,ID,generation,off,bytes,&key), propagating
 * its rc. Then check format, nonempty surface geometry, pitch>=width*cpp,
 * full-surface last byte and rectangle containment without overflow.
 * span.off is absolute VRAM, rowbytes=r.w*cpp, rows=r.h, pitch=s->pitch.
 * BYTES accepts all formats without packed-field limits. 2D/GDI/IFC require
 * sf/sf_gdi/ifc respectively (NONE is EFORMAT), 16bit pitch and packed
 * coordinates/dimensions. SIFM_SRC requires sifm, 16bit pitch/full geometry,
 * 12.4 start points and even YUV width/r.x/r.w. No new pitch/width alignment.
 * Both outputs remain unchanged on failure. Ready/epoch/RUN/fault checks
 * belong to drawing entries; IFC transfer limits belong to B6.
 * Entries handle zero rectangles after required pointer/state checks,
 * before accessing surfaces/RAM; all nonempty checks precede any emit. */
int gf3_surface_check(struct gf3_ctx *c, const struct gf3_surface *s,
                      struct gf3_rect r, enum gf3_surface_use use,
                      struct gf3_format *fmt, struct gf3_span *span);
/* Borrow ram->ptr unchanged. NULL ram/out, or NULL ptr for a nonempty
 * transfer, is EINVAL. Otherwise use span_check(0,bytes,pitch,rowbytes,rows).
 * Callers compute rowbytes without overflow (mono ceil(w/8), else w*cpp).
 * Publish out only on success. */
int gf3_ram_check(const struct gf3_ram *ram, u32 rowbytes, u32 rows,
                  struct gf3_span *out);

/* dl is the operation-wide absolute deadline, never renewed. FIFO pause
 * keeps F2's independent timeout; after resume check ch->last_rc/state.
 * Init registers while paused, initializes in RUN, waits then sets ready.
 * Fini closes admission, drains, pauses and deletes in reverse order.
 * Delete GR objects, notifier ctxdma, then storage; retain on stop failure. */
int gf3_init(struct gf3_ctx *, struct gf2_chan *, u32 dl);
int gf3_fini(struct gf3_ctx *, u32 dl);
/* Internal setters emit only; alu is GX 0..15. */
int gf3_surface_set(struct gf3_ctx *, const struct gf3_surface *src,
                    const struct gf3_surface *dst, bool gdi, u32 dl);
int gf3_rop_set(struct gf3_ctx *, u8 subc, u8 alu, u32 mask,
                enum gf3_fmt, u32 dl);
/* Drawing/transfers are synchronous: one outer reference emit/wait, with
 * each staging reuse waiting separately. F1/F2 errors propagate unchanged
 * to last_rc; never replay a partly submitted operation. Coordinates are
 * nonnegative and in the surface; zero width/height submits nothing. */
int gf3_fill(struct gf3_ctx *, const struct gf3_surface *, struct gf3_rect,
             u32 color, u8 alu, u32 mask, u32 dl);
int gf3_copy(struct gf3_ctx *, const struct gf3_surface *src,
             const struct gf3_surface *dst, struct gf3_rect sr,
             u32 dx, u32 dy, u8 alu, u32 mask, u32 dl);
int gf3_mono(struct gf3_ctx *, const struct gf3_surface *, struct gf3_rect,
             const struct gf3_ram *bits, u32 fg, u32 bg,
             struct gf3_rect clip, u32 dl);
int gf3_ifc(struct gf3_ctx *, const struct gf3_surface *, struct gf3_rect,
            const struct gf3_ram *, u32 dl);
int gf3_upload(struct gf3_ctx *, const struct gf3_surface *, struct gf3_rect,
               const struct gf3_ram *, bool force_m2mf, u32 dl);
int gf3_download(struct gf3_ctx *, const struct gf3_surface *, struct gf3_rect,
                 struct gf3_ram *, u32 dl);
/* Offsets are relative to the selected DMA objects, never RAM VA casts. */
int gf3_m2mf(struct gf3_ctx *, u32 in_handle, u32 out_handle, u32 in_off,
             u32 out_off, u32 in_pitch, u32 out_pitch,
             u32 rowbytes, u32 rows, u32 dl);
int gf3_scale(struct gf3_ctx *, const struct gf3_surface *src,
              const struct gf3_surface *dst, struct gf3_rect sr,
              struct gf3_rect dr, struct gf3_rect clip, bool bilinear, u32 dl);
/* Test-only adapter, never registered as the GUI present_rect hook.
 * ram.ptr is the transfer rectangle's top left, not the full backbuffer. */
int gf3_present_m2mf(struct gf3_ctx *, const struct gf3_surface *scan,
                     struct gf3_rect, const struct gf3_ram *, u32 dl);

/* Module-only HAL adapters; the backend registration uses the same guard.
 * ctx is borrowed; scan is copied by value only after C1 reserves it.
 * Stop/fault/recover closes admission and dispatch before invalidation. */
int geforce_fill_rect(int x, int y, int w, int h, u8 color);
int geforce_blit(int dx, int dy, int sx, int sy, int w, int h);
/* G2 implements; G1 uses this in gf_direct_ops. */
int geforce_fill_rop(int x, int y, int w, int h, u8 color, u8 rop);
void gf_hook_ctx_bind(struct gf3_ctx *);
void gf_hook_scan_bind(bool active, const struct gf3_surface *);
/* C1 owns (backend_geforce.c/gf_hook.c): the HAL descriptor and format-specific
 * hook follow a confirmed scan change of head 0 (F4 C2-scan links.scan_changed,
 * PM 2026-11-04). head!=0 is ignored. */
void gf_hook_scan_changed(u8 head, const struct gf3_surface *front, u32 mode_id);

/* Addendum N4: C0a implements this copy-out of backend framebuffer metadata
 * for C1. All outputs are required; video_framebuffer must have succeeded.
 * Available during video_start, before backend active becomes true. phys is
 * a physical address (struct video_fb, include/video_service.h); width and
 * height are pixels. No reservation/hook publication occurs here: C1 uses
 * its reserved BAR1 base to form off, reserves pitch*height, adds the
 * reservation_id/generation, then binds the completed scan. */
struct video_fb;
void geforce_backend_framebuffer_info(struct video_fb *out,
                                      u32 *width, u32 *height);

/* Internal GNU11 dispatch. Kernel rereads its slot each call and never
 * retains this table after unload. All pointers borrow module ownership. */
struct gf3_test_ops {
    struct gf3_ctx *ctx;
    const struct gf3_surface *test_surface;
    __typeof__(&gf3_fill) fill;
    __typeof__(&gf3_copy) copy;
    __typeof__(&gf3_mono) mono;
    __typeof__(&gf3_ifc) ifc;
    __typeof__(&gf3_upload) upload;
    __typeof__(&gf3_download) download;
    __typeof__(&gf3_scale) scale;
    __typeof__(&gf3_present_m2mf) present_m2mf;
    __typeof__(&gf_vram_view_map) map;
    __typeof__(&gf_vram_view_unmap) unmap;
};
/* Emit drawing commands without inserting or waiting for a fence. */
int gf3_fill_emit(struct gf3_ctx *, const struct gf3_surface *, struct gf3_rect, u32 color, u8 alu, u32 mask, u32 dl);
int gf3_copy_emit(struct gf3_ctx *, const struct gf3_surface *, const struct gf3_surface *, struct gf3_rect, u32 dx, u32 dy, u8 alu, u32 mask, u32 dl);
int gf3_scale_emit(struct gf3_ctx *, const struct gf3_surface *, const struct gf3_surface *, struct gf3_rect, struct gf3_rect, struct gf3_rect, bool, u32 dl);
/* Install the GART range validator; NULL rejects all GART surfaces. */
void gf3_range_query_set(struct gf3_ctx *, gf3_range_fn, void *opaque);
/* Pattern data for monochrome or color ROP3 operations. */
struct gf3_pattern { u32 kind; u32 mono[2]; u32 color[64]; u32 fg, bg; };
#define GF3_PAT_MONO 1
#define GF3_PAT_COLOR 2
#define GF3_ALU_KEEP 0xffu
/* Configure the source color key. */
/* Configure constant and per-pixel alpha. */
/* Configure ternary raster operation and its pattern. */
int gf3_rop3_set(struct gf3_ctx *, u8 subc, u8 rop3, const struct gf3_pattern *, enum gf3_fmt, u32 dl);

#endif
