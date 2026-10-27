/* PC-9821 normal console grid and GDC cursor; fixed resident backend. */
#include "memmap.h"
#include "types.h"
#include "tvram.h"
#include "io.h"
#include "pc98.h"
#include "console_backend.h"

/* テキストVRAM定義 (tvram.hとpc98.hの定義を使用) */
#define TVRAM_TEXT  TVRAM_BASE
STATIC_ASSERT(TVRAM_ROWS * TVRAM_BPR <= TVRAM_ATTR_BASE - TVRAM_CHAR_BASE,
              console_text_within_plane);
STATIC_ASSERT(TVRAM_ROWS * TVRAM_BPR <= TVRAM_CG_WINDOW - TVRAM_ATTR_BASE,
              console_attr_within_plane);

/* デフォルト属性 (os32_kapi_shared.h で定義済みの場合はスキップ) */
#ifndef ATTR_WHITE
#define ATTR_WHITE   TATTR_WHITE
#endif

void con_backend_clear(void)
{
    volatile u16 *text = (volatile u16 *)P2V_IO(TVRAM_TEXT);
    volatile u8 *attr;
    int i;
    for (i = 0; i < TVRAM_COLS * TVRAM_ROWS; i++) {
        text[i] = TVRAM_SPACE;
        attr = (volatile u8 *)P2V_IO((TVRAM_ATTR + (u32)i * TVRAM_CELL_BYTES));
        *attr = ATTR_WHITE;
    }
}

void con_backend_putchar(int x, int y, char ch, u8 color)
{
    if (x < 0 || x >= TVRAM_COLS || y < 0 || y >= TVRAM_ROWS) return;
    u32 offset = (u32)y * TVRAM_BPR + (u32)x * TVRAM_CELL_BYTES;
    *(volatile u16 *)P2V_IO(TVRAM_TEXT + offset) = (u16)(u8)ch;
    *(volatile u8 *)P2V_IO(TVRAM_ATTR + offset) = color;
}

void con_backend_readchar(int x, int y, u16 *code, u8 *attr)
{
    if (x < 0 || x >= TVRAM_COLS || y < 0 || y >= TVRAM_ROWS) return;
    u32 offset = (u32)y * TVRAM_BPR + (u32)x * TVRAM_CELL_BYTES;
    if (code) *code = *(volatile u16 *)P2V_IO((TVRAM_TEXT + offset));
    if (attr) *attr = *(volatile u8 *)P2V_IO((TVRAM_ATTR + offset));
}

void con_backend_putkanji(int x, int y, u16 jis, u8 color)
{
    if (x < 0 || x >= TVRAM_COLS - 1 || y < 0 || y >= TVRAM_ROWS) return;
    u32 offset = (u32)y * TVRAM_BPR + (u32)x * TVRAM_CELL_BYTES;
    u8 jh = (u8)((jis >> 8) & 0xFF);
    u8 jl = (u8)(jis & 0xFF);
    *(volatile u16 *)P2V_IO(TVRAM_TEXT + offset) = (u16)(jh - TVRAM_JIS_HI_BIAS) | ((u16)jl << 8);
    *(volatile u8 *)P2V_IO(TVRAM_ATTR + offset) = color;
    *(volatile u16 *)P2V_IO(TVRAM_TEXT + offset + TVRAM_CELL_BYTES) = (u16)(jh - TVRAM_JIS_HI_BIAS + TVRAM_KANJI_RIGHT) | ((u16)jl << 8);
    *(volatile u8 *)P2V_IO(TVRAM_ATTR + offset + TVRAM_CELL_BYTES) = color;
}

int con_backend_reverse(int x, int y)
{
    u16 code;
    int width, i, rx;

    if (x < 0 || x >= TVRAM_COLS || y < 0 || y >= TVRAM_ROWS) return 0;

    rx = x;

    /* 漢字判定: 右半分 (bit7セット) なら左半分に揃える */
    code = *(volatile u16 *)P2V_IO((TVRAM_TEXT + (u32)y * TVRAM_BPR + (u32)rx * TVRAM_CELL_BYTES));
    if ((code >> 8) != 0 && (code & TVRAM_KANJI_RIGHT)) {
        if (rx > 0) rx--;
    }

    /* 左半分の文字コードで幅を判定 */
    code = *(volatile u16 *)P2V_IO((TVRAM_TEXT + (u32)y * TVRAM_BPR + (u32)rx * TVRAM_CELL_BYTES));
    if ((code >> 8) != 0 && !(code & TVRAM_KANJI_RIGHT) && rx < TVRAM_COLS - 1) {
        width = TVRAM_WIDE_COLS;  /* 漢字 */
    } else {
        width = 1;  /* ANK */
    }

    /* 反転ビットをトグル */
    for (i = 0; i < width; i++) {
        u32 offset = (u32)y * TVRAM_BPR + (u32)(rx + i) * TVRAM_CELL_BYTES;
        volatile u8 *p = (volatile u8 *)P2V_IO((TVRAM_ATTR + offset));
        *p ^= TATTR_REVERSE;
    }

    return width;
}

void con_backend_scroll(int rows)
{
    volatile u16 *text = (volatile u16 *)P2V_IO(TVRAM_TEXT);
    volatile u16 *attr = (volatile u16 *)P2V_IO(TVRAM_ATTR);
    int i;

    /* 0 .. rows-2 行目までを1行引き上げ */
    for (i = 0; i < TVRAM_COLS * (rows - 1); i++) {
        text[i] = text[i + TVRAM_COLS];
        attr[i] = attr[i + TVRAM_COLS];
    }
    /* rows-1 行目を空白化 */
    for (i = TVRAM_COLS * (rows - 1); i < TVRAM_COLS * rows; i++) {
        text[i] = TVRAM_SPACE;
        attr[i] = ATTR_WHITE;
    }
}

void con_backend_cursor_at(int x, int y)
{
    u16 offset;
    offset = (u16)(y * TVRAM_COLS + x);
    outp(GDC_TEXT_CMD, GDC_CMD_CSRW);
    outp(GDC_TEXT_PARAM, (u8)(offset & 0xFF));
    outp(GDC_TEXT_PARAM, (u8)((offset >> 8) & 0xFF));
}

void con_backend_cursor_shape(int visible)
{
    outp(GDC_TEXT_CMD, GDC_CMD_CSRFORM);
    outp(GDC_TEXT_PARAM, (u8)((visible ? GDC_CSRFORM_DC : 0) | (GDC_TEXT_LINES_PER_ROW - 1)));
    outp(GDC_TEXT_PARAM, (u8)GDC_TEXT_CSR_TOP);
    outp(GDC_TEXT_PARAM, (u8)((GDC_TEXT_CSR_BOTTOM << GDC_TEXT_CSR_BOTTOM_SHIFT) | GDC_TEXT_CSR_BR_HI));
}
