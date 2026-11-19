#ifndef OS32_MOUSE_POINTER_CONTRACT_H
#define OS32_MOUSE_POINTER_CONTRACT_H

#include "pointer_service.h"

void mouse_poll(MouseState *state);
void mouse_set_bounds(i16 x_min, i16 y_min, i16 x_max, i16 y_max);
void mouse_cursor_show(void);
void mouse_cursor_set_mode(int mode);
void mouse_cursor_hide(void);

/* Seed the displayed position during device initialization. */
void mouse_pointer_init_position(void);

typedef struct MousePointerBackend {
    /* Acquire once and return the active mouse mode. In MOUSE_MODE_NONE the
     * caller's sample and displayed position are left as supplied. */
    u8 (*snapshot)(i16 *dx, i16 *dy, u8 *buttons, i16 *x, i16 *y);

    /* Map pixels to the backend's legacy text cursor grid. */
    void (*text_cell)(i16 x, i16 y, int *cx, int *cy);
} MousePointerBackend;

void mouse_pointer_backend_register(const MousePointerBackend *b);

#endif
