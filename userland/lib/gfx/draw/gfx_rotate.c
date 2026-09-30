/* ======================================================================== */
/*  GFX_ROTATE.C — 回転ブリット (libos32gfx)                               */
/*                                                                          */
/*  GFX_Surface を任意角度で回転（＋拡大縮小）してBBに描画する。            */
/*  逆アフィン変換方式。SFC Mode 7 と同じ 8.8 固定小数点精度。              */
/*                                                                          */
/*  描画サイズはソースと同サイズに固定（クロップ方式）。                    */
/*  回転により外にはみ出る部分は切り捨てられる。                            */
/* ======================================================================== */

#include "libos32gfx.h"
#include "libgfx_internal.h"
#include "libos32math.h"
#include "os32api.h"

/* ソースサーフェスの4プレーンから1ピクセルを読み取る */
static u8 _read_pixel(const GFX_Surface *s, int x, int y)
{
    int off = y * s->pitch + (x >> 3);
    u8 bit = 0x80 >> (x & 7);
    u8 color = 0;
    int p;
    for (p = 0; p < 4; p++) {
        if (s->planes[p][off] & bit) color |= (1 << p);
    }
    return color;
}

/* ======================================================================== */
/*  gfx_blit_affine — 回転＋拡大縮小付きブリット                            */
/*                                                                          */
/*  dx, dy : 描画先の中心座標（回転中心がこの位置に来る）                   */
/*  src    : 回転元の GFX_Surface                                           */
/*  angle  : 角度 0-511 (512分割, isin互換)                               */
/*  scale  : 拡大率 8.8固定小数点 (256=等倍, 128=半分, 512=2倍)            */
/*  colorkey : 透過色 (この色のピクセルは描画しない)                        */
/* ======================================================================== */

void gfx_blit_affine(int dx, int dy,
                      const GFX_Surface *src,
                      int angle,
                      int scale,
                      u8 colorkey)
{
    i32 cos_v, sin_v;
    int cx, cy;
    i32 du_x, dv_x, du_y, dv_y;
    int bbox_x, bbox_y, bbox_w, bbox_h;
    i32 u_row_start, v_row_start;
    i32 u, v;
    int sx, sy;
    int ix, iy;
    int clip_left, clip_right;
    int draw_x0;
    u8 color;

    if (!src || scale == 0) return;

    /* 1. セットアップ: sin/cos LUT (15bit精度) を 8.8fp の増分に変換 */
    cos_v = (i32)icos(angle);
    sin_v = (i32)isin(angle);

    cx = src->w / 2;
    cy = src->h / 2;

    /*
     * LUT値は -32767〜+32767 (15bit精度, 1.0 = 32767)。
     * 8.8fp (256=1.0) での「1ピクセルあたりの増分」を求める。
     * 数式: step = (cos_v / 32768) * (256 / scale) * 256 (8.8fpにするため256倍)
     *            = (cos_v * 65536) / (32768 * scale)
     *            = (cos_v * 2) / scale
     */
    du_x =  ((i32)cos_v * 2) / scale;
    dv_x =  ((i32)sin_v * 2) / scale;
    du_y = -((i32)sin_v * 2) / scale;
    dv_y =  ((i32)cos_v * 2) / scale;

    /* 2. 描画領域の決定 (ソースサイズ固定クロップ) */
    bbox_w = src->w;
    bbox_h = src->h;
    bbox_x = dx - cx;
    bbox_y = dy - cy;

    /* 完全画面外チェック */
    if (bbox_x + bbox_w <= 0 || bbox_x >= gfx_fb.width ||
        bbox_y + bbox_h <= 0 || bbox_y >= gfx_fb.height) {
        return;
    }

    /* 3. 初期 u, v の計算 (描画矩形左上のソース座標) */
    u_row_start = (i32)(-cx) * du_x + (i32)(-cy) * du_y + (cx << 8);
    v_row_start = (i32)(-cx) * dv_x + (i32)(-cy) * dv_y + (cy << 8);

    /* Y軸クリッピング (上端) */
    if (bbox_y < 0) {
        int skip = -bbox_y;
        u_row_start += (i32)skip * du_y;
        v_row_start += (i32)skip * dv_y;
        bbox_h -= skip;
        bbox_y = 0;
    }
    /* Y軸クリッピング (下端) */
    if (bbox_y + bbox_h > gfx_fb.height) {
        bbox_h = gfx_fb.height - bbox_y;
    }

    /* X軸クリッピング用の定数を事前計算 */
    clip_left = 0;
    clip_right = bbox_w;
    if (bbox_x < 0) {
        clip_left = -bbox_x;
    }
    if (bbox_x + bbox_w > gfx_fb.width) {
        clip_right = gfx_fb.width - bbox_x;
    }
    draw_x0 = (bbox_x < 0) ? 0 : bbox_x;

    /* 4. スキャンラインループ (逆アフィン変換) */
    for (iy = 0; iy < bbox_h; iy++) {
        /* Xクリップ分を加算した初期 u, v */
        u = u_row_start + (i32)clip_left * du_x;
        v = v_row_start + (i32)clip_left * dv_x;

        for (ix = clip_left; ix < clip_right; ix++) {
            sx = (int)(u >> 8);
            sy = (int)(v >> 8);

            /* ソース範囲チェック (範囲外ピクセルは描画しない = クロップ) */
            if ((unsigned)sx < (unsigned)src->w &&
                (unsigned)sy < (unsigned)src->h) {
                color = _read_pixel(src, sx, sy);
                if (color != colorkey) {
                    gfx_pixel_nodirty(draw_x0 + ix - clip_left,
                                       bbox_y + iy, color);
                }
            }
            u += du_x;
            v += dv_x;
        }

        u_row_start += du_y;
        v_row_start += dv_y;
    }

    /* 5. dirty rect 登録 (クリップ済み領域) */
    {
        int reg_w = clip_right - clip_left;
        if (reg_w > 0) {
            gfx_api->gfx_add_dirty_rect(draw_x0, bbox_y, reg_w, bbox_h);
        }
    }
}

/* ======================================================================== */
/*  gfx_blit_rotated — 回転のみのブリット (簡易版)                          */
/*                                                                          */
/*  scale=256 (等倍), colorkey=0 固定で gfx_blit_affine を呼ぶ。           */
/* ======================================================================== */

void gfx_blit_rotated(int dx, int dy,
                       const GFX_Surface *src,
                       int angle)
{
    gfx_blit_affine(dx, dy, src, angle, 256, 0);
}
