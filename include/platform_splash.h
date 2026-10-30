#ifndef PLATFORM_SPLASH_H
#define PLATFORM_SPLASH_H
#include "types.h"
/* Fixed platform entry points, synchronous boot foreground only.
 * begin returns 0 when drawing is possible; failure already shuts gfx down.
 * The framebuffer descriptor borrows gfx storage and is refreshed each begin.
 * Drawing is valid only between a successful begin and gfx_shutdown. */
int platform_splash_begin(void);
void platform_splash_clear(u8 color);
void platform_splash_fill_rect(int x, int y, int w, int h, u8 color);
void platform_splash_clear_pages(void);
#endif
