#include "gfx_internal.h"

int vram_scroll_y = 0;

void gfx_vsync_wait(void)
{
    while ((_in(GDC_GFX_STAT) & GDC_STAT_VSYNC) != 0);
    while ((_in(GDC_GFX_STAT) & GDC_STAT_VSYNC) == 0);
}

void gfx_scroll_init(void)
{
    vram_scroll_y = 0;
    gfx_hardware_scroll(0);
}

void gfx_hardware_scroll(int lines)
{
    if (!bb_b) return;
    int sad1, sl1, sad2, sl2;
    int line_mul;
    int sad_base;
    u8 params[GDC_PRAM_BYTES];
    int i;

    vram_scroll_y += lines % gfx_current_height;
    if (vram_scroll_y < 0) vram_scroll_y += gfx_current_height;
    vram_scroll_y %= gfx_current_height;

    /*
     * GDC SCROLL の SL は「物理表示ライン数」で指定する。
     * 200ラインモード (CSRFORM L/R=1) では各VRAMラインが2倍表示されるため、
     * VRAM行数に倍率 (400/gfx_current_height) を掛ける。
     */
    line_mul = GFX_HEIGHT / gfx_current_height;  /* 1(400line) or 2(200line) */

    /* ページフリッピング時: 表示ページのVRAMオフセットを加算
     * 1ページ分 = gfx_current_height * GFX_WPL ワード
     * 200line: 200*80/2 = 8000w, 400line: 400*80/2 = 16000w */
    sad_base = 0;
    if (gfx_flip_enabled) {
        sad_base = gfx_display_page * (gfx_current_height * GFX_WPL);
    }

    sad1 = sad_base + vram_scroll_y * GFX_WPL;
    sl1  = (gfx_current_height - vram_scroll_y) * line_mul;
    sad2 = sad_base;
    sl2  = vram_scroll_y * line_mul;

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
