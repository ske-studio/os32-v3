/* ======================================================================== */
/*  MOUSE.C — マウスドライバ統合モジュール                                  */
/*                                                                          */
/*  起動時にNP21/Wを検出し、シームレス/バスマウスを自動選択する。           */
/*  外部からは mouse_poll() で統一的にアクセスできる。                      */
/* ======================================================================== */

/* PC-98 acquisition and text-cell geometry. */
#include "irq_line.h"
#include "mouse.h"
#include "mouse_pointer_contract.h"
#include "../../input/hal/in2_int.h"
#include "kcg_contract.h"
#include "../char/np2sysp.h"
#include "pc98.h"
#include "kprintf.h"

static u8 mouse_mode_current = MOUSE_MODE_NONE;

/* Preserve calls across the PC-98 / PointerOps boundary when the block
 * below moves to its own translation unit in stage 3b-ii. */

/* ====================================================================== */
/*  mouse_init — マウスドライバ初期化                                      */
/* ====================================================================== */
void mouse_init(void)
{
    mouse_pointer_init_position();
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

__attribute__((noipa))
u8 mouse_pointer_snapshot(i16 *dx, i16 *dy, u8 *buttons, i16 *x, i16 *y)
{
    if (mouse_mode_current == MOUSE_MODE_SEAMLESS) mouse_hal_seamless_poll();
    if (mouse_mode_current != MOUSE_MODE_NONE) {
        mouse_legacy_snapshot(dx, dy, buttons, x, y);
    }
    return mouse_mode_current;
}

int mouse_available(void)
{
    return (mouse_mode_current != MOUSE_MODE_NONE);
}

__attribute__((noipa))
void mouse_pointer_text_cell(i16 x, i16 y, int *cx, int *cy)
{
    *cx = x / KCG_ANK_W;
    *cy = y / KCG_ANK_H;
    if (*cx > TVRAM_COLS - 1) *cx = TVRAM_COLS - 1;
    if (*cy > TVRAM_ROWS - 1) *cy = TVRAM_ROWS - 1;
}

/* PointerOps state and behavior. Keep this block together for stage 3b-ii. */
#include "pointer_service.h"
#include "kbd_contract.h"
#include "text_screen.h"

static i16 cursor_x;
static i16 cursor_y;

static i16 clamp16(i16 val, i16 lo, i16 hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

__attribute__((noipa))
void mouse_pointer_init_position(void)
{
    pointer_position(&cursor_x, &cursor_y);
}

void mouse_poll(MouseState *state)
{
    i16 dx = 0, dy = 0;
    u8 buttons = 0;
    u8 mode = mouse_pointer_snapshot(&dx, &dy, &buttons, &cursor_x, &cursor_y);

    kbd_stop_mouse_sample(buttons);

    if (state) {
        state->x = cursor_x;
        state->y = cursor_y;
        state->dx = dx;
        state->dy = dy;
        state->buttons = buttons;
        state->mode = mode;
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
/*  カーソル表示レイヤー                                                   */
/* ====================================================================== */
static int  cursor_display_mode = MOUSE_CURSOR_NONE;  /* MOUSE_CURSOR_NONE */
static int  cursor_visible = 0;
static int  cursor_prev_cx = -1;
static int  cursor_prev_cy = -1;
static int  cursor_prev_width = 1;

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

    mouse_pointer_text_cell(cursor_x, cursor_y, &cx, &cy);

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
