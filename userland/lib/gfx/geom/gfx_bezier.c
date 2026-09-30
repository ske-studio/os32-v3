/* ======================================================================== */
/*  GFX_BEZIER.C — ベジェ曲線描画 (libos32gfx)                             */
/*                                                                          */
/*  de Casteljauアルゴリズム（再帰中点分割）による2次/3次ベジェ曲線。       */
/*  全て整数演算のみ。FPU不要。                                             */
/* ======================================================================== */

#include "libos32gfx.h"

/* 再帰の最大深度 (安全弁) */
#define BEZIER_MAX_DEPTH  12

/* ======================================================================== */
/*  平坦性テスト                                                            */
/*                                                                          */
/*  制御点が始点→終点の直線から threshold ピクセル以内なら「平坦」と判定。  */
/*  距離の計算は外積ベース (整数演算のみ):                                  */
/*    d = |cross(P3-P0, Pi-P0)| / |P3-P0|                                  */
/*  sqrtを避けるため、d^2 * |P3-P0|^2 vs threshold^2 * |P3-P0|^2 で比較。  */
/* ======================================================================== */

static int is_flat3(int x0, int y0, int x1, int y1,
                    int x2, int y2, int x3, int y3)
{
    /* ベクトル P0→P3 */
    long dx = (long)(x3 - x0);
    long dy = (long)(y3 - y0);
    long len2 = dx * dx + dy * dy;
    long cross1, cross2;
    long dist1_sq, dist2_sq;
    long thresh;

    /* 始点と終点が同じ場合 */
    if (len2 == 0) {
        /* 全制御点が近ければ平坦 */
        long d1 = (long)(x1 - x0) * (x1 - x0) + (long)(y1 - y0) * (y1 - y0);
        long d2 = (long)(x2 - x0) * (x2 - x0) + (long)(y2 - y0) * (y2 - y0);
        return (d1 <= 4 && d2 <= 4);
    }

    /* P1 の直線からの距離^2 * len2 */
    cross1 = dx * (long)(y1 - y0) - dy * (long)(x1 - x0);
    dist1_sq = cross1 * cross1;

    /* P2 の直線からの距離^2 * len2 */
    cross2 = dx * (long)(y2 - y0) - dy * (long)(x2 - x0);
    dist2_sq = cross2 * cross2;

    /* 閾値: 1px → threshold^2 * len2 = len2 */
    thresh = len2;

    return (dist1_sq <= thresh && dist2_sq <= thresh);
}

static int is_flat2(int x0, int y0, int x1, int y1, int x2, int y2)
{
    long dx = (long)(x2 - x0);
    long dy = (long)(y2 - y0);
    long len2 = dx * dx + dy * dy;
    long cross1;
    long dist1_sq;

    if (len2 == 0) {
        long d1 = (long)(x1 - x0) * (x1 - x0) + (long)(y1 - y0) * (y1 - y0);
        return (d1 <= 4);
    }

    cross1 = dx * (long)(y1 - y0) - dy * (long)(x1 - x0);
    dist1_sq = cross1 * cross1;

    return (dist1_sq <= len2);
}

/* ======================================================================== */
/*  2次ベジェ曲線 (3制御点: P0, P1, P2)                                    */
/*  de Casteljau中点分割:                                                   */
/*    L0 = P0                                                               */
/*    L1 = (P0 + P1) / 2                                                    */
/*    L2 = M = (P0 + 2*P1 + P2) / 4                                        */
/*    R0 = M, R1 = (P1 + P2) / 2, R2 = P2                                  */
/* ======================================================================== */

static void bezier2_recursive(int x0, int y0, int x1, int y1,
                              int x2, int y2, u8 color, int depth)
{
    int mx01, my01, mx12, my12, mx, my;

    if (depth >= BEZIER_MAX_DEPTH || is_flat2(x0, y0, x1, y1, x2, y2)) {
        gfx_line(x0, y0, x2, y2, color);
        return;
    }

    /* 中点分割 */
    mx01 = (x0 + x1) / 2;
    my01 = (y0 + y1) / 2;
    mx12 = (x1 + x2) / 2;
    my12 = (y1 + y2) / 2;
    mx   = (mx01 + mx12) / 2;
    my   = (my01 + my12) / 2;

    bezier2_recursive(x0, y0, mx01, my01, mx, my, color, depth + 1);
    bezier2_recursive(mx, my, mx12, my12, x2, y2, color, depth + 1);
}

void gfx_bezier2(int x0, int y0, int x1, int y1,
                 int x2, int y2, u8 color)
{
    bezier2_recursive(x0, y0, x1, y1, x2, y2, color, 0);
}

