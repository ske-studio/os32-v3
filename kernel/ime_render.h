/* ======================================================================== */
/*  IME_RENDER.H — FEP 描画バックエンド抽象化インターフェース               */
/* ======================================================================== */

#ifndef __IME_RENDER_H
#define __IME_RENDER_H

#include "types.h"

/* 描画バックエンド構造体 */
typedef struct {
    /* 1セル ANK 文字。x, y はセル座標 (80x25 グリッド) */
    void (*putc)(int x, int y, char ank, u8 color);

    /* 全角1文字 (Unicode コードポイント)。戻り値=消費セル幅 (1 または 2) */
    int  (*putw)(int x, int y, u32 codepoint, u8 color);

    /* 指定行を空白で消去 */
    void (*clear_row)(int y, u8 color);

    /* バックエンド固有: 描画開始/終了フック */
    void (*begin)(void);
    void (*end)(void);
} IME_Render;

/* TVRAM用描画バックエンド実体 */
extern const IME_Render g_ime_render_tvram;

#endif /* __IME_RENDER_H */
