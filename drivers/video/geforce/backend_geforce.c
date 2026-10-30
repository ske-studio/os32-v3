/* GeForce HAL adapter. Reservation and device ownership belong to the caller. */
#include "gfx_hal.h"
#include "video_service.h"
#include "memmap.h"
#include "../../../gfx/palette.h"
#include "../pc98/pc98_route.h"
#ifdef OS32_GEFORCE_MODULE
#include "geforce_module.h"
#define gfx_backend_geforce (*geforce_context->backend)
#define gfx_current_height (*geforce_context->height)
#define gfx_flip_enabled (*geforce_context->flip)
#define gfx_display_page (*geforce_context->page)
#endif
extern void geforce_display_failed(void) __attribute__((weak));

#define GF_WIDTH 640
#define GF_HEIGHT 480
#define GF_PITCH GF_WIDTH
#define GF_MODE_INDEX 0U
#define GF_LEASE_FIRST 16
#define GF_LEASE_COUNT (VIDEO_PALETTE_ENTRIES - GF_LEASE_FIRST)
#define GF_GRAY_FIRST 8
#define GF_GRAY_STEP 10
#define GF_CUBE_STEP 51

extern int pc98_route_to_gf(void);
extern int pc98_route_to_98(void);
extern int pc98_route_cell_check(void);
extern int gf_present_rect(const u8 *, u32, volatile u8 *, u32, volatile u8 *,
                           int, int, int, int, int (*)(void *), void *);
#ifndef OS32_GEFORCE_MODULE
extern int gfx_current_height, gfx_flip_enabled, gfx_display_page;
extern GfxBackend gfx_backend_geforce;
#endif

static struct video_device *device;
static int (*identify_resources)(void);
static struct video_fb framebuffer;
static u32 bar0_phys;
static int (*interrupt_point)(void *);
static void *interrupt_context;
static int failed, active, routed;
static int (*stop_device)(void);
void geforce_backend_stop_bind(int (*stop)(void)) { stop_device = stop; }
static u8 initial_palette[VIDEO_PALETTE_BYTES];

/* Integration supplies a long-lived device and a read-only identity/resource
 * check. The core binds the CLIENT surface into gfx_backend_geforce.bb_base. */
void geforce_backend_bind(struct video_device *d, int (*identify)(void),
                          u32 bar0, int (*point)(void *), void *context)
{
    device = d;
    identify_resources = identify;
    bar0_phys = bar0;
    interrupt_point = point;
    interrupt_context = context;
}

static int geforce_probe(void)
{
    return !failed && device && identify_resources && identify_resources();
}

static void geforce_prepare(void)
{
    (void)geforce_probe();
}

static void geforce_palette_init(void)
{
    const PaletteEntry *sys = palette_get_all();
    u32 i = 0;
    for (; i < GF_LEASE_FIRST; ++i) {
        initial_palette[i * GFX_RGB_COMPONENTS + GFX_RGB_R] = (sys[i].r & GFX_PALETTE_COMPONENT_MASK) * PEGC_PAL_KAPI_SCALE;
        initial_palette[i * GFX_RGB_COMPONENTS + GFX_RGB_G] = (sys[i].g & GFX_PALETTE_COMPONENT_MASK) * PEGC_PAL_KAPI_SCALE;
        initial_palette[i * GFX_RGB_COMPONENTS + GFX_RGB_B] = (sys[i].b & GFX_PALETTE_COMPONENT_MASK) * PEGC_PAL_KAPI_SCALE;
    }
    for (u32 r = 0; r < GFX_PALETTE_CUBE_LEVELS; ++r)
        for (u32 g = 0; g < GFX_PALETTE_CUBE_LEVELS; ++g)
            for (u32 b = 0; b < GFX_PALETTE_CUBE_LEVELS; ++b, ++i) {
                initial_palette[i * GFX_RGB_COMPONENTS + GFX_RGB_R] = r * GF_CUBE_STEP;
                initial_palette[i * GFX_RGB_COMPONENTS + GFX_RGB_G] = g * GF_CUBE_STEP;
                initial_palette[i * GFX_RGB_COMPONENTS + GFX_RGB_B] = b * GF_CUBE_STEP;
            }
    for (u32 gray = 0; i < VIDEO_PALETTE_ENTRIES; ++i, ++gray) {
        u8 v = GF_GRAY_FIRST + gray * GF_GRAY_STEP;
        for (u32 c = 0; c < GFX_RGB_COMPONENTS; ++c)
            initial_palette[i * GFX_RGB_COMPONENTS + c] = v;
    }
}

