#ifndef OS32_DISPLAY_FALLBACK_H
#define OS32_DISPLAY_FALLBACK_H
extern GfxBackend gfx_backend_of_fb;
#define DISPLAY_FALLBACK_BACKEND (&gfx_backend_of_fb)
int of_fb_palette(int first, int count, const u8 *rgb);
#define DISPLAY_SET_PALETTE(be, first, count, rgb) of_fb_palette(first, count, rgb)
int display_provider_v1_width(void);
#define DISPLAY_V1_WIDTH (display_provider_v1_width())
int display_provider_shadow_push(int x, int y, int w, int h);
#define DISPLAY_SCROLL_OK() DISPLAY_V1_PRESENT_OK()
#define DISPLAY_V2_SHADOW_PUSH(x, y, w, h) display_provider_shadow_push(x, y, w, h)
int display_provider_present_ok(void);
void display_provider_public_init_ok(void);
#define DISPLAY_V1_PRESENT_OK() display_provider_present_ok()
#define DISPLAY_V1_PUBLIC_INIT_OK() display_provider_public_init_ok()
/* Called at the end of gfx_shutdown to put the text console back on the OF framebuffer.
 * Normal context only. Allocation, drawing and logging are permitted. */
void display_provider_console_return(void);
int display_provider_console_owns_scanout(void);
/* Called from exec exit notification before returning display ownership.
 * Normal context only. Allocation, drawing and logging are permitted. */
void display_provider_owner_exit(int id);
#endif
