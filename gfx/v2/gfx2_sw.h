/* HAL v2 A0 declarations; HAL_V2_DESIGN §11. */
#ifndef GFX_V2_GFX2_SW_H
#define GFX_V2_GFX2_SW_H
#include "gfx2_int.h"
#include "gfx2_fmt.h"
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
#define GFX2_SW_ROW_BYTES 16384u                          /* sync の面の行の作業 (I2 の追記) */
extern u8 gfx2_sw_rowbuf[2][GFX2_SW_ROW_BYTES];       /* S* が共有。実体は sw_table.c */

#endif
