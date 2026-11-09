#include "gfx_internal.h"
#include "display_restore.h"

void gfx_vsync_wait(void)
{
    while ((_in(GDC_GFX_STAT) & GDC_STAT_VSYNC) != 0);
    while ((_in(GDC_GFX_STAT) & GDC_STAT_VSYNC) == 0);
}

/* y is a normalized logical VRAM row; only this provider encodes GDC PRAM. */
void display_scroll(int y, int height, int flip, int page)
{
    int sad1, sl1, sad2, sl2;
    int line_mul;
    int sad_base;
    u8 params[GDC_PRAM_BYTES];
    int i;

    /*
     * GDC SCROLL の SL は「物理表示ライン数」で指定する。
     * 200ラインモード (CSRFORM L/R=1) では各VRAMラインが2倍表示されるため、
     * VRAM行数に倍率 (400/height) を掛ける。
     */
    line_mul = GFX_HEIGHT / height;  /* 1(400line) or 2(200line) */

    /* ページフリッピング時: 表示ページのVRAMオフセットを加算
     * 1ページ分 = height * GFX_WPL ワード
     * 200line: 200*80/2 = 8000w, 400line: 400*80/2 = 16000w */
    sad_base = 0;
    if (flip) {
        sad_base = page * (height * GFX_WPL);
    }

    sad1 = sad_base + y * GFX_WPL;
    sl1  = (height - y) * line_mul;
    sad2 = sad_base;
    sl2  = y * line_mul;

    params[0] = sad1 & 0xFF;
    params[1] = (sad1 >> 8) & 0xFF;
    params[2] = (sl1 & GDC_SCROLL_LINES_LO_MASK) << GDC_SCROLL_LINES_SHIFT;
    params[3] = (sl1 >> GDC_SCROLL_LINES_SHIFT) & GDC_SCROLL_LINES_HI_MASK;

    params[GDC_SCROLL_PART_BYTES] = sad2 & 0xFF;
    params[GDC_SCROLL_PART_BYTES + 1] = (sad2 >> 8) & 0xFF;
    params[GDC_SCROLL_PART_BYTES + 2] = (sl2 & GDC_SCROLL_LINES_LO_MASK) << GDC_SCROLL_LINES_SHIFT;
    params[GDC_SCROLL_PART_BYTES + 3] = (sl2 >> GDC_SCROLL_LINES_SHIFT) & GDC_SCROLL_LINES_HI_MASK;

    _out(GDC_GFX_CMD, GDC_CMD_SCROLL);
    gfx_vsync_wait();
    for (i = 0; i < GDC_SCROLL_PART_BYTES * GDC_SCROLL_PARTS; i++) {
        _out(GDC_GFX_PARAM, params[i]);
    }
}
