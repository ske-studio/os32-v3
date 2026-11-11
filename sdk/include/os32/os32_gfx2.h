/* HAL v2 A0 declarations; HAL_V2_DESIGN §11. */
#ifndef SDK_INCLUDE_OS32_OS32_GFX2_H
#define SDK_INCLUDE_OS32_OS32_GFX2_H
#define GFX2_MAX_CMDS 64
#define GFX2_OP_FILL 1
#define GFX2_OP_COPY 2
#define GFX2_OP_STRETCH 3
#define GFX2_OP_MONO 4
#define GFX2_OP_HOSTDATA 5
#define GFX2_OP_READBACK 6
#define GFX2_OP_ROP4 7
#define GFX2_OP_ALPHA 8
#define GFX2_OP_LINE 9
#define GFX2_OP_SET_PATTERN 10
#define GFX2_OP_MAX 10
#define GFX2_ROP_SRCCOPY 0xCC
#define GFX2_ROP_PATCOPY 0xF0
#define GFX2_HOW_NOSUPPORT 0
#define GFX2_HOW_SW 1
#define GFX2_HOW_HW 2
#define GFX2_CAP2D(op) (1UL << (op))   /* caps2d_hw・caps2d_sw・exact の bit (§4-1) */
#define GFX2_AF_ACTIVE 1
#define GFX2_AF_ONBOARD 2   /* AdapterInfo.flags */
#define GFX2_F_SRCKEY 0x01
#define GFX2_F_DSTKEY 0x02
#define GFX2_F_CLIP 0x04
#define GFX2_F_FILTER 0x08
#define GFX2_F_ALPHA_PIXEL 0x10
#define GFX2_F_PAT 0x20
#define GFX2_F_PAT_COLOR 0x40
#define GFX2_CX_TRANSPARENT 1
#define GFX2_POOL_VRAM 1
#define GFX2_POOL_GART 2
#define GFX2_POOL_SYSMEM 3
#define GFX2_USE_RENDER2D 0x01
#define GFX2_USE_SCANOUT 0x02
#define GFX2_USE_CURSOR 0x04
#define GFX2_USE_STAGING 0x08
#define GFX2_USE_TEXTURE 0x10
#define GFX2_USE_V1 0x80
#define GFX2_TILE_LINEAR 0
#define GFX2_TILE_TILED 1
#define GFX2_ST_FREE 0
#define GFX2_ST_IDLE 1
#define GFX2_ST_LOCKED_RO 2
#define GFX2_ST_LOCKED_RW 3
#define GFX2_ST_CLOSING 4
#define GFX2_ST_LOST 5
#define GFX2_ST_QUAR 6
#define GFX2_LOCK_READONLY 1
#define GFX2_LOCK_DISCARD 2
#define GFX2_LOCK_DONOTWAIT 4
#define GFX2_FLIP_VBLANK 1
#define GFX2_ROLE_DISPLAY 1
#define GFX2_ROLE_CLIENT 2
#define GFX2_CONN_VGA 1
#define GFX2_CONN_DVI_I 2
#define GFX2_CONN_DVI_D 3
#define GFX2_CONN_TV_COMPOSITE 4
#define GFX2_CONN_TV_SVIDEO 5
#define GFX2_CONN_PC98_ANALOG 6
#define GFX2_CONN_UNKNOWN 0
#define GFX2_CONN_ON 1
#define GFX2_CONN_OFF 2
#define GFX2_DPMS_ON 0
#define GFX2_DPMS_STANDBY 1
#define GFX2_DPMS_SUSPEND 2
#define GFX2_DPMS_OFF 3
#define GFX2_MODE_NHSYNC 1
#define GFX2_MODE_NVSYNC 2
#define GFX2_MODE_INTERLACE 4
#define GFX2_MODE_DBLSCAN 8
#define GFX2_CONN_VGA 1
#define GFX2_CONN_DVI_I 2
#define GFX2_CONN_DVI_D 3
#define GFX2_CONN_TV_COMPOSITE 4
#define GFX2_CONN_TV_SVIDEO 5
#define GFX2_CONN_PC98_ANALOG 6
#define GFX2_CONN_UNKNOWN 0
#define GFX2_CONN_ON 1
#define GFX2_CONN_OFF 2

