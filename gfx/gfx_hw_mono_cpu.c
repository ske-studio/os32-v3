#include "gfx_hw_mono_cpu.h"
#include "gfx_hal.h"
#include "os32_kapi_shared.h"

#define MONO_TILE_BYTES 4096

int gfx_hw_mono_cpu(int x, int y, int w, int h, const u8 *bits, u32 pitch,
                    u8 fg, int bg, int cx, int cy, int cw, int ch)
{
    u8 tile[MONO_TILE_BYTES];
    long long left = x, top = y, right = (long long)x + w, bottom = (long long)y + h;
    long long clip_right = (long long)cx + cw, clip_bottom = (long long)cy + ch;
    int key = bg < 0 ? (fg ^ 1) : -1;
    u8 zero = bg < 0 ? (u8)key : (u8)bg;

    if (!g_backend || !g_backend->put) return OS32_ERR_NOSYS;
    if (w <= 0 || h <= 0 || cw <= 0 || ch <= 0) return 0;
    if (left < cx) left = cx;
    if (top < cy) top = cy;
    if (right > clip_right) right = clip_right;
    if (bottom > clip_bottom) bottom = clip_bottom;
    if (left >= right || top >= bottom) return 0;

    for (long long tx = left; tx < right; ) {
        int tw = (int)((right - tx > MONO_TILE_BYTES) ? MONO_TILE_BYTES : right - tx);
        int rows = MONO_TILE_BYTES / tw;
        for (long long ty = top; ty < bottom; ) {
            int th = (int)((bottom - ty > rows) ? rows : bottom - ty);
            for (int row = 0; row < th; row++) {
                const u8 *src = bits + (u32)(ty - y + row) * pitch;
                for (int col = 0; col < tw; col++) {
                    u32 bit = (u32)(tx - x + col);
                    tile[row * tw + col] = (src[bit >> 3] & (0x80U >> (bit & 7))) ? fg : zero;
                }
            }
            int rc = g_backend->put((int)tx, (int)ty, tw, th, tile, (u32)tw, key);
            if (rc < 0) return rc;
            ty += th;
        }
        tx += tw;
    }
    return 0;
}
