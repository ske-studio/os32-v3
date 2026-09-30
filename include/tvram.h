/* ======================================================================== */
/*  TVRAM.H — テキストVRAM操作関数宣言                                      */
/* ======================================================================== */

#ifndef __TVRAM_H
#define __TVRAM_H

#include "types.h"

/* テキストVRAM定数 (PC-98) */
#define TVRAM_COLS    80
#define TVRAM_ROWS    25
#define TVRAM_BASE    0xA0000UL
#define TVRAM_ATTR    0xA2000UL
#ifndef TVRAM_BPR
#define TVRAM_BPR     (TVRAM_COLS * 2)  /* バイト/行 (160) */
#endif

/* 画面クリア */
void tvram_clear(void);

/* 1文字表示 */
void tvram_putchar_at(int x, int y, char ch, u8 color);

/* 1セル読み取り (文字コード + 属性) */
void tvram_readchar_at(int x, int y, u16 *code, u8 *attr);

/* 反転トグル (漢字対応, 戻り値=セル幅) */
int tvram_reverse_cell(int x, int y);

/* 漢字表示 (JISコード) */
void tvram_putkanji_at(int x, int y, u16 jis, u8 color);

/* 1行スクロール */
void tvram_scroll(void);

/* スクロール保護設定 */
void tvram_set_scroll_reserve(int rows);

#endif /* __TVRAM_H */
