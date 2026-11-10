/* ======================================================================== */
/*  TVRAM.H — テキストVRAM操作関数宣言                                      */
/* ======================================================================== */

#ifndef __TVRAM_H
#define __TVRAM_H

#include "types.h"

/* 既存の KAPI・import の利用者のための互換の入口 (宣言だけ)。生の JIS・属性の
 * 値の契約は変えない。番地と寸法は PC-98 の実装 (platform/pc98/tvram_private.h・
 * pc98.h) が持ち、格子の寸法は console_backend.h の con_backend_geometry()。 */

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
