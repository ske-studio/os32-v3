/* ======================================================================== */
/*  gfx_fill.c — スキャンラインフィルエンジン (libos32gfx)                  */
/*                                                                          */
/*  エッジテーブル構築 + even-odd rule によるスキャンラインフィル。           */
/*  ベジェ曲線の平坦化 (de Casteljau) もこのモジュールに含む。              */
/*                                                                          */
/*  全て整数演算のみ (FPU不使用)。                                          */
/* ======================================================================== */

#include "libos32gfx.h"
#include "libgfx_internal.h"
#include "os32api.h"

/* ベジェ平坦化の最大深度 */
#define FLATTEN_MAX_DEPTH 10

/* Phase 4b: scanline_fill から gfx_span_raw を直接呼び出す (dirty rect省略版) */

/* ======================================================================== */
/*  エッジテーブル操作                                                      */
/* ======================================================================== */

GFX_EdgeTable *gfx_edge_table_create(int max_edges, int max_intersect)
{
    GFX_EdgeTable *et;

    et = (GFX_EdgeTable *)gfx_api->mem_alloc(sizeof(GFX_EdgeTable));
    if (!et) return 0;

    et->edges = (GFX_Edge *)gfx_api->mem_alloc(max_edges * sizeof(GFX_Edge));
    if (!et->edges) {
        gfx_api->mem_free(et);
        return 0;
    }

    et->intersect_buf = (int *)gfx_api->mem_alloc(max_intersect * sizeof(int));
    if (!et->intersect_buf) {
        gfx_api->mem_free(et->edges);
        gfx_api->mem_free(et);
        return 0;
    }

    et->num_edges = 0;
    et->max_edges = max_edges;
    et->max_intersect = max_intersect;
    return et;
}

void gfx_edge_table_free(GFX_EdgeTable *et)
{
    if (!et) return;
    if (et->intersect_buf) gfx_api->mem_free(et->intersect_buf);
    if (et->edges) gfx_api->mem_free(et->edges);
    gfx_api->mem_free(et);
}

void gfx_edges_clear(GFX_EdgeTable *et)
{
    if (et) et->num_edges = 0;
}

void gfx_edge_add(GFX_EdgeTable *et, int x0, int y0, int x1, int y1)
{
    GFX_Edge *e;

    /* 水平線は無視 (スキャンラインフィルでは不要) */
    if (y0 == y1) return;
    if (!et || et->num_edges >= et->max_edges) return;

    e = &et->edges[et->num_edges];

    /* y0 < y1 になるよう正規化 */
    if (y0 > y1) {
        e->x0 = x1; e->y0 = y1;
        e->x1 = x0; e->y1 = y0;
    } else {
        e->x0 = x0; e->y0 = y0;
        e->x1 = x1; e->y1 = y1;
    }
    et->num_edges++;
}

/* ======================================================================== */
/*  3次ベジェ曲線の平坦化 (エッジテーブルに直線セグメントを追加)            */
/* ======================================================================== */

void gfx_bezier3_to_edges(GFX_EdgeTable *et,
                           int x0, int y0, int x1, int y1,
                           int x2, int y2, int x3, int y3, int depth)
{
    int mx01, my01, mx12, my12, mx23, my23;
    int mx012, my012, mx123, my123;
    int mx, my;

    /* 平坦性テスト */
    if (depth >= FLATTEN_MAX_DEPTH) {
        gfx_edge_add(et, x0, y0, x3, y3);
        return;
    }

    {
        long dx = (long)(x3 - x0);
        long dy = (long)(y3 - y0);
        long len2 = dx * dx + dy * dy;
        long cross1, cross2;

        if (len2 == 0) {
            long d1 = (long)(x1 - x0) * (x1 - x0) + (long)(y1 - y0) * (y1 - y0);
            long d2 = (long)(x2 - x0) * (x2 - x0) + (long)(y2 - y0) * (y2 - y0);
            if (d1 <= 4 && d2 <= 4) {
                gfx_edge_add(et, x0, y0, x3, y3);
                return;
            }
        } else {
            cross1 = dx * (long)(y1 - y0) - dy * (long)(x1 - x0);
            cross2 = dx * (long)(y2 - y0) - dy * (long)(x2 - x0);
            if (cross1 * cross1 <= len2 && cross2 * cross2 <= len2) {
                gfx_edge_add(et, x0, y0, x3, y3);
                return;
            }
        }
    }

    /* de Casteljau 中点分割 */
    mx01 = (x0 + x1) / 2;   my01 = (y0 + y1) / 2;
    mx12 = (x1 + x2) / 2;   my12 = (y1 + y2) / 2;
    mx23 = (x2 + x3) / 2;   my23 = (y2 + y3) / 2;

    mx012 = (mx01 + mx12) / 2;  my012 = (my01 + my12) / 2;
    mx123 = (mx12 + mx23) / 2;  my123 = (my12 + my23) / 2;

    mx = (mx012 + mx123) / 2;   my = (my012 + my123) / 2;

    gfx_bezier3_to_edges(et, x0, y0, mx01, my01, mx012, my012, mx, my, depth + 1);
    gfx_bezier3_to_edges(et, mx, my, mx123, my123, mx23, my23, x3, y3, depth + 1);
}

/* ======================================================================== */
/*  スキャンラインフィル (even-odd rule, DDA最適化)                          */
/* ======================================================================== */

/* アクティブエッジ (DDA用) — スタックに配置するため構造体を小さく保つ */
typedef struct {
    i32 x_fixed;   /* 現在のX座標 (16.16 固定小数点) */
    i32 dx_fixed;  /* 1スキャンラインあたりのX増分 (16.16 固定小数点) */
    int y_max;     /* エッジの終了Y座標 (exclusive) */
} AET_Entry;

