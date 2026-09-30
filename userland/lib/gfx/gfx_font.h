/* ======================================================================== */
/*  GFX_FONT.H — グラフィックモード用フォント描画                           */
/*                                                                          */
/*  8x8 ビットマップフォント (ASCII 0x20-0x7E)                              */
/*  バックバッファ上にテキスト描画するAPI                                    */
/* ======================================================================== */

#ifndef __GFX_FONT_H
#define __GFX_FONT_H

#include "os32api.h"
#include "libos32gfx.h"

/* ======== フォント定数 ======== */
#define FONT_W   8     /* フォント幅 (ピクセル) */
#define FONT_H   8     /* フォント高さ (ピクセル) */

/* ======== API ======== */

/* 1文字描画 (バックバッファ上) */
void gfx_putchar(int x, int y, char ch, u8 color);

/* 文字列描画 */
void gfx_puts(int x, int y, const char *str, u8 color);

/* 書式付き文字列描画 (簡易: %d, %u, %x, %s のみ) */
void gfx_printf(int x, int y, u8 color, const char *fmt, ...);

#endif /* __GFX_FONT_H */
