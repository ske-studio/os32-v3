/* ======================================================================== */
/*  IME_RENDER_TVRAM.C — TVRAM用 IME 描画バックエンド実装                   */
/* ======================================================================== */

#include "ime_render.h"
#include "tvram.h"
#include "utf8.h"

/* 空関数 */
static void tvram_render_begin(void)
{
}

static void tvram_render_end(void)
{
}

static void tvram_render_putc(int x, int y, char ank, u8 color)
{
    if (x >= 0 && x < TVRAM_COLS && y >= 0 && y < TVRAM_ROWS) {
        tvram_putchar_at(x, y, ank, color);
    }
}

static int tvram_render_putw(int x, int y, u32 codepoint, u8 color)
{
    u8 ank;
    u16 jis;

    if (x < 0 || x >= TVRAM_COLS || y < 0 || y >= TVRAM_ROWS) {
        return 0;
    }

    ank = unicode_to_ank(codepoint);
    if (ank) {
        tvram_putchar_at(x, y, (char)ank, color);
        return 1;
    }

    jis = unicode_to_jis(codepoint);
    if (jis) {
        if (x >= TVRAM_COLS - 1) {
            /* 画面端で2セル描けない場合は描画しない */
            return 0;
        }
        tvram_putkanji_at(x, y, jis, color);
        return 2;
    }

    /* 変換不可: □ (JIS 0x2222) を表示 */
    if (x < TVRAM_COLS - 1) {
        tvram_putkanji_at(x, y, 0x2222, color);
        return 2;
    }

    return 0;
}

static void tvram_render_clear_row(int y, u8 color)
{
    int x;
    if (y >= 0 && y < TVRAM_ROWS) {
        for (x = 0; x < TVRAM_COLS; x++) {
            tvram_putchar_at(x, y, ' ', color);
        }
    }
}

/* TVRAM描画バックエンドの実体 */
const IME_Render g_ime_render_tvram = {
    tvram_render_putc,
    tvram_render_putw,
    tvram_render_clear_row,
    tvram_render_begin,
    tvram_render_end
};