#define GFX2_FMT_PLANAR4 0
#define GFX2_FMT_INDEX8 1
#define GFX2_FMT_RGB565 2
#define GFX2_FMT_XRGB8888 3
#define GFX2_FMT_ARGB8888 4
#define GFX2_FMT_XRGB1555 5
#define GFX2_FMT_ARGB1555 6
#define GFX2_FMT_A8 7
#define GFX2_FMT_L8 8
#define GFX2_FMT_YUY2 9
#define GFX2_FMT_UYVY 10
typedef struct { unsigned long v; unsigned long x; } GFX2_Color;
typedef struct {
    unsigned short op, rop;            /*  0: rop は ROP3 (下位 8 bit) か ROP4 (前景 8 + 背景 8) */
    unsigned long flags;              /*  4: SRCKEY・DSTKEY・CLIP・FILTER・ALPHA_PIXEL・PAT */
    unsigned long dst, src;           /*  8: 面の sid (src=0 = 無し) */
    signed short dx, dy; unsigned short dw, dh; /* 16 */
    signed short sx, sy; unsigned short sw, sh; /* 24 */
    GFX2_Color fg, bg;      /* 32 */
    GFX2_Color skey, dkey;  /* 48 */
    unsigned long data, pitch;        /* 64: CPU 側の元/先 (ユーザーの VA) */
    signed short cx, cy; unsigned short cw, ch; /* 72: クリップ (flags の CLIP) */
} GFX2_Op;                  /* 80 */

