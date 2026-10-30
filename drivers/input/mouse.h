/* ======================================================================== */
/*  MOUSE.H — マウスドライバ 公開API                                        */
/*                                                                          */
/*  2モード対応:                                                            */
/*    - バスマウス (実機互換, IRQ13 + 8255 PPI)                              */
/*    - シームレスマウス (NP21/W専用, getmpos コマンド)                      */
/*                                                                          */
/*  出典: PC9800Bible §2-11, UNDOCUMENTED io_mouse.md,                      */
/*        NP21/W io/mouseif.c, np2tool/npmouse/                             */
/* ======================================================================== */
#ifndef MOUSE_H
#define MOUSE_H

#include "../../include/pointer_service.h"

#define MOUSE_ABS_MAX 32767L

/* ====================================================================== */
/*  公開API                                                                */
/* ====================================================================== */

/* 初期化: NP21/W検出→モード自動選択 */
void mouse_init(void);

/* 状態取得 (ポーリング) — 差分はpoll間でリセットされる */
void mouse_poll(MouseState *state);

/* カーソル移動範囲設定 */
void mouse_set_bounds(i16 x_min, i16 y_min, i16 x_max, i16 y_max);

/* マウスが使用可能か */
int  mouse_available(void);

/* カーソル表示レイヤー */
void mouse_cursor_set_mode(int mode);  /* MOUSE_CURSOR_xxx */
void mouse_cursor_show(void);
void mouse_cursor_hide(void);

/* ====================================================================== */
/*  カーネル内部用 (mouse_bus.c / mouse_seamless.c)                        */
/* ====================================================================== */

/* バスマウス: IRQ13割り込みハンドラ (isr_stub.asmから呼ばれる) */
void mouse_irq_handler(void);

/* バスマウス: 初期化 */
int  bus_mouse_init(void);

/* シームレスマウス: 初期化 */
int  seamless_mouse_init(void);

/* シームレスマウス: 絶対座標取得 (0..32767 の正規化値。画面座標への換算は
 * mouse.c が現在の移動範囲で行う) */
int  seamless_mouse_poll(i16 *x, i16 *y);

/* ボタン状態を8255ポートAから直接読取 (両モード共用) */
u8   mouse_read_buttons(void);

#endif /* MOUSE_H */
