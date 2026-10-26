/* Recording mouse provider for fixtures that include the real PC98 binding. */
#include "../../drivers/mouse.h"
static int pointer_seen, pointer_mode;
static i16 pointer_bounds[4];
static MouseState *pointer_output;
void mouse_poll(MouseState *state)
{
    pointer_seen = 1;
    pointer_output = state;
    if (!state) return;
    state->x = -123; state->y = 456;
    state->dx = 7; state->dy = -8;
    state->buttons = MOUSE_BTN_LEFT | MOUSE_BTN_RIGHT;
    state->mode = MOUSE_MODE_SEAMLESS;
}
int mouse_available(void) { pointer_seen = 2; return 1; }
void mouse_set_bounds(i16 x_min, i16 y_min, i16 x_max, i16 y_max)
{
    pointer_seen = 3;
    pointer_bounds[0] = x_min; pointer_bounds[1] = y_min;
    pointer_bounds[2] = x_max; pointer_bounds[3] = y_max;
}
void mouse_cursor_set_mode(int mode) { pointer_seen = 4; pointer_mode = mode; }
void mouse_cursor_show(void) { pointer_seen = 5; }
void mouse_cursor_hide(void) { pointer_seen = 6; }
