#ifndef OS32_DISPLAY_FALLBACK_H
#define OS32_DISPLAY_FALLBACK_H
extern GfxBackend gfx_backend_of_fb;
#define DISPLAY_FALLBACK_BACKEND (&gfx_backend_of_fb)
/* Called at the end of gfx_shutdown to put the text console back on the OF framebuffer.
 * Normal context only. Allocation, drawing and logging are permitted. */
void display_provider_console_return(void);
/* Called from exec exit notification before returning display ownership.
 * Normal context only. Allocation, drawing and logging are permitted. */
void display_provider_owner_exit(int id);
#endif
