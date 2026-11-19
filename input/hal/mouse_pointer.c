/* Shared PointerOps polling, bounds and text cursor. */
#include "mouse_pointer_contract.h"
#include "in2_int.h"
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
