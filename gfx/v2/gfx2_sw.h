/* HAL v2 A0 declarations; HAL_V2_DESIGN §11. */
#ifndef GFX_V2_GFX2_SW_H
#define GFX_V2_GFX2_SW_H
#include "gfx2_int.h"
struct gfx2_fmt_info { u8 bpp, cpp, has_alpha, indexed; u8 rs, rb, gs, gb, bs, bb, as, ab; }; /* 位置・幅 */
int  gfx2_fmt_info(u32 fmt, struct gfx2_fmt_info *o);      /* C2。0/INVAL */
u32  gfx2_px_read(const u8 *p, u32 fmt);  void gfx2_px_write(u8 *p, u32 fmt, u32 v);   /* C2 */
u32  gfx2_to_argb(u32 fmt, u32 v, const u32 *pal256);       /* C2。§3-3 の拡げ */
int  gfx2_from_argb(u32 fmt, u32 argb, u32 *v);            /* C2。INDEX8 は NOSYS */
u32  gfx2_yuv_argb(u8 y, u8 u, u8 v);  u32 gfx2_div255(u32 t);                       /* C2 */
struct gfx2_pattern { u32 kind; /* 0=無し 1=mono 2=色 */ u8 mono[8]; u32 color[64]; GFX2_Color fg, bg; };
struct gfx2_view {                    /* sw が見る面。C9 が作る */
    u8 *base; u32 pitch; u16 w, h; u8 fmt;
    const u32 *pal;                   /* INDEX8 → ARGB。C9 が gfx2_pal (§9-2) を指す */
    GfxSyncCtx *sync;                 /* 非 NULL: 行の書き・読みは gfxsync_*、base は使わない */
};
struct gfx2_exec {                    /* 走査 1 が作り、走査 2・check・emit が同じ物を見る */
    const GFX2_Op *op; struct gfx2_surf *dst, *src; struct gfx2_view dv, sv;
    struct gfx2_rect clip;            /* クリップ後の書き先 (面と CLIP の交わり) */
    const struct gfx2_pattern *pat;   /* submit.c の束の状態 */
    const u8 *cpu; u32 cpu_pitch;     /* MONO・HOSTDATA・mask の元 (ユーザーの VA、走査 1 で範囲を確かめ済み) */
    u32 how;                          /* GFX2_HOW_* */
};
typedef int (*gfx2_sw_fn)(const struct gfx2_exec *, struct gfx2_rect *touched);  /* 0/INVAL。touched は書いた外接 */
int gfx2_sw_fill(const struct gfx2_exec *, struct gfx2_rect *touched);      /* S1 */
int gfx2_sw_copy(const struct gfx2_exec *, struct gfx2_rect *touched);      /* S2 */
int gfx2_sw_rop(const struct gfx2_exec *, struct gfx2_rect *touched);       /* S3 */
int gfx2_sw_rop4(const struct gfx2_exec *, struct gfx2_rect *touched);      /* S4 */
int gfx2_sw_mono(const struct gfx2_exec *, struct gfx2_rect *touched);      /* S5 */
int gfx2_sw_hostdata(const struct gfx2_exec *, struct gfx2_rect *touched);  /* S6 */
int gfx2_sw_readback(const struct gfx2_exec *, u8 *kbuf);                   /* S7 (表に載せず C9 が直に呼ぶ) */
int gfx2_sw_stretch(const struct gfx2_exec *, struct gfx2_rect *touched);   /* S8 */
int gfx2_sw_ckey(const struct gfx2_exec *, struct gfx2_rect *touched);      /* S9 */
int gfx2_sw_alpha(const struct gfx2_exec *, struct gfx2_rect *touched);     /* S10 */
int gfx2_sw_line(const struct gfx2_exec *, struct gfx2_rect *touched);      /* S11 */
extern const gfx2_sw_fn gfx2_sw_table[GFX2_OP_MAX + 1];   /* 実体は C9 (submit.c)。CKEY 付きの COPY は sw_ckey */

#endif
