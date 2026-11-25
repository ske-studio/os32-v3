#include "gfx_hal.h"
#include "sys/kern/gfx/osri/gfx_osri.h"
#include "sys/kern/gfx/osri/gfx_osri_view_internal.h"
#include "sys/dev/video/of_fb/backend_of_fb.h"
#include "kmalloc.h"

#include "kernel_tick.h"
#include "input_service.h"
#include "kbd.h"
static const struct gfx_kernel_fb *published;
static unsigned long long generation;

const struct gfx_kernel_fb *pm_osri_decoded(unsigned long long *bound)
{
    *bound = generation;
    return osri_active ? published : 0;
}

int gfx_osri_view(const char *path, u32 duration_ms, int caller)
{
    struct gfx_kernel_fb client;
    if (OSRI_VIEW_BUSY(caller)) return OS32_ERR_BUSY;
    if (!g_backend || !g_backend->osri_show || gfx_kernel_framebuffer(&client) ||
        client.format != GFX_BB_PACKED8 || client.width < GFX_WIDTH ||
        client.height < GFX_HEIGHT || client.pitch < client.width ||
        !(generation = of_fb_osri_generation())) return OS32_ERR_NOSYS;

    osri_active = 1;
    struct OsriViewRun run = {caller, tick_count, duration_ms};
    struct gfx_kernel_fb decoded = { .width = GFX_WIDTH, .height = GFX_HEIGHT,
                                    .pitch = GFX_BPL, .format = GFX_BB_PLANAR4 };
    struct OsriImage *image = kmalloc(sizeof(*image));
    u8 *pixels = kmalloc(OSRI_PIXELS_BYTES);
    int rc = OS32_ERR_NOSPC;
    if (pixels && image) {
        for (unsigned i = 0; i < GFX_BB_PLANES; ++i)
            decoded.planes[i] = pixels + i * GFX_PLANE_SZ;
        rc = osri_load_checked(path, &decoded, image);
        if (generation != of_fb_osri_generation()) rc = OS32_ERR_IO;
        if (!rc) {
            published = &decoded;
            rc = g_backend->osri_show(image, &run);
            published = 0;
        }
    }
    if (pixels) kfree(pixels);
    if (image) kfree(image);
    osri_active = 0;
    return rc;
}


/* -1 empty, 0 Esc, 1 key press, 2 release (consume without extending). */
int pm_osri_key(void)
{
    if (con_sink_is_enabled()) {
        int raw = input_trygetrawkey();
        if (raw < 0) return -1;
        if (!(raw & KBD_RAW_MAKE)) return 2;
        return (raw & KBD_RAW_KEY_MAKE_MASK) != (KEY_ESC | KBD_RAW_MAKE);
    }
    int key = input_trygetkey();
    return key < 0 ? -1 : (key & 255) != 27;
}
