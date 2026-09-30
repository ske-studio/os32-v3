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

#include "types.h"

/* ====================================================================== */
/*  ボタンビットマスク                                                      */
/* ====================================================================== */
#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MIDDLE 0x04

/* ====================================================================== */
/*  ドライバモード                                                          */
/* ====================================================================== */
#define MOUSE_MODE_NONE      0   /* 未初期化 */
#define MOUSE_MODE_BUS       1   /* バスマウス (IRQ13) */
#define MOUSE_MODE_SEAMLESS  2   /* シームレス (NP21/W getmpos) */

/* ====================================================================== */
/*  マウス状態構造体                                                        */
/* ====================================================================== */
typedef struct {
    i16  x;          /* 現在のX座標 (画面座標) */
    i16  y;          /* 現在のY座標 (画面座標) */
    i16  dx;         /* 前回pollからのX差分 */
    i16  dy;         /* 前回pollからのY差分 */
    u8   buttons;    /* ボタン状態ビットマスク (MOUSE_BTN_xxx) */
    u8   mode;       /* 動作モード (MOUSE_MODE_xxx) */
} MouseState;

/* ====================================================================== */
/*  公開API                                                                */
/* ====================================================================== */

/* 初期化: NP21/W検出→モード自動選択 */
void mouse_init(void);

/* 状態取得 (ポーリング) — 差分はpoll間でリセットされる */
void mouse_poll(MouseState *state);

/* ボタン状態のみ取得 */
u8   mouse_buttons(void);

/* カーソル移動範囲設定 */
void mouse_set_bounds(i16 x_min, i16 y_min, i16 x_max, i16 y_max);

/* 割り込み周期設定 (バスマウスモードのみ) */
/* rate: 0=120Hz, 1=60Hz, 2=30Hz, 3=15Hz */
void mouse_set_rate(u8 rate);

/* マウスが使用可能か */
int  mouse_available(void);

/* 現在のモード取得 */
u8   mouse_get_mode(void);

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
