#ifndef PLATFORM_SPLASH_H
#define PLATFORM_SPLASH_H
#include "types.h"
#include "memmap.h"
#define PLATFORM_SPLASH_TICKS (3u * PIT_HZ)
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
/* Input: boot-entry deadline. Output: current tick plus unconsumed ticks.
 * Dynamic model time does not advance the global PIT clock. */
int platform_splash_try_image(const char *path, u32 *end_tick);
/* Clock-paced static/fixed display; failure means no usable clock. */
int platform_splash_wait_frame(u32 end_tick);
#ifdef SPLASH_RASTER_OBS
void platform_splash_observe(void);
#endif
#ifdef SPLASH_RASTER_DIAG
void platform_splash_probe_before(void);
void platform_splash_probe(void);
#endif
#endif