/* 最大アクティブエッジ数 (パスあたり平均5エッジなので十分) */
#define AET_MAX 64

/* エッジのy0でソート (挿入ソート) */
static void sort_edges_by_y0(GFX_Edge *arr, int n)
{
    int i, j;
    GFX_Edge key;

    for (i = 1; i < n; i++) {
        key = arr[i];
        j = i - 1;
        while (j >= 0 && arr[j].y0 > key.y0) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

/* 交差点ソート (挿入ソート — 交差点数は通常少ない) */
static void sort_intersections(int *arr, int n)
{
    int i, j, key;

    for (i = 1; i < n; i++) {
        key = arr[i];
        j = i - 1;
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

void gfx_scanline_fill(GFX_EdgeTable *et, u8 color, int min_y, int max_y)
{
    int y, i;
    int edge_idx;       /* 次にAETに投入するエッジのインデックス */
    int aet_count;      /* AET内の有効エントリ数 */
    int base;           /* y * pitch (事前計算) */
    AET_Entry aet[AET_MAX];

    if (!et || et->num_edges == 0) return;
    if (min_y < 0) min_y = 0;
    if (max_y >= 400) max_y = 399;

    /* Phase 4a: dirty rect をバウンディングボックスで1回だけ事前登録 */
    /* (インナーループでの gfx_add_dirty_rect 呼び出しを排除) */
    {
        int gmin_x = 640, gmax_x = 0;
        for (i = 0; i < et->num_edges; i++) {
            int ex0 = et->edges[i].x0;
            int ex1 = et->edges[i].x1;
            if (ex0 < gmin_x) gmin_x = ex0;
            if (ex1 < gmin_x) gmin_x = ex1;
            if (ex0 > gmax_x) gmax_x = ex0;
            if (ex1 > gmax_x) gmax_x = ex1;
        }
        if (gmin_x < 0) gmin_x = 0;
        if (gmax_x >= 640) gmax_x = 639;
        if (gmin_x <= gmax_x) {
            gfx_api->gfx_add_dirty_rect(gmin_x & ~31, min_y,
                ((gmax_x + 32) & ~31) - (gmin_x & ~31),
                max_y - min_y + 1);
        }
    }

    /* エッジをy0でソート (DDA/AET管理の前提) */
    sort_edges_by_y0(et->edges, et->num_edges);

    edge_idx = 0;
    aet_count = 0;
    base = min_y * gfx_fb.pitch;

    for (y = min_y; y <= max_y; y++) {
        int n_intersect;

        /* 1. 新しいエッジをAETに追加 (y0 == y のもの) */
        while (edge_idx < et->num_edges && et->edges[edge_idx].y0 <= y) {
            GFX_Edge *e = &et->edges[edge_idx];
            edge_idx++;

            /* y >= y1 なら既に期限切れ */
            if (y >= e->y1) continue;

            if (aet_count < AET_MAX) {
                int dy = e->y1 - e->y0;
                int dx = e->x1 - e->x0;
                AET_Entry *a = &aet[aet_count];

                /* 16.16固定小数点でX座標とX増分を計算 (除算はここだけ) */
                a->x_fixed = (e->x0 << 16) + (i32)((long)dx * (y - e->y0) * 65536L / dy);
                a->dx_fixed = (i32)((long)dx * 65536L / dy);
                a->y_max = e->y1;
                aet_count++;
            }
        }

        /* 2. 期限切れエッジをAETから削除 (y >= y_max) */
        i = 0;
        while (i < aet_count) {
            if (y >= aet[i].y_max) {
                aet[i] = aet[aet_count - 1];
                aet_count--;
            } else {
                i++;
            }
        }

        if (aet_count < 2) {
            /* AETにエッジが1本以下 → 描画なし、X増分だけ更新 */
            for (i = 0; i < aet_count; i++) {
                aet[i].x_fixed += aet[i].dx_fixed;
            }
            base += gfx_fb.pitch;
            continue;
        }

        /* 3. 交差X座標を収集 (固定小数点→整数) */
        n_intersect = 0;
        for (i = 0; i < aet_count && n_intersect < et->max_intersect; i++) {
            et->intersect_buf[n_intersect++] = aet[i].x_fixed >> 16;
        }

        /* 4. ソート (Phase 4c: 交差点2個の場合はスワップのみ) */
        if (n_intersect > 2) {
            sort_intersections(et->intersect_buf, n_intersect);
        } else if (n_intersect == 2 &&
                   et->intersect_buf[0] > et->intersect_buf[1]) {
            int tmp = et->intersect_buf[0];
            et->intersect_buf[0] = et->intersect_buf[1];
            et->intersect_buf[1] = tmp;
        }

        /* 5. even-odd rule で塗りつぶし */
        /* Phase 4b: gfx_span_raw を直接呼び出し (dirty rectは事前登録済み) */
        for (i = 0; i + 1 < n_intersect; i += 2) {
            int x_start = et->intersect_buf[i];
            int x_end = et->intersect_buf[i + 1];

            if (x_start < 0) x_start = 0;
            if (x_end >= 640) x_end = 639;
            if (x_start <= x_end) {
                gfx_span_raw(base, x_start, x_end, color);
            }
        }

        /* 6. X座標をDDAで更新 (加算のみ!) */
        for (i = 0; i < aet_count; i++) {
            aet[i].x_fixed += aet[i].dx_fixed;
        }
        base += gfx_fb.pitch;
    }
}
