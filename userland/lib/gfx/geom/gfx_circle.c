/* ======================================================================== */
/*  GFX_CIRCLE.C — サークル系描画プリミティブ (libos32gfx)                  */
/*                                                                          */
/*  Bresenham（ミッドポイント）アルゴリズムによる円・楕円・弧の描画。        */
/*  全てバックバッファ上に描画し、gfx_pixel / gfx_hline を使用する。        */
/* ======================================================================== */

#include "libos32gfx.h"
#include "libgfx_internal.h"   /* gfx_span_raw (H2b: プレーン/パックド共通スパン) */
#include "os32api.h"

/* gfx_fill_circle \u3067 dirty rect \u767b\u9332\u3092\u56de\u907f\u3057\u3066\u76f4\u63a5\u547c\u3073\u51fa\u3059 */

/* ======================================================================== */
/*  円の輪郭 (Midpoint Circle Algorithm)                                    */
/* ======================================================================== */

void gfx_circle(int cx, int cy, int r, u8 color)
{
    int x = 0;
    int y = r;
    int d = 1 - r;

    if (r <= 0) {
        if (r == 0) gfx_pixel(cx, cy, color);
        return;
    }

    /* dirty rect をバウンディングボックスで1回だけ登録 */
    gfx_api->gfx_add_dirty_rect(cx - r, cy - r, r * 2 + 1, r * 2 + 1);

    while (x <= y) {
        /* dirty rect 登録なし版で描画 */
        gfx_pixel_nodirty(cx + x, cy + y, color);
        gfx_pixel_nodirty(cx - x, cy + y, color);
        gfx_pixel_nodirty(cx + x, cy - y, color);
        gfx_pixel_nodirty(cx - x, cy - y, color);
        gfx_pixel_nodirty(cx + y, cy + x, color);
        gfx_pixel_nodirty(cx - y, cy + x, color);
        gfx_pixel_nodirty(cx + y, cy - x, color);
        gfx_pixel_nodirty(cx - y, cy - x, color);

        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}

/* ======================================================================== */
/*  塗りつぶし円                                                            */
/*  水平走査線ベースで高速に描画 (gfx_hline使用)                            */
/* ======================================================================== */

void gfx_fill_circle(int cx, int cy, int r, u8 color)
{
    int x = 0;
    int y = r;
    int d = 1 - r;
    int base;

    if (r <= 0) {
        if (r == 0) gfx_pixel(cx, cy, color);
        return;
    }

    /* dirty rect をバウンディングボックスで1回だけ登録 */
    gfx_api->gfx_add_dirty_rect(cx - r, cy - r, r * 2 + 1, r * 2 + 1);

    /* 内部では gfx_span_raw を直接呼び、dirty rect の個別登録を回避 */
    while (x <= y) {
        int lx, rx2, ly, hy;

        /* cy + y */
        ly = cy + y;
        if (ly >= 0 && ly < gfx_fb.height) {
            lx = cx - x; rx2 = cx + x;
            if (lx < 0) lx = 0;
            if (rx2 >= gfx_fb.width) rx2 = gfx_fb.width - 1;
            if (lx <= rx2) {
                base = ly * gfx_fb.pitch;
                gfx_span_raw(base, lx, rx2, color);
            }
        }

        /* cy - y */
        hy = cy - y;
        if (hy >= 0 && hy < gfx_fb.height && hy != ly) {
            lx = cx - x; rx2 = cx + x;
            if (lx < 0) lx = 0;
            if (rx2 >= gfx_fb.width) rx2 = gfx_fb.width - 1;
            if (lx <= rx2) {
                base = hy * gfx_fb.pitch;
                gfx_span_raw(base, lx, rx2, color);
            }
        }

        /* cy + x */
        ly = cy + x;
        if (ly >= 0 && ly < gfx_fb.height) {
            lx = cx - y; rx2 = cx + y;
            if (lx < 0) lx = 0;
            if (rx2 >= gfx_fb.width) rx2 = gfx_fb.width - 1;
            if (lx <= rx2) {
                base = ly * gfx_fb.pitch;
                gfx_span_raw(base, lx, rx2, color);
            }
        }

        /* cy - x */
        hy = cy - x;
        if (hy >= 0 && hy < gfx_fb.height && hy != ly) {
            lx = cx - y; rx2 = cx + y;
            if (lx < 0) lx = 0;
            if (rx2 >= gfx_fb.width) rx2 = gfx_fb.width - 1;
            if (lx <= rx2) {
                base = hy * gfx_fb.pitch;
                gfx_span_raw(base, lx, rx2, color);
            }
        }

        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}

/* ======================================================================== */
/*  楕円の輪郭 (Midpoint Ellipse Algorithm)                                 */
/* ======================================================================== */

void gfx_ellipse(int cx, int cy, int rx, int ry, u8 color)
{
    long rx2, ry2;
    long tworx2, twory2;
    long x, y;
    long px, py;
    long p;

    if (rx <= 0 || ry <= 0) {
        if (rx == 0 && ry == 0) gfx_pixel(cx, cy, color);
        return;
    }

    rx2 = (long)rx * rx;
    ry2 = (long)ry * ry;
    tworx2 = 2 * rx2;
    twory2 = 2 * ry2;

    /* 領域1: dy/dx > -1 */
    x = 0;
    y = ry;
    px = 0;
    py = tworx2 * y;
    p = ry2 - rx2 * ry + rx2 / 4;

    while (px < py) {
        gfx_pixel(cx + (int)x, cy + (int)y, color);
        gfx_pixel(cx - (int)x, cy + (int)y, color);
        gfx_pixel(cx + (int)x, cy - (int)y, color);
        gfx_pixel(cx - (int)x, cy - (int)y, color);

        x++;
        px += twory2;
        if (p < 0) {
            p += ry2 + px;
        } else {
            y--;
            py -= tworx2;
            p += ry2 + px - py;
        }
    }

    /* 領域2: dy/dx <= -1 */
    p = ry2 * (x * 2 + 1) * (x * 2 + 1) / 4
        + rx2 * (y - 1) * (y - 1)
        - rx2 * ry2;

    while (y >= 0) {
        gfx_pixel(cx + (int)x, cy + (int)y, color);
        gfx_pixel(cx - (int)x, cy + (int)y, color);
        gfx_pixel(cx + (int)x, cy - (int)y, color);
        gfx_pixel(cx - (int)x, cy - (int)y, color);

        y--;
        py -= tworx2;
        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += twory2;
            p += rx2 - py + px;
        }
    }
}

/* ======================================================================== */
/*  塗りつぶし楕円                                                          */
/* ======================================================================== */

void gfx_fill_ellipse(int cx, int cy, int rx, int ry, u8 color)
{
    long rx2, ry2;
    long tworx2, twory2;
    long x, y;
    long px, py;
    long p;
    int last_y;

    if (rx <= 0 || ry <= 0) {
        if (rx == 0 && ry == 0) gfx_pixel(cx, cy, color);
        return;
    }

    rx2 = (long)rx * rx;
    ry2 = (long)ry * ry;
    tworx2 = 2 * rx2;
    twory2 = 2 * ry2;

    /* 領域1 */
    x = 0;
    y = ry;
    px = 0;
    py = tworx2 * y;
    p = ry2 - rx2 * ry + rx2 / 4;
    last_y = (int)y;

    while (px < py) {
        x++;
        px += twory2;
        if (p < 0) {
            p += ry2 + px;
        } else {
            /* y が変わった → 前の y で水平線を引く */
            gfx_hline(cx - (int)(x - 1), cy + last_y, (int)(x - 1) * 2 + 1, color);
            gfx_hline(cx - (int)(x - 1), cy - last_y, (int)(x - 1) * 2 + 1, color);
            y--;
            py -= tworx2;
            p += ry2 + px - py;
            last_y = (int)y;
        }
    }

    /* 領域2 */
    p = ry2 * (x * 2 + 1) * (x * 2 + 1) / 4
        + rx2 * (y - 1) * (y - 1)
        - rx2 * ry2;

    while (y >= 0) {
        gfx_hline(cx - (int)x, cy + (int)y, (int)x * 2 + 1, color);
        gfx_hline(cx - (int)x, cy - (int)y, (int)x * 2 + 1, color);

        y--;
        py -= tworx2;
        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += twory2;
            p += rx2 - py + px;
        }
    }
}