/* ======================================================================== */
/*  3次ベジェ曲線 (4制御点: P0, P1, P2, P3)                                */
/*  de Casteljau中点分割:                                                   */
/*    M01 = (P0+P1)/2    M12 = (P1+P2)/2    M23 = (P2+P3)/2                */
/*    M012 = (M01+M12)/2  M123 = (M12+M23)/2                               */
/*    M = (M012+M123)/2                                                     */
/* ======================================================================== */

static void bezier3_recursive(int x0, int y0, int x1, int y1,
                              int x2, int y2, int x3, int y3,
                              u8 color, int depth)
{
    int mx01, my01, mx12, my12, mx23, my23;
    int mx012, my012, mx123, my123;
    int mx, my;

    if (depth >= BEZIER_MAX_DEPTH ||
        is_flat3(x0, y0, x1, y1, x2, y2, x3, y3)) {
        gfx_line(x0, y0, x3, y3, color);
        return;
    }

    /* 中点分割 */
    mx01 = (x0 + x1) / 2;   my01 = (y0 + y1) / 2;
    mx12 = (x1 + x2) / 2;   my12 = (y1 + y2) / 2;
    mx23 = (x2 + x3) / 2;   my23 = (y2 + y3) / 2;

    mx012 = (mx01 + mx12) / 2;  my012 = (my01 + my12) / 2;
    mx123 = (mx12 + mx23) / 2;  my123 = (my12 + my23) / 2;

    mx = (mx012 + mx123) / 2;   my = (my012 + my123) / 2;

    bezier3_recursive(x0,  y0,  mx01, my01, mx012, my012, mx, my,
                      color, depth + 1);
    bezier3_recursive(mx,  my,  mx123, my123, mx23, my23, x3, y3,
                      color, depth + 1);
}

void gfx_bezier3(int x0, int y0, int x1, int y1,
                 int x2, int y2, int x3, int y3, u8 color)
{
    bezier3_recursive(x0, y0, x1, y1, x2, y2, x3, y3, color, 0);
}

/* ======================================================================== */
/*  3次ベジェ曲線 (太線) — ストローク方式                                   */
/*                                                                          */
/*  de Casteljau分割で平坦セグメントに到達したら、                          */
/*  その線分を太さ分だけ上下にずらした2本のラインで描画する。               */
/*  簡易的だが386 16MHzでも軽量に動作する。                                 */
/* ======================================================================== */

static void thick_line(int x0, int y0, int x1, int y1,
                       int thickness, u8 color)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int half = thickness / 2;
    int i;

    /* 線分に垂直な方向にオフセットして複数本描画 */
    /* 垂直方向の近似: 線分が水平寄りなら上下に、垂直寄りなら左右にずらす */
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    if (dx >= dy) {
        /* 水平寄り → Y方向にオフセット */
        for (i = -half; i <= half; i++) {
            gfx_line(x0, y0 + i, x1, y1 + i, color);
        }
    } else {
        /* 垂直寄り → X方向にオフセット */
        for (i = -half; i <= half; i++) {
            gfx_line(x0 + i, y0, x1 + i, y1, color);
        }
    }
}

static void bezier3_thick_recursive(int x0, int y0, int x1, int y1,
                                    int x2, int y2, int x3, int y3,
                                    int thickness, u8 color, int depth)
{
    int mx01, my01, mx12, my12, mx23, my23;
    int mx012, my012, mx123, my123;
    int mx, my;

    if (depth >= BEZIER_MAX_DEPTH ||
        is_flat3(x0, y0, x1, y1, x2, y2, x3, y3)) {
        thick_line(x0, y0, x3, y3, thickness, color);
        return;
    }

    mx01 = (x0 + x1) / 2;   my01 = (y0 + y1) / 2;
    mx12 = (x1 + x2) / 2;   my12 = (y1 + y2) / 2;
    mx23 = (x2 + x3) / 2;   my23 = (y2 + y3) / 2;

    mx012 = (mx01 + mx12) / 2;  my012 = (my01 + my12) / 2;
    mx123 = (mx12 + mx23) / 2;  my123 = (my12 + my23) / 2;

    mx = (mx012 + mx123) / 2;   my = (my012 + my123) / 2;

    bezier3_thick_recursive(x0,  y0,  mx01, my01, mx012, my012, mx, my,
                            thickness, color, depth + 1);
    bezier3_thick_recursive(mx,  my,  mx123, my123, mx23, my23, x3, y3,
                            thickness, color, depth + 1);
}

void gfx_bezier3_thick(int x0, int y0, int x1, int y1,
                       int x2, int y2, int x3, int y3,
                       int thickness, u8 color)
{
    if (thickness <= 1) {
        gfx_bezier3(x0, y0, x1, y1, x2, y2, x3, y3, color);
        return;
    }
    bezier3_thick_recursive(x0, y0, x1, y1, x2, y2, x3, y3,
                            thickness, color, 0);
}
