/* PC-9821 boot splash: fixed PC98 backend and borrowed planar framebuffer. */
#include "platform_splash.h"
#include "gfx.h"
#include "gfx_internal.h"
#include "gfx_hal.h"
#include "kstring.h"

static struct gfx_kernel_fb splash_fb;

/* ブートでは必ず存在する PC98 標準表示だけを使う。任意デバイスの probe /
 * init は後の通常 gfx_init に任せ、GFX= の希望値は描画前に戻す。
 * 起動中の同期呼び出し専用。設定ファイルや公開 ABI は変更しない。 */
int platform_splash_begin(void)
{
    int saved_pref = gfx_get_backend_pref();
    gfx_set_backend_pref(GFX_PREF_PC98);
    gfx_init();
    gfx_set_backend_pref(saved_pref);
    /* gfx_init is void: validate before any drawing, without another probe. */
    if (gfx_kernel_framebuffer(&splash_fb) || splash_fb.format != GFX_BB_PLANAR4 ||
        !splash_fb.planes[0] || !splash_fb.planes[1] ||
        !splash_fb.planes[2] || !splash_fb.planes[3]) {
        gfx_shutdown();
        return -1;
    }
    return 0;
}

/* バックバッファ全面を指定色で塗る — kmemset (rep stosd) で高速化 */
void platform_splash_clear(u8 color)
{
    int p;
    for (p = 0; p < GVRAM_PLANES; p++) {
        u8 val = (color & (1 << p)) ? 0xFF : 0x00;
        kmemset(splash_fb.planes[p], val, GFX_PLANE_SZ);
    }
}

/* バックバッファに矩形を塗る */
void platform_splash_fill_rect(int x, int y, int w, int h, u8 color)
{
    int p, row;
    int x1, x2, byte_start, byte_end;
    u8 mask_first, mask_last;

    /* クリッピング */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GFX_WIDTH) w = GFX_WIDTH - x;
    if (y + h > GFX_HEIGHT) h = GFX_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    x1 = x;
    x2 = x + w - 1;
    byte_start = x1 / BITS_PER_BYTE;
    byte_end   = x2 / BITS_PER_BYTE;
    mask_first = 0xFF >> (x1 % BITS_PER_BYTE);
    mask_last  = 0xFF << (BITS_PER_BYTE - 1 - (x2 % BITS_PER_BYTE));

    for (p = 0; p < GVRAM_PLANES; p++) {
        u8 *plane = splash_fb.planes[p];
        u8 set = (color & (1 << p)) ? 1 : 0;

        for (row = y; row < y + h; row++) {
            u8 *line = plane + row * GFX_BPL;

            if (byte_start == byte_end) {
                u8 mask = mask_first & mask_last;
                if (set)
                    line[byte_start] |= mask;
                else
                    line[byte_start] &= ~mask;
            } else {
                int b;
                if (set)
                    line[byte_start] |= mask_first;
                else
                    line[byte_start] &= ~mask_first;

                for (b = byte_start + 1; b < byte_end; b++)
                    line[b] = set ? 0xFF : 0x00;

                if (set)
                    line[byte_end] |= mask_last;
                else
                    line[byte_end] &= ~mask_last;
            }
        }
    }
}

void platform_splash_clear_pages(void)
{
    gfx_clear_planar_pages(GFX_PLANE_SZ);
}
