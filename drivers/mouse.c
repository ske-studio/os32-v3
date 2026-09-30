/* ======================================================================== */
/*  MOUSE.C — マウスドライバ統合モジュール                                  */
/*                                                                          */
/*  起動時にNP21/Wを検出し、シームレス/バスマウスを自動選択する。           */
/*  外部からは mouse_poll() で統一的にアクセスできる。                      */
/* ======================================================================== */

#include "mouse.h"
#include "np2sysp.h"
#include "pc98.h"
#include "io.h"
#include "idt.h"
#include "kprintf.h"

/* ====================================================================== */
/*  内部状態                                                               */
/* ====================================================================== */
static u8  mouse_mode_current = MOUSE_MODE_NONE;

/* カーソル位置 (画面座標、両モードで使用) */
static i16 cursor_x;
static i16 cursor_y;

/* クリッピング領域 */
static i16 bound_x_min = 0;
static i16 bound_y_min = 0;
static i16 bound_x_max = 639;
static i16 bound_y_max = 399;

/* シームレスモード: 前回の絶対座標 (差分計算用) */
static i16 prev_abs_x;
static i16 prev_abs_y;
static int prev_abs_valid;

/* bus_mouse_get (mouse_bus.c) */
extern void bus_mouse_get(i16 *dx, i16 *dy, u8 *buttons);

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
    cursor_x = bound_x_max / 2;
    cursor_y = bound_y_max / 2;
    prev_abs_valid = 0;

    /* NP21/W検出 → シームレスモード試行 */
    if (np2_detect()) {
        if (seamless_mouse_init()) {
            mouse_mode_current = MOUSE_MODE_SEAMLESS;
            kprintf(0x0A, "[mouse] seamless mode (NP21/W)\n");
            return;
        }
    }

    /* 実機 or フォールバック → バスマウスモード */
    if (bus_mouse_init()) {
        irq_enable(13);  /* IRQ13 (スレーブ IR5) をPICで許可 */
        mouse_mode_current = MOUSE_MODE_BUS;
        kprintf(0x0A, "[mouse] bus mouse mode (IRQ13)\n");
        return;
    }

    mouse_mode_current = MOUSE_MODE_NONE;
    kprintf(0x04, "[mouse] no mouse available\n");
}

/* ====================================================================== */
/*  mouse_poll — マウス状態取得                                            */
/* ====================================================================== */
void mouse_poll(MouseState *state)
{
    i16 dx = 0, dy = 0;
    u8 buttons = 0;

    if (mouse_mode_current == MOUSE_MODE_BUS) {
        /* バスマウス: 累積差分を取得 */
        bus_mouse_get(&dx, &dy, &buttons);

        cursor_x += dx;
        cursor_y += dy;
        cursor_x = clamp16(cursor_x, bound_x_min, bound_x_max);
        cursor_y = clamp16(cursor_y, bound_y_min, bound_y_max);

    } else if (mouse_mode_current == MOUSE_MODE_SEAMLESS) {
        i16 abs_x, abs_y;
        /* シームレス: 絶対座標取得。seamless_mouse_poll は 0..32767 の正規化値
         * (NP21/W の 0..65535 を半分に畳んだもの) を返すので、現在の移動範囲に
         * 比例配分する。決め打ちの 639/399 だと 480 ラインで下端に届かない。 */
        if (seamless_mouse_poll(&abs_x, &abs_y)) {
            abs_x = (i16)(bound_x_min +
                          (long)abs_x * (long)(bound_x_max - bound_x_min) / 32767L);
            abs_y = (i16)(bound_y_min +
                          (long)abs_y * (long)(bound_y_max - bound_y_min) / 32767L);
            if (prev_abs_valid) {
                dx = abs_x - prev_abs_x;
                dy = abs_y - prev_abs_y;
            }
            prev_abs_x = abs_x;
            prev_abs_y = abs_y;
            prev_abs_valid = 1;

            cursor_x = clamp16(abs_x, bound_x_min, bound_x_max);
            cursor_y = clamp16(abs_y, bound_y_min, bound_y_max);
        }
        /* ボタンは8255ポートAから直接読取 (両モード共用) */
        buttons = mouse_read_buttons();
    }

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
/*  mouse_buttons — ボタン状態のみ取得                                     */
/* ====================================================================== */
u8 mouse_buttons(void)
{
    return mouse_read_buttons();
}

/* ====================================================================== */
/*  mouse_set_bounds — カーソル移動範囲設定                                */
/* ====================================================================== */
void mouse_set_bounds(i16 x_min, i16 y_min, i16 x_max, i16 y_max)
{
    bound_x_min = x_min;
    bound_y_min = y_min;
    bound_x_max = x_max;
    bound_y_max = y_max;

    /* 現在位置をクランプ */
    cursor_x = clamp16(cursor_x, bound_x_min, bound_x_max);
    cursor_y = clamp16(cursor_y, bound_y_min, bound_y_max);
}

/* ====================================================================== */
/*  mouse_set_rate — 割り込み周期設定 (バスマウスのみ)                     */
/* ====================================================================== */
void mouse_set_rate(u8 rate)
{
    if (mouse_mode_current == MOUSE_MODE_BUS) {
        outp(MOUSE_INTERVAL, rate & 0x03);
    }
}

/* ====================================================================== */
/*  mouse_available — マウスが使用可能か                                   */
/* ====================================================================== */
int mouse_available(void)
{
    return (mouse_mode_current != MOUSE_MODE_NONE);
}

/* ====================================================================== */
/*  mouse_get_mode — 現在のモード取得                                      */
/* ====================================================================== */
u8 mouse_get_mode(void)
{
    return mouse_mode_current;
}

/* ====================================================================== */
/*  カーソル表示レイヤー                                                   */
/* ====================================================================== */
static int  cursor_display_mode = 0;  /* MOUSE_CURSOR_NONE */
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

    if (cursor_display_mode == 0) return;  /* NONE */

    cx = cursor_x / 8;
    cy = cursor_y / 16;
    if (cx > 79) cx = 79;
    if (cy > 24) cy = 24;

    if (cursor_display_mode == 1) {
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
    } else if (cursor_display_mode == 2) {
        /* GFX: スタブ (将来実装) */
    }

    cursor_visible = 1;
}

void mouse_cursor_hide(void)
{
    if (!cursor_visible) return;

    if (cursor_display_mode == 1 && cursor_prev_cx >= 0) {
        /* TEXT: 反転解除 */
        tvram_reverse_cell(cursor_prev_cx, cursor_prev_cy);
        cursor_prev_cx = -1;
        cursor_prev_cy = -1;
    } else if (cursor_display_mode == 2) {
        /* GFX: スタブ (将来実装) */
    }

    cursor_visible = 0;
}
