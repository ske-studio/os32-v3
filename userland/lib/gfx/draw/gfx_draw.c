#include "libos32gfx.h"
#include "libgfx_internal.h"
#include "os32api.h"

/* ======================================================================== */
/*  描画プリミティブ                                                        */
/* ======================================================================== */

extern void __cdecl asm_gfx_clear(u8 color, u8 **bb_array);
extern void __cdecl asm_fill_plane_rect(u8 *start, int pitch, int rows, int width_bytes, u8 fill_val);
extern void __cdecl asm_gfx_line(u8 **planes, int pitch, int x0, int y0, int x1, int y1, u8 color);
/* asm_gfx_hline は libgfx_internal.h で宣言済み (gfx_span_raw が使う) */

/* パックド 8bpp の直線 (Bresenham)。dirty rect は呼び出し側が登録済み。 */
static void gfx_line_packed(int x0, int y0, int x1, int y1, u8 color)
{
    int dx, dy, sx, sy, err, e2;

    dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    sx = (x0 < x1) ? 1 : -1;
    sy = (y0 < y1) ? 1 : -1;
    err = dx - dy;

    for (;;) {
        gfx_p_put(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void gfx_clear(u8 color)
{
    if (gfx_packed) {
        /* バックバッファ全面 (pitch × height) を 1 バイト/画素で塗る */
        int rows = gfx_fb.height;
        int pitch = gfx_fb.pitch;
        u32 v32 = (u32)color;
        v32 |= v32 << 8; v32 |= v32 << 16;
        if ((pitch & 3) == 0) {
            _memset_d(gfx_fb.planes[0], v32, (unsigned int)((pitch >> 2) * rows));
        } else {
            u8 *d = gfx_fb.planes[0];
            int n = pitch * rows;
            while (n--) *d++ = color;
        }
        gfx_api->gfx_add_dirty_rect(0, 0, gfx_fb.width, gfx_fb.height);
        return;
    }
    asm_gfx_clear(color, gfx_fb.planes);
    gfx_api->gfx_add_dirty_rect(0, 0, gfx_fb.width, gfx_fb.height);
}

void gfx_clear_rect(int rx, int ry, int rw, int rh, u8 color)
{
    int p;
    int byte_x, byte_w;
    int start_off;

    if (gfx_packed) {
        int r, base;
        if (rx < 0) { rw += rx; rx = 0; }
        if (ry < 0) { rh += ry; ry = 0; }
        if (rx + rw > gfx_fb.width)  rw = gfx_fb.width - rx;
        if (ry + rh > gfx_fb.height) rh = gfx_fb.height - ry;
        if (rw <= 0 || rh <= 0) return;
        base = ry * gfx_fb.pitch;
        for (r = 0; r < rh; r++) {
            gfx_span_raw(base, rx, rx + rw - 1, color);
            base += gfx_fb.pitch;
        }
        gfx_api->gfx_add_dirty_rect(rx, ry, rw, rh);
        return;
    }

    byte_x = rx >> 3;
    byte_w = ((rx + rw + 7) >> 3) - byte_x;
    if (rx < 0) {
        int shift = 0 - rx;
        rw -= shift; rx = 0;
        byte_x = rx >> 3;
        byte_w = ((rx + rw + 7) >> 3) - byte_x;
    }
    if (rx + rw > gfx_fb.width) rw = gfx_fb.width - rx;
    byte_w = ((rx + rw + 7) >> 3) - byte_x;

    if (byte_x < 0) byte_x = 0;
    if (byte_x + byte_w > gfx_fb.pitch) byte_w = gfx_fb.pitch - byte_x;

    if (ry < 0) { rh -= (0 - ry); ry = 0; }
    if (ry + rh > gfx_fb.height) rh = gfx_fb.height - ry;
    if (byte_w <= 0 || rh <= 0) return;

    start_off = ry * gfx_fb.pitch + byte_x;
    for (p = 0; p < 4; p++) {
        u8 fill = (color & (1 << p)) ? 0xFF : 0x00;
        asm_fill_plane_rect(gfx_fb.planes[p] + start_off,
                            gfx_fb.pitch, rh, byte_w, fill);
    }
    gfx_api->gfx_add_dirty_rect(rx, ry, rw, rh);
}

void gfx_pixel(int x, int y, u8 color)
{
    int offset;
    u8 bit;
    int p;

    if (x < 0 || x >= gfx_fb.width || y < 0 || y >= gfx_fb.height) return;

    if (gfx_packed) {
        gfx_fb.planes[0][y * gfx_fb.pitch + x] = color;
        gfx_api->gfx_add_dirty_rect(x, y, 1, 1);
        return;
    }

    offset = y * gfx_fb.pitch + (x >> 3);
    bit = 0x80 >> (x & 7);

    for (p = 0; p < 4; p++) {
        if (color & (1 << p))
            gfx_fb.planes[p][offset] |= bit;
        else
            gfx_fb.planes[p][offset] &= ~bit;
    }
    gfx_api->gfx_add_dirty_rect(x, y, 1, 1);
}

/* dirty rect 登録なしのピクセル描画 (バッチ描画の内部用)。
 * 呼出し元がバウンディングボックスで一括登録する前提。 */
void gfx_pixel_nodirty(int x, int y, u8 color)
{
    int offset;
    u8 bit;
    int p;

    if (x < 0 || x >= gfx_fb.width || y < 0 || y >= gfx_fb.height) return;

    if (gfx_packed) {
        gfx_fb.planes[0][y * gfx_fb.pitch + x] = color;
        return;
    }

    offset = y * gfx_fb.pitch + (x >> 3);
    bit = 0x80 >> (x & 7);

    for (p = 0; p < 4; p++) {
        if (color & (1 << p))
            gfx_fb.planes[p][offset] |= bit;
        else
            gfx_fb.planes[p][offset] &= ~bit;
    }
}

u8 gfx_get_pixel(int x, int y)
{
    int offset;
    u8 bit, color;
    int p;

    if (x < 0 || x >= gfx_fb.width || y < 0 || y >= gfx_fb.height) return 0;

    if (gfx_packed) return gfx_fb.planes[0][y * gfx_fb.pitch + x];

    offset = y * gfx_fb.pitch + (x >> 3);
    bit = 0x80 >> (x & 7);
    color = 0;

    for (p = 0; p < 4; p++) {
        if (gfx_fb.planes[p][offset] & bit)
            color |= (1 << p);
    }
    return color;
}

void gfx_hline(int x, int y, int w, u8 color)
{
    int x2;

    if (y < 0 || y >= gfx_fb.height || w <= 0) return;
    if (x < 0) { w -= (0 - x); x = 0; }
    x2 = x + w - 1;
    if (x2 >= gfx_fb.width) x2 = gfx_fb.width - 1;
    if (x > x2) return;

    /* dirty rect: プレーンは VRAM ワード単位に丸める。パックドは 1 バイト
     * 1 画素で丸め条件が無いので実寸で登録する (転送量が減る)。 */
    if (gfx_packed)
        gfx_api->gfx_add_dirty_rect(x, y, x2 - x + 1, 1);
    else
        gfx_api->gfx_add_dirty_rect(x & ~31, y, ((x2 + 32) & ~31) - (x & ~31), 1);
    gfx_span_raw(y * gfx_fb.pitch, x, x2, color);
}

void gfx_vline(int x, int y, int h, u8 color)
{
    int p, off;
    u8 bit;

    if (x < 0 || x >= gfx_fb.width || h <= 0) return;
    if (y < 0) { h -= (0 - y); y = 0; }
    if (y + h > gfx_fb.height) h = gfx_fb.height - y;
    if (h <= 0) return;

    if (gfx_packed) {
        u8 *d = gfx_fb.planes[0] + y * gfx_fb.pitch + x;
        int r;
        gfx_api->gfx_add_dirty_rect(x, y, 1, h);
        for (r = 0; r < h; r++) {
            *d = color;
            d += gfx_fb.pitch;
        }
        return;
    }

    gfx_api->gfx_add_dirty_rect(x & ~31, y, 32, h);
    off = y * gfx_fb.pitch + (x >> 3);
    bit = 0x80 >> (x & 7);

    for (p = 0; p < 4; p++) {
        int r;
        if (color & (1 << p)) {
            for (r = 0; r < h; r++) {
                gfx_fb.planes[p][off + r * gfx_fb.pitch] |= bit;
            }
        } else {
            u8 inv = ~bit;
            for (r = 0; r < h; r++) {
                gfx_fb.planes[p][off + r * gfx_fb.pitch] &= inv;
            }
        }
    }
}

void gfx_line(int x0, int y0, int x1, int y1, u8 color)
{
    /* dirty_rect をバウンディングボックスで1回だけ登録 */
    int minx, miny, maxx, maxy;
    minx = (x0 < x1) ? x0 : x1;
    maxx = (x0 > x1) ? x0 : x1;
    miny = (y0 < y1) ? y0 : y1;
    maxy = (y0 > y1) ? y0 : y1;

    /* 画面外の場合は dirty_rect のクリップのみ */
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx >= gfx_fb.width) maxx = gfx_fb.width - 1;
    if (maxy >= gfx_fb.height) maxy = gfx_fb.height - 1;
    if (minx <= maxx && miny <= maxy) {
        if (gfx_packed)
            gfx_api->gfx_add_dirty_rect(minx, miny, maxx - minx + 1, maxy - miny + 1);
        else
            gfx_api->gfx_add_dirty_rect(minx & ~31, miny,
                ((maxx + 32) & ~31) - (minx & ~31), maxy - miny + 1);
    }

    if (gfx_packed) {
        gfx_line_packed(x0, y0, x1, y1, color);
        return;
    }
    asm_gfx_line(gfx_fb.planes, gfx_fb.pitch, x0, y0, x1, y1, color);
}

void gfx_rect(int x, int y, int w, int h, u8 color)
{
    /* dirty rect を全体で1つだけ登録してから描画する (4辺個別登録を回避) */
    int x2 = x + w - 1;
    int y2 = y + h - 1;
    int dx, dy2;

    if (w <= 0 || h <= 0) return;

    dx = (x < 0) ? 0 : x;
    dy2 = (y2 >= gfx_fb.height) ? gfx_fb.height - 1 : y2;
    if (x2 >= gfx_fb.width) x2 = gfx_fb.width - 1;
    if (dx <= x2 && y >= 0 && y <= dy2) {
        if (gfx_packed)
            gfx_api->gfx_add_dirty_rect(dx, (y < 0) ? 0 : y,
                x2 - dx + 1, dy2 - ((y < 0) ? 0 : y) + 1);
        else
            gfx_api->gfx_add_dirty_rect(dx & ~31, (y < 0) ? 0 : y,
                ((x2 + 32) & ~31) - (dx & ~31), dy2 - ((y < 0) ? 0 : y) + 1);
    }

    gfx_hline(x, y, w, color);
    gfx_hline(x, y + h - 1, w, color);
    gfx_vline(x, y, h, color);
    gfx_vline(x + w - 1, y, h, color);
}

void gfx_fill_rect(int x, int y, int w, int h, u8 color)
{
    int x2, r, base;

    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    x2 = x + w - 1;
    if (x2 >= gfx_fb.width) x2 = gfx_fb.width - 1;
    if (y + h > gfx_fb.height) h = gfx_fb.height - y;
    if (x > x2 || h <= 0) return;

    if (gfx_packed)
        gfx_api->gfx_add_dirty_rect(x, y, x2 - x + 1, h);
    else
        gfx_api->gfx_add_dirty_rect(x & ~31, y,
            ((x2 + 32) & ~31) - (x & ~31), h);

    /* 行ごとにスパンを塗る (プレーンは asm_gfx_hline、パックドは 1B/画素) */
    base = y * gfx_fb.pitch;
    for (r = 0; r < h; r++) {
        gfx_span_raw(base, x, x2, color);
        base += gfx_fb.pitch;
    }
}

/* ======================================================================== */
/*  塗りつぶし三角形 (スキャンライン方式)                                   */
/*                                                                          */
/*  頂点をY座標でソートし、上半分・下半分に分けて水平線で塗る。             */
/*  dirty rect はバウンディングボックスで1回登録。                          */
/* ======================================================================== */

void gfx_fill_tri(int x0, int y0, int x1, int y1, int x2, int y2, u8 color)
{
    int tmp, y, sa, sb, sx, ex;
    int ax, ay, bx, by, cx, cy;
    int min_x, max_x, min_y, max_y;

    /* 頂点をY座標でソート (y0 <= y1 <= y2) */
    if (y0 > y1) { tmp=x0; x0=x1; x1=tmp; tmp=y0; y0=y1; y1=tmp; }
    if (y1 > y2) { tmp=x1; x1=x2; x2=tmp; tmp=y1; y1=y2; y2=tmp; }
    if (y0 > y1) { tmp=x0; x0=x1; x1=tmp; tmp=y0; y0=y1; y1=tmp; }

    min_y = y0;
    max_y = y2;

    if (min_y == max_y) {
        /* 縮退三角形 (水平線) */
        min_x = x0; if (x1 < min_x) min_x = x1; if (x2 < min_x) min_x = x2;
        max_x = x0; if (x1 > max_x) max_x = x1; if (x2 > max_x) max_x = x2;
        gfx_hline(min_x, min_y, max_x - min_x + 1, color);
        return;
    }

    /* dirty rect: バウンディングボックスで1回登録 */
    min_x = x0; if (x1 < min_x) min_x = x1; if (x2 < min_x) min_x = x2;
    max_x = x0; if (x1 > max_x) max_x = x1; if (x2 > max_x) max_x = x2;
    gfx_api->gfx_add_dirty_rect(min_x, min_y,
                                 max_x - min_x + 1, max_y - min_y + 1);

    ax = x0; ay = y0;
    bx = x1; by = y1;
    cx = x2; cy = y2;

    for (y = ay; y <= cy; y++) {
        /* エッジ a→c の x座標 (常に使用) */
        sa = ax + (cx - ax) * (y - ay) / (cy - ay);

        if (y < by) {
            /* 上半分: エッジ a→b */
            if (by == ay) {
                sb = bx;
            } else {
                sb = ax + (bx - ax) * (y - ay) / (by - ay);
            }
        } else {
            /* 下半分: エッジ b→c */
            if (cy == by) {
                sb = bx;
            } else {
                sb = bx + (cx - bx) * (y - by) / (cy - by);
            }
        }

        sx = (sa < sb) ? sa : sb;
        ex = (sa > sb) ? sa : sb;
        if (ex >= sx) {
            gfx_hline(sx, y, ex - sx + 1, color);
        }
    }
}

/* ======================================================================== */
/*  フォントラスタライザ (単色・透過)                                       */
/* ======================================================================== */
extern void __cdecl asm_kcg_draw_font(int x, int y, const u8 *pat, int w_bytes, int h_lines, u8 fg, u8 **bb_array);

void gfx_draw_font(int x, int y, const u8 *pat, int w_bytes, int h_lines, u8 fg)
{
    /* パックド 8bpp: 1 バイト 1 画素で直接置く。アライメント制約は無い。
     * dirty rect はグリフ全体で 1 回だけ登録する (gfx_pixel を画素ごとに
     * 呼ぶと登録回数が h*w になり present が細切れになる)。 */
    if (gfx_packed) {
        int row, col, b;
        for (row = 0; row < h_lines; row++) {
            int py = y + row;
            if (py < 0 || py >= gfx_fb.height) continue;
            for (b = 0; b < w_bytes; b++) {
                u8 bits = pat[row * w_bytes + b];
                if (!bits) continue;
                for (col = 0; col < 8; col++) {
                    if (bits & (0x80 >> col)) {
                        int px = x + b * 8 + col;
                        if (px < 0 || px >= gfx_fb.width) continue;
                        gfx_fb.planes[0][py * gfx_fb.pitch + px] = fg;
                    }
                }
            }
        }
        gfx_api->gfx_add_dirty_rect(x, y, w_bytes * 8, h_lines);
        return;
    }

    /* asm_kcg_draw_font はX座標の8ドットアライメント(バイト境界)を前提としている */
    if ((x & 7) == 0) {
        asm_kcg_draw_font(x, y, pat, w_bytes, h_lines, fg, gfx_fb.planes);
        gfx_api->gfx_add_dirty_rect(x, y, w_bytes * 8, h_lines);
    } else {
        /* 非アライメント時のフォールバック (基本ここでは呼ばれないはず) */
        int row, col, b;
        for (row = 0; row < h_lines; row++) {
            for (b = 0; b < w_bytes; b++) {
                u8 bits = pat[row * w_bytes + b];
                for (col = 0; col < 8; col++) {
                    if (bits & (0x80 >> col)) {
                        gfx_pixel(x + b * 8 + col, y + row, fg);
                    }
                }
            }
        }
    }
}
