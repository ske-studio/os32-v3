#include "gfx_dirty.h"

/* ======================================================================== */
/*  変更矩形の整列・併合・追加                                            */
/* ======================================================================== */
void gfx_dirty_add(DirtyRectQueue *queue, int width, int height, int x, int y, int w, int h)
{
    int aligned_x, aligned_w;
    int right;
    int i;
    GFX_Rect *r;

    /* マージ判定用のギャップ閾値 (px)。
     * この距離以内の矩形は統合する (32pxアライメントに合わせる) */
    int gap = BITS_PER_U32;

    int new_x, new_y, new_r, new_b;

    if (!gfx_clip_screen(&x, &y, &w, &h, width, height)) return;
    right = x + w;

    /* 32ピクセル境界 (4バイト) にアライメント:
     * xは切り捨て、幅は切り上げ
     */
    aligned_x = x & ~(BITS_PER_U32 - 1);
    aligned_w = ((right + (BITS_PER_U32 - 1)) & ~(BITS_PER_U32 - 1)) - aligned_x;

    /* --- マージ判定 --- */
    new_x = aligned_x;
    new_y = y;
    new_r = aligned_x + aligned_w;
    new_b = y + h;

    for (i = 0; i < queue->count; i++) {
        int ex, ey, er, eb;

        r = &queue->rects[i];
        ex = r->x;
        ey = r->y;
        er = ex + r->w;
        eb = ey + r->h;

        /* 完全包含チェック: 新rectが既存rectに包含されるなら何もしない */
        if (new_x >= ex && new_y >= ey && new_r <= er && new_b <= eb) {
            return;
        }

        /* オーバーラップ or 隣接(gap px以内)なら統合 */
        if (new_x <= er + gap && new_r >= ex - gap &&
            new_y <= eb + gap && new_b >= ey - gap) {
            /* バウンディングボックスに統合 */
            if (new_x < ex) r->x = new_x; else new_x = ex;
            if (new_y < ey) r->y = new_y; else new_y = ey;
            if (new_r > er) er = new_r;
            if (new_b > eb) eb = new_b;
            r->w = er - r->x;
            r->h = eb - r->y;
            return;
        }
    }

    /* マージ対象なし → 新規追加 */

    /* キューがいっぱいの場合はバウンディングボックスに圧縮 */
    if (queue->count >= MAX_DIRTY_RECTS) {
        int min_x = width, min_y = height, max_x = 0, max_y = 0;
        for (i = 0; i < queue->count; i++) {
            if (queue->rects[i].x < min_x) min_x = queue->rects[i].x;
            if (queue->rects[i].y < min_y) min_y = queue->rects[i].y;
            if (queue->rects[i].x + queue->rects[i].w > max_x) max_x = queue->rects[i].x + queue->rects[i].w;
            if (queue->rects[i].y + queue->rects[i].h > max_y) max_y = queue->rects[i].y + queue->rects[i].h;
        }
        queue->count = 1;
        queue->rects[0].x = min_x;
        queue->rects[0].y = min_y;
        queue->rects[0].w = max_x - min_x;
        queue->rects[0].h = max_y - min_y;
    }

    r = &queue->rects[queue->count++];
    r->x = new_x;
    r->y = new_y;
    r->w = new_r - new_x;
    r->h = new_b - new_y;
}

/* ======================================================================== */
/*  ステイルページ対策: 前フレームのdirty rectを現フレームにマージ            */
/* ======================================================================== */
void __attribute__((noinline)) gfx_dirty_merge(DirtyRectQueue *queue, const DirtyRectQueue *previous, int width, int height)
{
    for (int i = 0; i < previous->count; i++) {
        const GFX_Rect *r = gfx_dirty_at(previous, i);
        gfx_dirty_add(queue, width, height, r->x, r->y, r->w, r->h);
    }
}
