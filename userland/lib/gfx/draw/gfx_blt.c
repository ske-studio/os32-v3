#include "libos32gfx.h"
#include "libgfx_internal.h"

extern void __cdecl asm_copy_plane_rect(u8 *dst, int dst_pitch,
                                        const u8 *src, int src_pitch,
                                        int rows, int width_bytes);

/* ======================================================================== */
/*  バッキングストア・キャッシュ操作                                         */
/*                                                                          */
/*  パックド 8bpp (PEGC) でも **バッファの形式は 4 プレーンのまま** にする    */
/*  (票 H2b)。呼び出し側 (スプライトの bg_buf など) が確保済みのバッファ長は  */
/*  4 * orig_wb * h バイトで決め打たれており、1 バイト/画素で書くと 2 倍に    */
/*  溢れるため。色は 0〜15 の 4 ビットだけを保存する — PEGC 側でシステム色に  */
/*  同じ番号を割ってあるので、GUI が使う範囲では往復で値が変わらない。        */
/*  16 以上の色を退避したい呼び出し元は自前でバッファを持つこと。            */
/* ======================================================================== */

/* パックド画面 → 4 プレーン形式バッファ (save)、およびその逆 (restore)。
 * バッファ内の並びはプレーン経路とまったく同じ:
 *   plane i, row r, byte b  =  dst_base[i*orig_wb*h + r*orig_wb + b]
 * 対応する画面座標は x = (sx + b) * 8 + k (k = 0..7, MSB が左)。 */
static void gfx_packed_save_planar(int sx, int y, int wb, int h,
                                   u8 *dst_base, int orig_wb)
{
    int i, r, b, k;
    for (i = 0; i < 4; i++) {
        u8 *dst_plane = dst_base + (i * orig_wb * h);
        for (r = 0; r < h; r++) {
            int py = y + r;
            for (b = 0; b < wb; b++) {
                u8 v = 0;
                for (k = 0; k < 8; k++) {
                    int px = (sx + b) * 8 + k;
                    if (px < 0 || px >= gfx_fb.width) continue;
                    if (gfx_fb.planes[0][py * gfx_fb.pitch + px] & (1 << i))
                        v |= (u8)(0x80 >> k);
                }
                dst_plane[r * orig_wb + b] = v;
            }
        }
    }
}

static void gfx_packed_restore_planar(int sx, int y, int wb, int h,
                                      const u8 *src_base, int orig_wb)
{
    int i, r, b, k;
    for (r = 0; r < h; r++) {
        int py = y + r;
        for (b = 0; b < wb; b++) {
            for (k = 0; k < 8; k++) {
                int px = (sx + b) * 8 + k;
                u8 bit = (u8)(0x80 >> k);
                u8 color = 0;
                if (px < 0 || px >= gfx_fb.width) continue;
                for (i = 0; i < 4; i++) {
                    const u8 *src_plane = src_base + (i * orig_wb * h);
                    if (src_plane[r * orig_wb + b] & bit) color |= (u8)(1 << i);
                }
                gfx_fb.planes[0][py * gfx_fb.pitch + px] = color;
            }
        }
    }
}

void __cdecl gfx_save_rect(int x, int y, int w, int h, void *buf)
{
    int i;
    int sx, ex, wb;
    u8 *dst_base = (u8 *)buf;

    int orig_wb = ((x + w + 7) / 8) - (x / 8);
    if (orig_wb <= 0 || h <= 0) return;

    /* ビューポートでクリップ */
    if (x < 0) { w -= (0 - x); x = 0; }
    if (y < 0) { h -= (0 - y); y = 0; }
    if (x + w > gfx_fb.width) w = gfx_fb.width - x;
    if (y + h > gfx_fb.height) h = gfx_fb.height - y;
    if (w <= 0 || h <= 0) return;

    sx = x / 8;
    ex = (x + w + 7) / 8;
    wb = ex - sx;

    if (gfx_packed) {
        gfx_packed_save_planar(sx, y, wb, h, dst_base, orig_wb);
        return;
    }

    for (i = 0; i < 4; i++) {
        u8 *dst_plane = dst_base + (i * orig_wb * h);
        const u8 *src_start = gfx_fb.planes[i] + y * gfx_fb.pitch + sx;
        asm_copy_plane_rect(dst_plane, orig_wb,
                            src_start, gfx_fb.pitch,
                            h, wb);
    }
}

void __cdecl gfx_restore_rect(int x, int y, int w, int h, const void *buf)
{
    int i;
    int sx, ex, wb;
    const u8 *src_base = (const u8 *)buf;

    int orig_wb = ((x + w + 7) / 8) - (x / 8);
    if (orig_wb <= 0 || h <= 0) return;

    if (x < 0) { w -= (0 - x); x = 0; }
    if (y < 0) { h -= (0 - y); y = 0; }
    if (x + w > gfx_fb.width) w = gfx_fb.width - x;
    if (y + h > gfx_fb.height) h = gfx_fb.height - y;
    if (w <= 0 || h <= 0) return;

    sx = x / 8;
    ex = (x + w + 7) / 8;
    wb = ex - sx;

    if (gfx_packed) {
        gfx_packed_restore_planar(sx, y, wb, h, src_base, orig_wb);
        return;
    }

    for (i = 0; i < 4; i++) {
        const u8 *src_plane = src_base + (i * orig_wb * h);
        u8 *dst_start = gfx_fb.planes[i] + y * gfx_fb.pitch + sx;
        asm_copy_plane_rect(dst_start, gfx_fb.pitch,
                            src_plane, orig_wb,
                            h, wb);
    }
}