/* ======================================================================== */
/*  弧の描画 (度数指定)                                                     */
/*                                                                          */
/*  start_deg, end_deg: 0-360 (度数)。3時方向=0°、反時計回り。              */
/*  start_deg < end_deg の場合はその区間を描画。                            */
/*  start_deg > end_deg の場合は 360° を跨いで描画。                        */
/* ======================================================================== */

/* 固定小数点sin/cosテーブル (256スケール, 0-90度を1度刻み) */
static const i16 sin_table[91] = {
      0,   4,   9,  13,  18,  22,  27,  31,  36,  40,
     44,  49,  53,  57,  62,  66,  70,  74,  79,  83,
     87,  91,  95,  99, 103, 107, 111, 114, 118, 122,
    126, 129, 133, 136, 139, 143, 146, 149, 152, 155,
    158, 161, 164, 167, 169, 172, 175, 177, 179, 182,
    184, 186, 188, 190, 192, 194, 196, 197, 199, 200,
    202, 203, 204, 206, 207, 208, 209, 210, 211, 211,
    212, 213, 213, 214, 214, 215, 215, 215, 216, 216,
    216, 216, 216, 216, 216, 216, 215, 215, 215, 214,
    256
};

/* sin(deg) * 256 を返す (deg: 0-360) */
static int isin(int deg)
{
    int neg = 0;
    int val;
    deg = deg % 360;
    if (deg < 0) deg += 360;

    if (deg > 180) { deg -= 180; neg = 1; }
    if (deg > 90) deg = 180 - deg;

    val = (int)sin_table[deg];
    return neg ? -val : val;
}



