/* Boot foreground image owner. Pixels borrow the existing planar BB. */
#include "platform_splash.h"
#include "gfx_osri.h"
#include "splash_raster.h"
#include "kmalloc.h"
#include "palette.h"
#include "kprintf.h"
#include "vfs.h"
#include "io.h"

extern volatile u32 tick_count;

int platform_splash_try_image(const char *path, u32 end_tick)
{
    struct gfx_kernel_fb fb;
    struct SplashRaster *s = pc98_splash_raster_state();
    struct OsriImage *image = 0;
    OS32_Stat stat;
    const char *reason = "framebuffer";
    int rc = PLATFORM_SPLASH_FALLBACK;
    if (gfx_kernel_framebuffer(&fb)) goto done;
    reason = "missing";
    if (vfs_stat(path, &stat)) goto done;
    reason = "memory";
    image = kmalloc(sizeof(*image));
    if (!image) goto done;
    reason = "invalid";
    if (osri_load(path, &fb, image)) goto done;
    /* Stage 1 admits fixed palettes only; no difference events reach VRAM. */
    reason = "event budget";
    if (image->count) goto done;
    if ((i32)(tick_count - end_tick) >= 0) {
        rc = PLATFORM_SPLASH_DONE;
        goto done;
    }
    gfx_add_dirty_rect(0, 0, GFX_WIDTH, GFX_HEIGHT);
    gfx_present_nosync();
    reason = "no sync";
    if (_irq_enabled()) {
        if (pc98_splash_raster_frame(s, image)) {
            rc = PLATFORM_SPLASH_NO_SYNC;
            goto done;
        }
    } else {
        for (u32 i = 0; i < OSRI_COLORS; i++) {
            u16 rgb = image->pal[i];
            palette_set(i, (rgb >> 8) & GFX_PALETTE_COMPONENT_MASK,
                        (rgb >> 4) & GFX_PALETTE_COMPONENT_MASK,
                        rgb & GFX_PALETTE_COMPONENT_MASK);
        }
    }
    /* Initial palette and VRAM are committed once, then IRQs remain available. */
    while ((i32)(tick_count - end_tick) < 0)
        if (platform_splash_wait_frame(end_tick)) break;
    rc = PLATFORM_SPLASH_DONE;
done:
    if (image) kfree(image);
    if (rc != PLATFORM_SPLASH_DONE)
        kprintf(0x07, "[splash] %s: builtin logo\n", reason);
    return rc;
}
