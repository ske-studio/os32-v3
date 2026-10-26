/* Cooperative pointer interface. Register a complete resident table at boot,
 * before IRQ publication; no runtime replacement or ownership transfer.
 * Coordinates, bounds, poll deltas and cursor state belong to the provider. */
#ifndef OS32_POINTER_SERVICE_H
#define OS32_POINTER_SERVICE_H

#include "types.h"
#include "os32_kapi_shared.h" /* Public MOUSE_CURSOR_* values. */

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

typedef struct PointerOps {
    void (*poll)(MouseState *state);
    int (*available)(void);
    void (*bounds)(i16 x_min, i16 y_min, i16 x_max, i16 y_max);
    void (*cursor_mode)(int mode);
    void (*cursor_show)(void);
    void (*cursor_hide)(void);
} PointerOps;

void pointer_register(const PointerOps *ops);
void pointer_poll(MouseState *state);
int pointer_available(void);
void pointer_set_bounds(i16 x_min, i16 y_min, i16 x_max, i16 y_max);
void pointer_cursor_set_mode(int mode);
void pointer_cursor_show(void);
void pointer_cursor_hide(void);

#endif /* OS32_POINTER_SERVICE_H */