typedef struct {
unsigned long size, adapter, backend_kind, tier3d_now, tier3d_hw, overlay_now, overlay_hw, overlay_fmt;
unsigned long caps2d_hw, caps2d_sw, exact;
unsigned long fmt_surface, fmt_render2d, fmt_scanout, fmt_src_stretch;
unsigned long max_w, max_h, max_pitch, pitch_align, offset_align, scanout_align, stretch_min_q16, stretch_max_q16, max_cmds;
unsigned long vram_bytes, vram_cpu_bytes, gart_bytes, gart_page, fence_hw, async;
unsigned long heads, connectors, palette_entries, gamma_bits, cursor_w, cursor_h, cursor_fmt, flip_vblank, vblank_irq, thermal;
unsigned long cpu_direct, sync_mode;
char sync_profile[16];
unsigned long vs_version, ps_version, max_tex_w, max_tex_h, max_tex_aspect, max_tex_stages, max_streams, max_prim, max_index, max_rt, tex_caps, raster_caps, zcmp_caps, blend_caps;
unsigned long reserved[4];
} GFX2_Caps;
typedef struct { unsigned long sid, gen; unsigned short w, h; unsigned char fmt, pool, usage, tiling; unsigned long pitch, bytes, gpu_off, state; } GFX2_SurfaceDesc;
typedef struct { unsigned short vendor, device; unsigned long subsys; unsigned char revision, bus, dev, fn; char name[64], driver[32]; unsigned long driver_ver, vram_bytes, gart_bytes; unsigned char heads, connectors, backend_kind, flags; unsigned long reserved; } GFX2_AdapterInfo;
typedef struct { unsigned long head, attached, connector_mask; signed short x, y; unsigned short w, h; unsigned long rotation, mode_id, scanout_sid, fmt_scanout; unsigned short max_w, max_h; unsigned long reserved[6]; } GFX2_HeadInfo;
typedef struct { unsigned long id, type, possible_heads, clone_mask, status, edid_blocks; char monitor_name[16]; unsigned short mm_w, mm_h; unsigned long preferred_mode_id, reserved[4]; } GFX2_ConnectorInfo;
typedef struct { unsigned long id; unsigned short w, h; unsigned long fmt, refresh_mhz, pixclk_khz; unsigned short htotal, vtotal; unsigned long hsync_khz10, flags; unsigned char source, scaling; unsigned short pad; unsigned long reserved; } GFX2_ModeInfo;
int gfx2_adapter_count(void);
int gfx2_adapter_info(unsigned long a, GFX2_AdapterInfo *o);
int gfx2_adapter_gen(unsigned long a, unsigned long *gen);
int gfx2_caps(unsigned long a, GFX2_Caps *o, unsigned long size);
int gfx2_check_op(unsigned long a, const GFX2_Op *shape, unsigned long sfmt, unsigned long dfmt, unsigned long spool, unsigned long dpool, unsigned long *how);
int gfx2_surface_create(unsigned long a, unsigned short w, unsigned short h, unsigned char fmt, unsigned char pool, unsigned char usage, unsigned long *sid);
int gfx2_surface_free(unsigned long sid);
int gfx2_surface_desc(unsigned long sid, GFX2_SurfaceDesc *o);
int gfx2_screen_surface(unsigned long a, unsigned long head, unsigned long role, unsigned long *sid);
int gfx2_lock(unsigned long sid, unsigned long flags, void **va, unsigned long *pitch);
int gfx2_unlock(unsigned long sid);
int gfx2_submit(unsigned long a, const GFX2_Op *ops, unsigned long n, unsigned long gen, unsigned long *done, unsigned long *fence);
int gfx2_fence_wait(unsigned long a, unsigned long fence, unsigned long deadline_ms);
int gfx2_fence_query(unsigned long a, unsigned long *completed, unsigned long *failed_upto);
int gfx2_head_info(unsigned long a, unsigned long head, GFX2_HeadInfo *o);
int gfx2_connector_info(unsigned long a, unsigned long c, GFX2_ConnectorInfo *o);
int gfx2_connector_edid(unsigned long a, unsigned long c, unsigned long block, unsigned char *out128);
int gfx2_mode_count(unsigned long a, unsigned long head, unsigned long fmt);
int gfx2_mode_get(unsigned long a, unsigned long head, unsigned long fmt, unsigned long i, GFX2_ModeInfo *o);
int gfx2_mode_set(unsigned long a, unsigned long head, unsigned long conn_mask, unsigned long mode_id, unsigned long sid);
int gfx2_flip(unsigned long a, unsigned long head, unsigned long sid, unsigned long flags, unsigned long deadline_ms);
int gfx2_vblank_wait(unsigned long a, unsigned long head, unsigned long after, unsigned long deadline_ms, unsigned long *seen);
int gfx2_palette(unsigned long a, unsigned long first, unsigned long count, const unsigned long *xrgb);
int gfx2_gamma(unsigned long a, unsigned long head, const unsigned short *rgb768);
int gfx2_cursor_image(unsigned long a, unsigned long head, unsigned long sid);
int gfx2_cursor_move(unsigned long a, unsigned long head, signed long x, signed long y);
int gfx2_cursor_show(unsigned long a, unsigned long head, unsigned long show);
int gfx2_dpms(unsigned long a, unsigned long head, unsigned long state);
int gfx2_present(unsigned long a, unsigned long head, signed short x, signed short y, unsigned short w, unsigned short h);
/* The wire ABI is ILP32. LP64 hosts may include this header for unrelated
 * pure-function tests; the target and gfx2 ABI probe enforce every size. */
typedef char gfx2_chk_Color[sizeof(unsigned long) != 4 || sizeof(GFX2_Color) == 8 ? 1 : -1];
typedef char gfx2_chk_Op[sizeof(unsigned long) != 4 || sizeof(GFX2_Op) == 80 ? 1 : -1];
typedef char gfx2_chk_Caps[sizeof(unsigned long) != 4 || sizeof(GFX2_Caps) == 256 ? 1 : -1];
typedef char gfx2_chk_SurfaceDesc[sizeof(unsigned long) != 4 || sizeof(GFX2_SurfaceDesc) == 32 ? 1 : -1];
typedef char gfx2_chk_AdapterInfo[sizeof(unsigned long) != 4 || sizeof(GFX2_AdapterInfo) == 128 ? 1 : -1];
typedef char gfx2_chk_HeadInfo[sizeof(unsigned long) != 4 || sizeof(GFX2_HeadInfo) == 64 ? 1 : -1];
typedef char gfx2_chk_ConnectorInfo[sizeof(unsigned long) != 4 || sizeof(GFX2_ConnectorInfo) == 64 ? 1 : -1];
typedef char gfx2_chk_ModeInfo[sizeof(unsigned long) != 4 || sizeof(GFX2_ModeInfo) == 40 ? 1 : -1];
#endif
