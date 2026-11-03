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
/* Internal boot foreground result; no public KAPI change. */
enum { PLATFORM_SPLASH_DONE = 0, PLATFORM_SPLASH_FALLBACK = 1,
       PLATFORM_SPLASH_NO_SYNC = 2 };
int platform_splash_try_image(const char *path, u32 end_tick);
/* Absolute boot-entry deadline in microseconds, shared with logo/exit. */
int platform_splash_try_image_until(const char *path, unsigned long long deadline_us);
/* Clock-paced static/fixed display; failure means no usable clock. */
int platform_splash_wait_frame(u32 end_tick);
#ifdef SPLASH_RASTER_DIAG
void platform_splash_probe_before(void);
void platform_splash_probe(void);
#endif
#endif
