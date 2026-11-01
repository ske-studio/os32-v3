/* Actual loader/owner/boot/player, generated OSRI, fake VFS and video storage. */
#define _start raster_original_start
#define sys_time_now raster_clock_now
#include "splash_raster_host.c"
#undef sys_time_now
#undef _start
int sys_time_now(u32 *lo, u32 *hi);
#include "platform_splash.h"
#include "gfx_hal.h"
#include "vfs.h"
#include "kmalloc.h"
#include "config.h"
#include "kstring.h"
#include "wire_data.h"
#include "../../gfx/gfx_osri.c"
#include "../../platform/pc98/splash.c"
#include "../../platform/pc98/splash_image.c"
#include "../../init/boot_splash.c"

static u8 wire_pixels[OSRI_PIXELS_BYTES];
static struct OsriImage wire_image;
static int wire_pref, wire_saved_pref, wire_fault, wire_invalid_fb;
static int wire_allocs, wire_frees, wire_opens, wire_closes, wire_logs;
static int wire_commits, wire_rasters, wire_clears, wire_stops, wire_text;
static int wire_loaded, wire_fixed_colors, wire_static;
static u32 wire_pos, wire_size;
static const u8 *wire_file;
volatile u32 tick_count;

int sys_time_now(u32 *lo, u32 *hi)
{
    int rc = raster_clock_now(lo, hi);
    tick_count = clock_us / 10000;
    return rc;
}
int gfx_get_backend_pref(void) { return wire_pref; }
void gfx_set_backend_pref(int pref) { wire_pref = pref; }
void gfx_init(void)
{
    REQUIRE(wire_pref == GFX_PREF_PC98);
    pc98_splash_raster_reset();
}
void gfx_shutdown(void) { wire_stops++; pc98_splash_raster_reset(); }
int gfx_kernel_framebuffer(struct gfx_kernel_fb *fb)
{
    *fb = (struct gfx_kernel_fb){ .width = GFX_WIDTH, .height = GFX_HEIGHT,
        .pitch = GFX_BPL, .format = GFX_BB_PLANAR4 };
    for (int i = 0; i < GVRAM_PLANES; i++) fb->planes[i] = wire_pixels + i * GFX_PLANE_SZ;
    if (wire_invalid_fb) fb->planes[3] = 0;
    return 0;
}
void *kmemset(void *dst, int c, u32 n)
{ u8 *p = dst; while (n--) *p++ = c; return dst; }
void *kmalloc(u32 size)
{ REQUIRE(size == sizeof(wire_image)); wire_allocs++; return wire_fault == 7 ? 0 : &wire_image; }
void kfree(void *p) { REQUIRE(p == &wire_image); wire_frees++; }
int vfs_stat(const char *path, OS32_Stat *st)
{ REQUIRE(!kstrcmp(path, SYS_SPLASH_IMAGE)); (void)st; return wire_fault == 1 ? -1 : 0; }
int vfs_open(const char *path, int flags)
{
    REQUIRE(!kstrcmp(path, SYS_SPLASH_IMAGE) && flags == O_RDONLY);
    wire_opens++; wire_pos = 0; return 3;
}
void vfs_close(int fd) { REQUIRE(fd == 3); wire_closes++; }
int vfs_read_fd(int fd, void *dst, u32 n)
{
    REQUIRE(fd == 3);
    if (wire_fault == 8) { clock_us = 4000000; tick_count = 400; }
    if (n > 37) n = 37; /* Valid repeated short reads. */
    u8 *p = dst;
    u32 got = 0;
    while (got < n && wire_pos < wire_size) {
        u8 c = wire_file[wire_pos];
        if (wire_fault == 2 && wire_pos == OSRI_PIXELS_OFF) c ^= 1;
        *p++ = c; wire_pos++; got++;
    }
    if (!got) wire_loaded = 1;
    return got;
}
int kstrcmp(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return (u8)*a - (u8)*b; }
void __cdecl kprintf(u8 attr, const char *fmt, ...)
{ (void)attr; (void)fmt; wire_logs++; }
void palette_set(int index, u8 r, u8 g, u8 b)
{ (void)index; (void)r; (void)g; (void)b; wire_fixed_colors++; }
void palette_init(void) { }
void tvram_clear(void) { wire_text++; }
void gfx_clear_planar_pages(u32 size) { REQUIRE(size == GFX_PLANE_SZ); wire_clears++; }
void gfx_add_dirty_rect(int x, int y, int w, int h)
{ REQUIRE(x == 0 && y == 0 && w == GFX_WIDTH && h == GFX_HEIGHT); }
void gfx_present_nosync(void)
{
    wire_commits++;
    REQUIRE(wire_pref == wire_saved_pref);
    if (!wire_static) {
        REQUIRE(wire_loaded && wire_pos == wire_size);
        REQUIRE(wire_pixels[0] == 0x81 && wire_pixels[GFX_PLANE_SZ] == 0x42 &&
                wire_pixels[2*GFX_PLANE_SZ] == 0x24 && wire_pixels[3*GFX_PLANE_SZ] == 0x18);
        if (wire_fault == 4) missing_v = 1;
    }
    if (!raster.dynamic_ok || wire_fault == 4) wire_static = 1;
}
void gfx_present_raster(GFX_RasterPalTable *table)
{
    wire_commits++; wire_rasters++; writes = 0;
    REQUIRE(wire_pref == wire_saved_pref);
    REQUIRE(!pc98_splash_raster_table(pc98_splash_raster_state(), table));
    tick_count = clock_us / 10000;
}
static void wire_setup(int fault)
{
    setup(1, 0);
    wire_fault = fault; wire_invalid_fb = 0;
    wire_file = wire_fixed; wire_size = sizeof(wire_fixed);
    wire_allocs = wire_frees = wire_opens = wire_closes = wire_logs = 0;
    wire_commits = wire_rasters = wire_clears = wire_stops = wire_text = 0;
    wire_loaded = wire_fixed_colors = wire_static = 0;
    wire_pos = tick_count = 0;
    if (fault == 3) wire_size--;
    if (fault == 5) scale = 1011;
    if (fault == 6) { wire_file = wire_events; wire_size = sizeof(wire_events); }
    if (fault == 9) { missing_v = 1; wire_static = 1; }
    if (fault == 10) { clock_error = 1; wire_static = 1; }
    if (fault == 11) irq_on = 0;
    kmemset(wire_pixels, 0xa5, sizeof(wire_pixels));
}
void _start(void)
{
    for (int pref = 0; pref < 4; pref++) for (int fault = 0; fault <= 11; fault++) {
        wire_setup(fault); wire_pref = wire_saved_pref = pref;
        boot_splash();
        REQUIRE(wire_pref == pref && wire_stops == 1 && wire_clears == 1 && wire_text == 1);
        REQUIRE(!raster.checked && !raster.dynamic_ok);
        REQUIRE(wire_closes == wire_opens && wire_frees == (wire_allocs && fault != 7));
        REQUIRE(irq_on == (fault != 11));
        if (fault == 1 || fault == 2 || fault == 3 || fault == 6 || fault == 7) {
            REQUIRE(wire_logs == 1 && wire_rasters > 0);
            REQUIRE(wire_pixels[0] == 0xff && wire_pixels[GFX_PLANE_SZ] == 0);
        } else if (fault == 4 || fault == 9 || fault == 10) {
            REQUIRE(wire_logs == 1 && !wire_rasters);
            REQUIRE(wire_commits <= 2 && calls < 4000000);
        } else {
            REQUIRE(!wire_logs && !wire_rasters);
            REQUIRE(wire_commits == (fault == 8 ? 0 : 1));
            if (fault != 8) REQUIRE(wire_loaded);
        }
        if (fault != 8 && fault != 10 && fault != 11) REQUIRE(tick_count >= BOOT_TIMEOUT);
        if (fault != 8) REQUIRE(tick_count <= BOOT_TIMEOUT + 3);
    }
    wire_setup(0); wire_invalid_fb = 1;
    boot_splash();
    REQUIRE(wire_stops == 1 && wire_text == 1 && !wire_allocs && !wire_commits && !wire_clears);
    finish(0);
}
