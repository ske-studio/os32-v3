#ifndef OS32_BACKEND_OF_FB_H
#define OS32_BACKEND_OF_FB_H
#include "gfx_hal.h"
/* Platform supplies a validated full-screen alias and a ledger CLIENT shadow.
 * INDEX8 is the Apple OF palette (white 0, black 255). No OF calls here. */
struct of_fb_desc {
    volatile u8 *display;
    u8 *shadow;
    u32 width, height, pitch, bytes, shadow_pitch, shadow_bytes;
    u32 protected_rows;
};
extern GfxBackend gfx_backend_of_fb;
extern const struct GfxBackendV2 of_fb_v2;
int of_fb_bind(const struct of_fb_desc *d);
int of_fb_ready(void);
void of_fb_unbind(void);
int of_fb_copy(const u8 *src, u32 pitch, u32 bytes,
               int x, int y, int w, int h);
#endif
