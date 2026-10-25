#ifndef __GFX_INTERNAL_H
#define __GFX_INTERNAL_H

#include "gfx.h"
#include "pc98.h"
#include "memmap.h"
#include "palette.h"

/* Clip before adding coordinates: callers may supply any signed KAPI value.
 * Positive extents plus negative origins cannot overflow; then use subtraction. */
static inline int gfx_clip_screen(int *x, int *y, int *w, int *h,
                                  int width, int height)
{
    if (*w <= 0 || *h <= 0 || *x >= width || *y >= height) return 0;
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*w > width - *x) *w = width - *x;
    if (*h > height - *y) *h = height - *y;
    return *w > 0 && *h > 0;
}

/* ======================================================================== */
/*  内部変数アクセス                                                        */
/* ======================================================================== */
extern u8 *bb[GVRAM_PLANES];
extern u8 *bb_b;
extern u8 *bb_r;
extern u8 *bb_g;
extern u8 *bb_i;

extern int vram_scroll_y;
extern int gfx_current_height;  /* 現在高さ: planar 200/400、packed 480。終了後は400 */

/* ページフリッピング状態 (200/400ライン共通) */
extern int gfx_flip_enabled;     /* 0=無効, 1=有効 */
extern int gfx_display_page;     /* 現在表示中のページ (0 or 1) */

#define MAX_DIRTY_RECTS 64
typedef struct {
    int count;
    GFX_Rect rects[MAX_DIRTY_RECTS];
} DirtyRectQueue;

extern DirtyRectQueue dirty_queue;
extern DirtyRectQueue prev_dirty;  /* 前フレームdirty (ステイルページ対策) */

extern u32 gfx_init_gen;
extern int gfx_diag_altwin;
u32 pegc_window_base(void);
int pegc_select_window(u32 base);
void gfx_diag_first_present(u32 before);
void gfx_scroll_init(void);
/* PC98 planar の初期化・スプラッシュ終了専用 (戻りの描画ページは1)。 */
void gfx_clear_planar_pages(u32 plane_size);

/* ======================================================================== */
/*  I/O・メモリ操作インライン                                               */
/* ======================================================================== */

/* ポート I/O は include/io.h の共通実装を使う。
 * かつてはここに同一のインライン asm が重複定義されていた (IR3)。
 * 既存呼び出し (43 箇所) を書き換えないためエイリアスで提供する。 */
#include "io.h"
#define _out(port, val)  outp((port), (val))
#define _in(port)        inp((port))

static inline void _memcpy_w(void *dst, const void *src, unsigned int words) {
    __asm__ volatile("rep movsw"
                     : "+D"(dst), "+S"(src), "+c"(words)
                     : : "memory");
}

static inline void _memcpy_d(void *dst, const void *src, unsigned int dwords) {
    __asm__ volatile("rep movsl"
                     : "+D"(dst), "+S"(src), "+c"(dwords)
                     : : "memory");
}

static inline void _memset_w(void *dst, unsigned int val16, unsigned int words) {
    __asm__ volatile("rep stosw"
                     : "+D"(dst), "+c"(words)
                     : "a"((unsigned short)val16)
                     : "memory");
}

static inline void _memset_d(void *dst, unsigned int val32, unsigned int dwords) {
    __asm__ volatile("rep stosl"
                     : "+D"(dst), "+c"(dwords)
                     : "a"(val32)
                     : "memory");
}

#endif /* __GFX_INTERNAL_H */
