#ifndef OS32_MOUSE_POINTER_CONTRACT_H
#define OS32_MOUSE_POINTER_CONTRACT_H

#include "pointer_service.h"

/* Seed the displayed position during device initialization. */
void mouse_pointer_init_position(void);

/* Acquire once and return the active mouse mode. In MOUSE_MODE_NONE the
 * caller's sample and displayed position are left as supplied. */
u8 mouse_pointer_snapshot(i16 *dx, i16 *dy, u8 *buttons, i16 *x, i16 *y);

/* Map pixels to the backend's legacy text cursor grid. */
void mouse_pointer_text_cell(i16 x, i16 y, int *cx, int *cy);

#endif
