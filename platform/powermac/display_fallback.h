#ifndef OS32_DISPLAY_FALLBACK_H
#define OS32_DISPLAY_FALLBACK_H
extern GfxBackend gfx_backend_of_fb;
#define DISPLAY_FALLBACK_BACKEND (&gfx_backend_of_fb)
#endif