static int geforce_stop(void)
{
    int rc = stop_device ? stop_device() : device ? video_stop(device) : 0;
    if (pc98_route_to_98()) rc = -1;
    active = routed = 0;
    if (rc) failed = 1;
    return rc;
}

static void geforce_fail(void)
{
    failed = 1;
    geforce_stop();
    if (geforce_display_failed) geforce_display_failed();
}

static int geforce_transfer(int x, int y, int w, int h)
{
    return gf_present_rect(gfx_backend_geforce.bb_base, gfx_backend_geforce.bb_pitch,
                           P2V_IO(framebuffer.phys), framebuffer.pitch, P2V_IO(bar0_phys),
                           x, y, w, h, interrupt_point, interrupt_context);
}

static void geforce_init(void)
{
    struct video_mode mode;
    if (!geforce_probe()) return;
    if (active) return; /* One fixed mode; gfx_init -> gfx_init_200 preserves it. */
    if (video_mode_info(device, GF_MODE_INDEX, &mode) ||
        video_framebuffer(device, &framebuffer) || !gfx_backend_geforce.bb_base) {
        geforce_fail();
        return;
    }
    geforce_palette_init();
    pc98_console_set_safe(0);
    if (video_start(device, mode.id, initial_palette) ||
        geforce_transfer(0, 0, GF_WIDTH, GF_HEIGHT) ||
        pc98_route_cell_check() || pc98_route_to_gf()) {
        geforce_fail();
        return;
    }
    active = routed = 1;
    gfx_flip_enabled = gfx_display_page = 0;
    gfx_current_height = GF_HEIGHT;
}

static int geforce_query(GFX_ScreenInfo *info)
{
    const GfxMode *mode = gfx_backend_mode(&gfx_backend_geforce);
    if (!info || !mode) return OS32_ERR_INVAL;
    info->width = mode->width; info->height = mode->height;
    info->bpp = GFX_FMT_PACKED8_BPP; info->format = mode->format;
    info->flags = 0; info->lease_mask = 0;
    info->lease_first = GF_LEASE_FIRST; info->lease_count = GF_LEASE_COUNT;
    for (u32 i = 0; i < sizeof(info->reserved) / sizeof(info->reserved[0]); ++i)
        info->reserved[i] = 0;
    return 0;
}

static void geforce_present(int x, int y, int w, int h)
{
    if (active && geforce_transfer(x, y, w, h)) geforce_fail();
}

static void geforce_set_palette(int first, int count, const u8 *rgb)
{
    u8 converted[VIDEO_PALETTE_BYTES];
    if (!active || !rgb || first < 0 || count <= 0 || first >= (int)VIDEO_PALETTE_ENTRIES ||
        count > (int)VIDEO_PALETTE_ENTRIES - first) return;
    for (int i = 0; i < count * GFX_RGB_COMPONENTS; ++i)
        converted[i] = (rgb[i] & GFX_PALETTE_COMPONENT_MASK) * PEGC_PAL_KAPI_SCALE;
    if (video_palette(device, first, count, converted)) { geforce_fail(); return; }
    for (int i = 0; i < count && first + i < GF_LEASE_FIRST; ++i)
        palette_shadow_set(first + i, rgb[i * GFX_RGB_COMPONENTS],
                           rgb[i * GFX_RGB_COMPONENTS + 1], rgb[i * GFX_RGB_COMPONENTS + 2]);
}

static void geforce_enter(void)
{
    if (!active && !failed) geforce_init();
    else if (active && !routed) {
        if (pc98_route_to_gf()) geforce_fail();
        else routed = 1;
    }
}
static void geforce_leave(void)
{
    if (active && routed && geforce_stop() && geforce_display_failed)
        geforce_display_failed();
}
static void geforce_shutdown(void)
{
    if (active && geforce_stop() && geforce_display_failed) geforce_display_failed();
}

static const GfxMode geforce_modes[] = {
    {GF_WIDTH, GF_HEIGHT, GFX_FMT_PACKED8, VIDEO_RGB_BITS, 0, 0, GF_PITCH}
};
#ifdef OS32_GEFORCE_MODULE
GfxBackend geforce_backend_ops = {
#else
GfxBackend gfx_backend_geforce = {
#endif
    .name = "geforce", .probe = geforce_probe, .init = geforce_init,
    .query = geforce_query, .shutdown = geforce_shutdown,
    .present_rect = geforce_present, .set_palette = geforce_set_palette,
    .enter = geforce_enter, .leave = geforce_leave,
    .bb_pitch = GF_PITCH, .bb_format = GFX_BB_PACKED8,
    .prepare = geforce_prepare, .modes = geforce_modes,
    .mode_count = sizeof(geforce_modes) / sizeof(geforce_modes[0])
};
