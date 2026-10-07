#include "libos32gfx.h"
#include "libgfx_attach_internal.h"
#include "utf8_internal.h"
#include "memmap.h"

KernelAPI *gfx_api;
GFX_Framebuffer gfx_fb;
int gfx_dirty_suppress;
int gfx_packed;
int gfx_ready;
const struct gfx_attach_port *gfx_attach_port;
const struct gfx_attach_port *gfx_unicode_port;
static struct gfx_attach_ref unicode_ref;
static struct gfx_attach_view unicode_view;
static int gfx_pools_initialized;
static int gfx_stopped;
static struct gfx_attach_ref gfx_ref;
static struct gfx_attach_view gfx_view;

/* Generated v70 value ABI; these ports own no kernel aliases. */
static int kapi_query(u32 role, struct gfx_attach_desc *out)
{
    OS32_SurfaceQueryResult q;
    const OS32_SurfaceDesc *d;
    u32 i;
    int rc = gfx_api->surface_query(role, &q);
    if (rc) return rc;
    if (q.count != 1 || q.desc[0].role != role) return OS32_ERR_INVAL;
    d = &q.desc[0];
    out->ref = (struct gfx_attach_ref){d->ref.sid, d->ref.generation};
    out->format = d->format; out->width = d->width; out->height = d->height;
    out->pitch = d->pitch; out->planes = d->planes; out->bytes = d->bytes;
    for (i = 0; i < 4; i++) out->plane_offset[i] = d->plane_offset[i];
    return 0;
}

static int kapi_lease(u32 role, u32 access, const struct gfx_attach_ref *ref,
                      struct gfx_attach_view *out)
{
    OS32_SurfaceRef r = {ref->sid, ref->generation};
    OS32_LeaseView v;
    u32 i;
    int rc = gfx_api->surface_lease(role, &r, access, &v);
    if (rc) return rc;
    out->token = v.token; out->base = v.base; out->bytes = v.bytes;
    for (i = 0; i < 4; i++) out->planes[i] = v.planes[i];
    return 0;
}

static int client_query(struct gfx_attach_desc *out)
{ return kapi_query(OS32_SURFACE_CLIENT, out); }
static int unicode_query(struct gfx_attach_desc *out)
{ return kapi_query(OS32_SURFACE_UNICODE, out); }
static int client_lease(const struct gfx_attach_ref *ref, struct gfx_attach_view *out)
{ return kapi_lease(OS32_SURFACE_CLIENT, OS32_SURFACE_RW, ref, out); }
static int unicode_lease(const struct gfx_attach_ref *ref, struct gfx_attach_view *out)
{ return kapi_lease(OS32_SURFACE_UNICODE, OS32_SURFACE_RO, ref, out); }
static int kapi_unlease(u32 token)
{ return gfx_api->surface_unlease(token); }
static const struct gfx_attach_port gfx_kapi_port = {client_query, client_lease, kapi_unlease};
static const struct gfx_attach_port unicode_kapi_port = {unicode_query, unicode_lease, kapi_unlease};

static void bind_ports(void)
{
    if (gfx_api && gfx_api->version >= 70) {
        gfx_attach_port = &gfx_kapi_port;
        gfx_unicode_port = &unicode_kapi_port;
    }
}

static unsigned int gfx_cpl(void)
{
    unsigned short cs;
    __asm__ volatile("mov %%cs,%0" : "=r"(cs));
    return cs & 3U;
}

void libos32gfx_unicode_init(void)
{
    struct gfx_attach_desc desc = {0};
    struct gfx_attach_view view = {0};
    bind_ports();
    /* CPL0 keeps its internal path; user failure never probes the low alias. */
    if (gfx_cpl() == 0) {
        utf8_set_jis_table((const u8 *)P2V_CONST(MEM_UNICODE_TABLE_BASE));
        return;
    }
    if (!gfx_unicode_port) return;
    utf8_set_jis_table(0);
    if (gfx_unicode_port->query(&desc)) goto fail;
    if (unicode_view.token && unicode_ref.sid == desc.ref.sid &&
        unicode_ref.generation == desc.ref.generation) {
        utf8_set_jis_table((const u8 *)unicode_view.base);
        return;
    }
    if (unicode_view.token) (void)gfx_unicode_port->unlease(unicode_view.token);
    unicode_view = (struct gfx_attach_view){0};
    if (!desc.ref.generation || desc.bytes != MEM_UNICODE_TABLE_SIZE ||
        desc.planes != 1 || desc.plane_offset[0]) goto fail;
    if (gfx_unicode_port->lease(&desc.ref, &view)) goto fail;
    unicode_view = view;
    if (!view.token || !view.base || view.bytes != desc.bytes ||
        view.bytes > ~view.base || view.planes[0] != view.base ||
        view.planes[1] || view.planes[2] || view.planes[3]) goto fail;
    unicode_ref = desc.ref;
    utf8_set_jis_table((const u8 *)view.base);
    return;
fail:
    if (unicode_view.token) (void)gfx_unicode_port->unlease(unicode_view.token);
    unicode_view = (struct gfx_attach_view){0};
    unicode_ref = (struct gfx_attach_ref){0};
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
    /* CPL0 (including gshell) bypasses USER query authorization.
     * A NULL port is only the pre-v70 compatibility path. */
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
    int rc;
    gfx_stopped = 0;
    libos32gfx_unicode_init();
    rc = gfx_refresh();
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
    /* CLIENT regen occurs in our fullscreen init/shutdown only. V86 return
     * regenerates DISPLAY only. Check at present and explicit wait returns,
     * never in each drawing primitive. */
    if (gfx_stopped) return OS32_ERR_INVAL;
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
    gfx_stopped = 1;
    libos32gfx_detach();
    if (gfx_api) gfx_api->gfx_shutdown();
}

void gfx_present(void)
{
    if (libos32gfx_check() || !gfx_ready) return;
    gfx_api->gfx_add_dirty_rect(0, 0, gfx_fb.width, gfx_fb.height);
}

int libos32gfx_getchar(void)
{
    int key = gfx_api->kbd_getchar();
    (void)libos32gfx_check();
    return key;
}

int libos32gfx_trygetchar(void)
{
    int key = gfx_api->kbd_trygetchar();
    /* Empty polling does not park. Match Rust's input return gate. */
    if (key >= 0) (void)libos32gfx_check();
    return key;
}

void libos32gfx_yield(void)
{
    gfx_api->sys_yield();
    (void)libos32gfx_check();
}

void libos32gfx_halt(void)
{
    gfx_api->sys_halt();
    (void)libos32gfx_check();
}
