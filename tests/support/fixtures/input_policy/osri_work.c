#include "pc98.h"
#include "sys/hal/gfx/gfx_internal.h"
static u16 attributes[TVRAM_COLS * TVRAM_ROWS];
#define P2V_IO(address) ((void)(address), attributes)
#include "sys/platform/pc98/video/backend_pc98_osri.c"
#include "io_clock.h"

unsigned input_test_irq;
volatile u32 kctx_irq_depth, tick_count;
int gfx_current_height = GFX_HEIGHT, gfx_flip_enabled;
static struct SplashRaster raster = {.checked = 1, .dynamic_ok = 1};
static struct OsriImage image;
static struct PlV2Plan plan;
static u16 saved_text[OSRI_TEXT_CELLS];
static unsigned frames, serviced, bad, cleared, presented;
static unsigned sample, writes, row_writes, selected;
static u8 palette[OSRI_COLORS];

void *kmalloc(u32 bytes) { return bytes == sizeof plan ? (void *)&plan : saved_text; }
void kfree(void *p) { (void)p; }
struct SplashRaster *__wrap_pc98_splash_raster_state(void) { return &raster; }
int __wrap_pl_run_timing(const struct SplashRaster *s) { return s != &raster; }
int sys_time_now(u32 *lo, u32 *hi) { *lo = sample; *hi = 0; return 0; }
enum boundary_input { EMPTY, ESCAPE, ABORT, EXTEND };
static enum boundary_input boundary;
static int key = -1;
int gfx_osri_poll_abort(int caller)
{
    (void)caller;
    return (!image.count && frames == 3) || (boundary == ABORT && serviced) ||
           (boundary == EXTEND && serviced == 2);
}
int con_sink_is_enabled(void) { return 0; }
int input_trygetrawkey(void) { return -1; }
int input_peekkey(void) { return key; }
int input_trygetkey(void) { int ch = key; key = -1; return ch; }
void palette_set(int i, u8 r, u8 g, u8 b) { palette[i] = r; (void)g; (void)b; }
void palette_get(int i, u8 *r, u8 *g, u8 *b) { *r = *g = *b = palette[i]; }
void gfx_add_dirty_rect(int x, int y, int w, int h)
{ if (x || y || w != GFX_WIDTH || h != GFX_HEIGHT) ++bad; }
void gfx_present_nosync(void) { ++presented; }
void gfx_clear_planar_pages(u32 size) { if (size == GFX_PLANE_SZ) ++cleared; }
static void service(void)
{
    if (input_test_irq || frames != raster.frame) ++bad;
    ++serviced;
    if (serviced == 1 && boundary == ESCAPE) key = OSRI_ESC_ASCII;
    if (serviced == 1 && boundary == EXTEND) key = 'a';
    ++tick_count;
}

/* The GDC supplies one new VSYNC then 400 pairs of HBLANK edges.
 * The planner and raster player perform the actual port writes. */
unsigned osri_status(unsigned port)
{
    if (port != GDC_GFX_STAT) __builtin_trap();
    unsigned length = image.count ? 5 + 2 * SPLASH_RASTER_VISIBLE_LINES : 2;
    unsigned phase = sample++ % length;
    if (!phase) {
        if (++frames != serviced || frames > 3 || input_test_irq) __builtin_trap();
        if (!presented || attributes[0] || palette[1]) ++bad;
    }
    if (phase == 1 || (image.count && (phase == 2 || phase == 3)))
        return GDC_STAT_VSYNC;
    return phase >= 5 && (phase - 5) % 2 ? GDC_STAT_HBLANK : 0;
}

void osri_out(unsigned port, unsigned value)
{
    if (port == GDC_ACCESS_PAGE) return;
    unsigned pos = writes++ % (image.count ? 68 : 64);
    if (pos < 64) {
        const unsigned ports[] = {PAL_IDX_PORT, PAL_G_PORT, PAL_R_PORT, PAL_B_PORT};
        if (port != ports[pos % 4] || value != (pos % 4 ? 0 : pos / 4)) ++bad;
    } else {
        const unsigned ports[] = {PAL_IDX_PORT, PAL_G_PORT, PAL_R_PORT, PAL_B_PORT};
        const unsigned values[] = {4, 2, 1, 3};
        if (!input_test_irq || port != ports[pos - 64] || value != values[pos - 64]) ++bad;
        ++row_writes;
    }
    if (port == PAL_IDX_PORT) selected = value;
    else if (selected >= OSRI_COLORS) ++bad;
}
int probe(void)
{
    if (idle_work_register(service)) return 1;
    raster.total_lines = SPLASH_RASTER_VISIBLE_LINES;
    raster.line_num_us = SPLASH_RASTER_LINE_NUM_US;
    raster.line_den = SPLASH_RASTER_24K_DEN;
    for (unsigned dynamic = 0; dynamic < 2; ++dynamic) {
        image.count = dynamic;
        image.version = OSRI_VERSION_2;
        image.band = 1;
        image.event[0] = (struct OsriEvent){.line = 1, .index = 4, .rgb444 = 0x123};
        raster.frame = sample = writes = row_writes = 0;
        palette[1] = 7;
        attributes[0] = 0x55;
        frames = serviced = cleared = presented = 0;
        struct OsriViewRun ctl = {.duration_ms = 50, .start = tick_count};
        int rc = pc98_osri_show(&image, &ctl);
        if (rc != (dynamic ? GFX_OSRI_SHOW_DONE : GFX_OSRI_SHOW_CANCELLED) ||
            bad || frames != 3 || raster.frame != 3 || writes != 3 * (dynamic ? 68 : 64) ||
            row_writes != (dynamic ? 12 : 0) || cleared != 1 || presented != 1 ||
            attributes[0] != 0x55 || palette[1] != 7) return 2;
    }
    for (boundary = EMPTY; boundary <= EXTEND; ++boundary) {
        raster.frame = frames = serviced = sample = writes = row_writes = 0;
        struct OsriViewRun ctl = {.duration_ms = 20, .start = tick_count};
        int rc = pc98_osri_show(&image, &ctl);
        if (frames != (boundary == EXTEND ? 1u : 0u) || bad ||
            rc != (boundary == EMPTY ? OS32_ERR_AGAIN : GFX_OSRI_SHOW_CANCELLED))
            return 3 + boundary;
    }
    return 0;
}
