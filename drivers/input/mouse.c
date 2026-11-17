/* ======================================================================== */
/*  MOUSE.C — マウスドライバ統合モジュール                                  */
/*                                                                          */
/*  起動時にNP21/Wを検出し、シームレス/バスマウスを自動選択する。           */
/*  外部からは mouse_poll() で統一的にアクセスできる。                      */
/* ======================================================================== */

#include "irq_line.h"
#include "mouse.h"
#include "../../input/hal/in2_int.h"
#include "kbd.h"
#include "kcg_contract.h"
#include "../char/np2sysp.h"
#include "pc98.h"
#include "io.h"
#include "kprintf.h"

/* ====================================================================== */
/*  内部状態                                                               */
/* ====================================================================== */
static u8  mouse_mode_current = MOUSE_MODE_NONE;

/* カーソル位置 (画面座標、両モードで使用) */
static i16 cursor_x;
static i16 cursor_y;

/* ====================================================================== */
/*  クランプ関数                                                           */
/* ====================================================================== */
static i16 clamp16(i16 val, i16 lo, i16 hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

/* ====================================================================== */
/*  mouse_init — マウスドライバ初期化                                      */
/* ====================================================================== */
void mouse_init(void)
{
    pointer_position(&cursor_x, &cursor_y);
    mouse_ext_mode(0);

    /* NP21/W検出 → シームレスモード試行 */
    if (np2_detect()) {
        if (seamless_mouse_init()) {
            mouse_hal_seamless_init();
            mouse_ext_mode(1);
            mouse_mode_current = MOUSE_MODE_SEAMLESS;
            kprintf(KLOG_LEGACY_GREEN, "[mouse] seamless mode (NP21/W)\n");
            return;
        }
    }

    /* 実機 or フォールバック → バスマウスモード */
    if (bus_mouse_init()) {
        irq_enable(PC98_IRQ_MOUSE);  /* IRQ13 (スレーブ IR5) をPICで許可 */
        mouse_mode_current = MOUSE_MODE_BUS;
        kprintf(KLOG_LEGACY_GREEN, "[mouse] bus mouse mode (IRQ13)\n");
        return;
    }

    mouse_mode_current = MOUSE_MODE_NONE;
    kprintf(KLOG_LEGACY_RED, "[mouse] no mouse available\n");
}

/* ====================================================================== */
/*  mouse_poll — マウス状態取得                                            */
/* ====================================================================== */
void mouse_poll(MouseState *state)
{
    i16 dx = 0, dy = 0;
    u8 buttons = 0;

    if (mouse_mode_current == MOUSE_MODE_SEAMLESS) mouse_hal_seamless_poll();
    if (mouse_mode_current != MOUSE_MODE_NONE) {
        mouse_legacy_snapshot(&dx, &dy, &buttons, &cursor_x, &cursor_y);
    }

    kbd_stop_mouse_sample(buttons);

    if (state) {
        state->x = cursor_x;
        state->y = cursor_y;
        state->dx = dx;
        state->dy = dy;
        state->buttons = buttons;
        state->mode = mouse_mode_current;
    }
}

/* ====================================================================== */
/*  mouse_set_bounds — カーソル移動範囲設定                                */
/* ====================================================================== */
void mouse_set_bounds(i16 x_min, i16 y_min, i16 x_max, i16 y_max)
{
    pointer_bounds(x_min, y_min, x_max, y_max);
    /* Keep the legacy displayed position in sync until the next poll. */
    cursor_x = clamp16(cursor_x, x_min, x_max);
    cursor_y = clamp16(cursor_y, y_min, y_max);
}

/* ====================================================================== */
/*  mouse_available — マウスが使用可能か                                   */
/* ====================================================================== */
int mouse_available(void)
{
    return (mouse_mode_current != MOUSE_MODE_NONE);
}

/* ====================================================================== */
/*  カーソル表示レイヤー                                                   */
/* ====================================================================== */
static int  cursor_display_mode = MOUSE_CURSOR_NONE;  /* MOUSE_CURSOR_NONE */
static int  cursor_visible = 0;
static int  cursor_prev_cx = -1;
static int  cursor_prev_cy = -1;
static int  cursor_prev_width = 1;

/* 外部参照: console.c の tvram_reverse_cell */
extern int tvram_reverse_cell(int x, int y);

void mouse_cursor_set_mode(int mode)
{
    /* 現在表示中なら消去 */
    if (cursor_visible) {
        mouse_cursor_hide();
    }
    cursor_display_mode = mode;
    cursor_prev_cx = -1;
    cursor_prev_cy = -1;
    cursor_prev_width = 1;
}

void mouse_cursor_show(void)
{
    int cx, cy;

    if (cursor_display_mode == MOUSE_CURSOR_NONE) return;  /* NONE */

    cx = cursor_x / KCG_ANK_W;
    cy = cursor_y / KCG_ANK_H;
    if (cx > TVRAM_COLS - 1) cx = TVRAM_COLS - 1;
    if (cy > TVRAM_ROWS - 1) cy = TVRAM_ROWS - 1;

    if (cursor_display_mode == MOUSE_CURSOR_TEXT) {
        /* TEXT: TVRAM属性反転 */
        if (cursor_visible && cursor_prev_cx >= 0) {
            /* 前位置と同じなら何もしない */
            if (cx == cursor_prev_cx && cy == cursor_prev_cy) return;
            /* 前位置を反転解除 */
            tvram_reverse_cell(cursor_prev_cx, cursor_prev_cy);
        }
        cursor_prev_width = tvram_reverse_cell(cx, cy);
        cursor_prev_cx = cx;
        cursor_prev_cy = cy;
    } else if (cursor_display_mode == MOUSE_CURSOR_GFX) {
        /* GFX: スタブ (将来実装) */
    }

    cursor_visible = 1;
}

void mouse_cursor_hide(void)
{
    if (!cursor_visible) return;

    if (cursor_display_mode == MOUSE_CURSOR_TEXT && cursor_prev_cx >= 0) {
        /* TEXT: 反転解除 */
        tvram_reverse_cell(cursor_prev_cx, cursor_prev_cy);
        cursor_prev_cx = -1;
        cursor_prev_cy = -1;
    } else if (cursor_display_mode == MOUSE_CURSOR_GFX) {
        /* GFX: スタブ (将来実装) */
    }

    cursor_visible = 0;
}
