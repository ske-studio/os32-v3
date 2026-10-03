#include "libos32gfx.h"
#include "libgfx_attach_internal.h"

KernelAPI *gfx_api;
GFX_Framebuffer gfx_fb;
int gfx_dirty_suppress;
int gfx_packed;
int gfx_ready;
const struct gfx_attach_port *gfx_attach_port;
static int gfx_pools_initialized;
static struct gfx_attach_ref gfx_ref;
static struct gfx_attach_view gfx_view;

static unsigned int gfx_cpl(void)
{
    unsigned short cs;
    __asm__ volatile("mov %%cs,%0" : "=r"(cs));
    return cs & 3U;
}

void libos32gfx_detach(void)
{
    u32 token = gfx_view.token;
    gfx_ready = 0;
    gfx_fb = (GFX_Framebuffer){0};
    gfx_packed = 0;
    gfx_ref = (struct gfx_attach_ref){0};
    gfx_view = (struct gfx_attach_view){0};
    /* A revoked token returns INVAL; never restore its old pointer. */
    if (token && gfx_attach_port) (void)gfx_attach_port->unlease(token);
}

static int gfx_refresh(void)
{
    struct gfx_attach_desc desc = {0};
    struct gfx_attach_view view = {0};
    GFX_Framebuffer fb = {0};
    GFX_ScreenInfo si = {0};
    int rc;
    u32 i;
    if (!gfx_api) goto invalid;
    /* CPL=0 (including gshell) must never enter USER query authorization.
     * NULL is the e6 production binding, not an error fallback from a port. */
    if (gfx_cpl() == 0 || !gfx_attach_port) {
        gfx_api->gfx_get_framebuffer(&fb);
        if (!fb.planes[0] || fb.width <= 0 || fb.height <= 0 || fb.pitch <= 0)
            goto invalid;
        gfx_fb = fb;
        gfx_packed = fb.planes[1] == (u8 *)0;
        if (!gfx_packed && gfx_api->version >= 40) {
            gfx_api->gfx_screen_info(&si);
            if (si.format == GFX_FMT_PACKED8) gfx_packed = 1;
        }
        gfx_ready = 1;
        return 0;
    }
    rc = gfx_attach_port->query(&desc);
    if (rc) goto fail;
    if (gfx_ready && gfx_view.token && gfx_ref.sid == desc.ref.sid &&
        gfx_ref.generation == desc.ref.generation) return 0;
    libos32gfx_detach();
    if (!desc.ref.generation || !desc.width || !desc.height || !desc.pitch ||
        desc.width > 0x7fffffffU || desc.height > 0x7fffffffU ||
        desc.pitch > 0x7fffffffU || desc.height > desc.bytes / desc.pitch ||
        !((desc.format == GFX_FMT_PLANAR4 && desc.planes == 4) ||
          (desc.format == GFX_FMT_PACKED8 && desc.planes == 1))) goto invalid;
    rc = gfx_attach_port->lease(&desc.ref, &view);
    if (rc) goto fail;
    /* Keep ownership before validating the result, so partial failure rolls
     * back the acquired token as well as clearing all drawing state. */
    gfx_view = view;
    if (!view.token || !view.base || view.bytes != desc.bytes ||
        view.bytes > ~view.base) goto invalid;
    for (i = 0; i < 4; i++) {
        if (i < desc.planes) {
            if (desc.plane_offset[i] > desc.bytes ||
                desc.pitch * desc.height > desc.bytes - desc.plane_offset[i] ||
                view.planes[i] != view.base + desc.plane_offset[i]) goto invalid;
            fb.planes[i] = (u8 *)view.planes[i];
        } else if (view.planes[i]) goto invalid;
    }
    /* query describes storage. Existing screen_info supplies visible height
     * (200 vs registered 400); only reacquisition needs this additional call.
     * The unchanged-generation check above performs just one query. */
    gfx_api->gfx_screen_info(&si);
    if (si.width != (int)desc.width || si.height <= 0 ||
        (u32)si.height > desc.height || si.format != (int)desc.format) goto invalid;
    fb.width = desc.width; fb.height = si.height; fb.pitch = desc.pitch;
    gfx_ref = desc.ref;
    gfx_fb = fb;
    gfx_packed = desc.format == GFX_FMT_PACKED8;
    gfx_ready = 1;
    return 0;
invalid:
    rc = OS32_ERR_INVAL;
fail:
    libos32gfx_detach();
    return rc;
}

int libos32gfx_attach_checked(void)
{
    int rc = gfx_refresh();
    /* Pools do not depend on a framebuffer. Initialize even after FULL so
     * check/present can recover, but never reset live objects on reattach. */
    if (gfx_api && !gfx_pools_initialized) {
        gfx_surface_init();
        gfx_sprite_init();
        gfx_pools_initialized = 1;
    }
    return rc;
}

int libos32gfx_check(void)
{
    /* No extra legacy KAPI traffic before e11, or for TRUSTED callers. */
    if (gfx_ready && (gfx_cpl() == 0 || !gfx_attach_port)) return 0;
    return gfx_refresh();
}

void libos32gfx_attach(KernelAPI *api)
{
    gfx_api = api;
    (void)libos32gfx_attach_checked();
}

void libos32gfx_init(KernelAPI *api)
{
    gfx_api = api;
    gfx_api->gfx_init();
    (void)libos32gfx_attach_checked();
}

void libos32gfx_shutdown(void)
{
    libos32gfx_detach();
    gfx_api->gfx_shutdown();
}

void gfx_present(void)
{
    if (libos32gfx_check() || !gfx_ready) return;
    gfx_api->gfx_add_dirty_rect(0, 0, gfx_fb.width, gfx_fb.height);
}