/* 角度が指定範囲内にあるかを判定 */
static int in_arc_range(int angle, int start, int end)
{
    angle = angle % 360;
    if (angle < 0) angle += 360;
    start = start % 360;
    if (start < 0) start += 360;
    end = end % 360;
    if (end < 0) end += 360;

    if (start <= end) {
        return (angle >= start && angle <= end);
    } else {
        /* 360度跨ぎ */
        return (angle >= start || angle <= end);
    }
}

void gfx_arc(int cx, int cy, int r, int start_deg, int end_deg, u8 color)
{
    int x = 0;
    int y = r;
    int d = 1 - r;
    int angle;

    if (r <= 0) return;

    while (x <= y) {
        /* 8方向の角度を計算してフィルタリング */
        /* (cx+x, cy-y) → 右上領域 */
        /* 簡易アプローチ: 各ピクセルの角度を計算して範囲判定 */

        /* atan2近似: 8つの対称点それぞれの角度 */
        /* 3時方向=0°、反時計回り */

        /* (+x, -y) = 右上 → 約 0-45° */
        angle = 0;
        if (x == 0 && y > 0) angle = 90;
        else if (y == 0 && x > 0) angle = 0;
        else {
            /* 近似: atan2(y,x) ≈ 45 * y/x (第1象限上半分) */
            angle = 45 * x / y;
        }

        /* 実際の8対称点の角度 */
        if (in_arc_range(angle, start_deg, end_deg))
            gfx_pixel(cx + y, cy - x, color);        /* 0〜45° */
        if (in_arc_range(90 - angle, start_deg, end_deg))
            gfx_pixel(cx + x, cy - y, color);        /* 45〜90° */
        if (in_arc_range(90 + angle, start_deg, end_deg))
            gfx_pixel(cx - x, cy - y, color);        /* 90〜135° */
        if (in_arc_range(180 - angle, start_deg, end_deg))
            gfx_pixel(cx - y, cy - x, color);        /* 135〜180° */
        if (in_arc_range(180 + angle, start_deg, end_deg))
            gfx_pixel(cx - y, cy + x, color);        /* 180〜225° */
        if (in_arc_range(270 - angle, start_deg, end_deg))
            gfx_pixel(cx - x, cy + y, color);        /* 225〜270° */
        if (in_arc_range(270 + angle, start_deg, end_deg))
            gfx_pixel(cx + x, cy + y, color);        /* 270〜315° */
        if (in_arc_range(360 - angle, start_deg, end_deg))
            gfx_pixel(cx + y, cy + x, color);        /* 315〜360° */

        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}

/* ======================================================================== */
/*  太線円 (線幅指定)                                                       */
/*  2重円を描いて間を塗ることで太線を実現                                   */
/* ======================================================================== */

void gfx_circle_thick(int cx, int cy, int r, int thickness, u8 color)
{
    int outer_r = r;
    int inner_r = r - thickness + 1;
    int x, y, d;

    if (thickness <= 1) {
        gfx_circle(cx, cy, r, color);
        return;
    }
    if (inner_r < 0) inner_r = 0;

    /* 外円の走査でhlineを引き、内円より外側のみ描画 */
    x = 0;
    y = outer_r;
    d = 1 - outer_r;

    while (x <= y) {
        int inner_x_at_y;
        int inner_y_at_x;

        /* 外円のy座標に対応する内円のx座標 */
        if (y < inner_r) {
            long tmp = (long)inner_r * inner_r - (long)y * y;
            inner_x_at_y = 0;
            {
                int s;
                for (s = inner_r; s >= 0; s--) {
                    if ((long)s * s <= tmp) { inner_x_at_y = s; break; }
                }
            }
        } else {
            inner_x_at_y = 0;
        }

        /* 外円のx座標に対応する内円のy座標 */
        if (x < inner_r) {
            long tmp = (long)inner_r * inner_r - (long)x * x;
            inner_y_at_x = 0;
            {
                int s;
                for (s = inner_r; s >= 0; s--) {
                    if ((long)s * s <= tmp) { inner_y_at_x = s; break; }
                }
            }
        } else {
            inner_y_at_x = 0;
        }

        /* 上下走査線 (y座標方向) */
        if (y > inner_y_at_x || x >= inner_r) {
            gfx_hline(cx - x, cy + y, x * 2 + 1, color);
            gfx_hline(cx - x, cy - y, x * 2 + 1, color);
        } else {
            /* ドーナツ形: 左右の帯だけ描画 */
            gfx_hline(cx - x, cy + y, x - inner_x_at_y, color);
            gfx_hline(cx + inner_x_at_y + 1, cy + y, x - inner_x_at_y, color);
            gfx_hline(cx - x, cy - y, x - inner_x_at_y, color);
            gfx_hline(cx + inner_x_at_y + 1, cy - y, x - inner_x_at_y, color);
        }

        /* 左右走査線 (x座標方向で対称) */
        if (x > inner_x_at_y || y >= inner_r) {
            gfx_hline(cx - y, cy + x, y * 2 + 1, color);
            gfx_hline(cx - y, cy - x, y * 2 + 1, color);
        } else {
            gfx_hline(cx - y, cy + x, y - inner_y_at_x, color);
            gfx_hline(cx + inner_y_at_x + 1, cy + x, y - inner_y_at_x, color);
            gfx_hline(cx - y, cy - x, y - inner_y_at_x, color);
            gfx_hline(cx + inner_y_at_x + 1, cy - x, y - inner_y_at_x, color);
        }

        if (d < 0) {
            d += 2 * x + 3;
        } else {
            d += 2 * (x - y) + 5;
            y--;
        }
        x++;
    }
}
