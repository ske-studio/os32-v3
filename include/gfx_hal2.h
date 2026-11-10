/* HAL v2 A0 declarations; HAL_V2_DESIGN §11. */
#ifndef INCLUDE_GFX_HAL2_H
#define INCLUDE_GFX_HAL2_H
#include "types.h"
#include "../sdk/include/os32/os32_gfx2.h"
struct gfx2_exec;
struct gfx2_surf;
typedef struct GfxBackendV2 {   /* include/gfx_hal2.h (A0)。戻り値は OS32_ERR_*、第 1 引数は ctx。全部の hook は必須
                                   (使わない物は NOSYS を返す関数を置く。NULL は登録で拒む) */
    u32 abi; void *ctx;
    int (*caps)(void *, GFX2_Caps *);                                   /* 副作用なし */
    int (*check)(void *, const struct gfx2_exec *);                     /* 0 = HW でできる、NOSYS = できない */
    int (*heap_range)(void *, u32 *base, u32 *size, u32 *phys);         /* heap に渡す VRAM の範囲と VRAM の offset 0 の物理番地 (C17・C7 の PTE の元) */
    int (*alloc)(void *, struct gfx2_surf *);                           /* heap の置き場に装置の予約、priv を埋める */
    int (*release)(void *, struct gfx2_surf *);                         /* GPU の使用の終わりと写像の解除の後だけ */
    int (*kva)(void *, const struct gfx2_surf *, u8 **va);              /* CPU から触る kernel の VA。NOSYS → sw は NOSUPPORT */
    int (*gart_bind)(void *, const u32 *pfn, u32 n, u32 *gpu_off);
    int (*gart_unbind)(void *, u32 gpu_off, u32 n);
    int (*emit)(void *, const struct gfx2_exec *, u32 fence);           /* 検査済みの 1 命令の描画だけ積む。fence は記録用で印は積まない
                                                                           (印は後の fence_emit)。INVAL・NOSYS は何も積まなかった時だけ返す */
    int (*readback)(void *, const struct gfx2_exec *, u8 *kbuf);        /* 完了まで待って kbuf に */
    int (*kick)(void *);
    int (*fence_emit)(void *, u32 v2, u32 dl_us);
    int (*fence_state)(void *, u32 *completed, u32 *failed_upto);
    int (*fence_wait)(void *, u32 v2, u32 dl_us);
    int (*enum_head)(void *, u32 head, GFX2_HeadInfo *);
    int (*enum_conn)(void *, u32 conn, GFX2_ConnectorInfo *);
    int (*enum_mode)(void *, u32 head, u32 fmt, u32 i, GFX2_ModeInfo *);
    int (*edid)(void *, u32 conn, u32 block, u8 *out128);
    int (*mode_set)(void *, u32 head, u32 conn_mask, const GFX2_ModeInfo *, const struct gfx2_surf *);
    int (*flip)(void *, u32 head, const struct gfx2_surf *, u32 flags, u32 dl_us);
    int (*flip_done)(void *, u32 head, u32 *retired_sid);               /* vblank の処理 (C13) が呼ぶ */
    int (*vblank_wait)(void *, u32 head, u32 after, u32 dl_us, u32 *seen);
    int (*palette)(void *, u32 first, u32 count, const u32 *xrgb);
    int (*gamma)(void *, u32 head, const u16 *rgb768);
    int (*cursor_image)(void *, u32 head, const struct gfx2_surf *);
    int (*cursor_move)(void *, u32 head, i32 x, i32 y);
    int (*cursor_show)(void *, u32 head, u32 show);
    int (*dpms)(void *, u32 head, u32 state);
    int (*stop)(void *);                                                /* 喪失の時。失敗 = LOST_HELD */
} GfxBackendV2;
#define GFX_HAL2_ABI 1

#endif
