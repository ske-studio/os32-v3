/* Diagnostic boot probe executes against the same rational scan model. */
#define _start raster_original_start
#include "splash_raster_host.c"
#undef _start
#include "gfx_hal.h"
#include "bootinfo.h"
#include "idt.h"
static u8 probe_fake_bios = PEGC_BIOS_PRXCRT_31KHZ;
#define P2V_IO(address) (&probe_fake_bios)
static int probe_frame_checked(struct SplashRaster *s, const GFX_RasterPalTable *t);
#define pc98_splash_raster_table probe_frame_checked
#include "../../platform/pc98/splash_probe.c"
#undef pc98_splash_raster_table

volatile u32 tick_count;
u32 ktime_clamp_count;
const char os32_build_commit[] = "host-probe";
static struct bootinfo probe_bootinfo = { .img_crc = 123, .source = 2 };
static struct pit_setup probe_pit = { .reload = 24576 };
static int probe_presents, probe_rects, probe_rx_pos;
static u32 probe_checked_frames;
static char probe_output[65536];
static u32 probe_output_size;
int gfx_current_height = 400, gfx_flip_enabled = 1;
const struct bootinfo *bootinfo_get(void) { return &probe_bootinfo; }
const struct pit_setup *pit_get_setup(void) { return &probe_pit; }
int serial_gate_active(void) { return 0; }
void serial_puts_polled(const char *p)
{
    REQUIRE(probe_frames[PROBE_FRAMES-1].duration || probe_errors);
    REQUIRE(irq_on);
    while (*p) { REQUIRE(probe_output_size < sizeof(probe_output)-1); probe_output[probe_output_size++] = *p++; }
    probe_output[probe_output_size] = 0;
}
int serial_get_status(u32 *mode, u32 *baud, u32 *fifo)
{ *mode = 0; *baud = 9600; *fifo = 1; return 0; }
void serial_diag_get(u32 *oe, u32 *fe, u32 *pe, u32 *overflow)
{ *oe = *fe = *pe = *overflow = 0; }
int serial_trygetchar(void)
{
    if (probe_rx_pos >= 8 * 4) return -1;
    int i = probe_rx_pos++;
    return i % 4 ? 0 : i / 4;
}
void platform_splash_clear(u8 c) { REQUIRE(c == 0); }
void platform_splash_fill_rect(int x, int y, int w, int h, u8 c)
{
    REQUIRE(x >= 0 && y >= 0 && w > 0 && h > 0 && x+w <= 640 && y+h <= 400 && c < 16);
    probe_rects++;
}
void gfx_add_dirty_rect(int x, int y, int w, int h)
{ REQUIRE(x == 0 && y == 0 && w == 640 && h == 400); }
void gfx_present_nosync(void) { probe_presents++; }
void palette_set(int index, u8 r, u8 g, u8 b) { color(index, r, g, b); }
static int probe_contains(const char *s)
{
    for (u32 i = 0; i < probe_output_size; i++) {
        u32 j = 0;
        while (s[j] && s[j] == probe_output[i+j]) j++;
        if (!s[j]) return 1;
    }
    return 0;
}
static int probe_frame_checked(struct SplashRaster *s, const GFX_RasterPalTable *t)
{
    int first = probe_checked_frames < 9 ? 8 + (int)probe_checked_frames - 4 : 4;
    REQUIRE(t->entries[1].line == first);
    REQUIRE(t->entries[0].r == 15 && t->entries[0].g == 0);
    REQUIRE(t->entries[1].r == 0 && t->entries[1].g == 15);
    for (int i = 2; i < t->count; i++) {
        REQUIRE(t->entries[i].line == t->entries[i-1].line + 4);
        REQUIRE(t->entries[i].r != t->entries[i-1].r);
    }
    if (probe_checked_frames >= 9) REQUIRE(t->entries[t->count-1].line == 396);
    probe_checked_frames++;
    return pc98_splash_raster_table(s, t);
}
void _start(void)
{
    for (int mode = 0; mode < 2; mode++) {
        setup(mode, 0);
        probe_output_size = probe_presents = probe_rects = probe_rx_pos = 0;
        probe_checked_frames = 0;
        platform_splash_probe_before();
        platform_splash_probe();
        REQUIRE(!probe_errors && probe_rx_count == 8 && !probe_rx_missing && !probe_rx_bytes);
        REQUIRE(probe_presents == 1 && probe_rects > 1000);
        REQUIRE(probe_frames[PROBE_FRAMES-1].duration > 0);
        REQUIRE(probe_frames[PROBE_FRAMES-1].closed_total > 0);
        REQUIRE(probe_contains("sample=clock_read_us n=128"));
        REQUIRE(probe_contains("sample=vsync_us n=32"));
        REQUIRE(probe_contains("sample=hperiod_us n=64"));
        REQUIRE(probe_contains(" begin_us=") && probe_contains(" end_us="));
        REQUIRE(probe_contains("errors=0") && probe_contains("rx_words=8"));
        REQUIRE(probe_contains("commit=host-probe crc=123"));
        REQUIRE(irq_on);
    }
    setup(1, 0); missing_h = 1;
    platform_splash_probe();
    REQUIRE(probe_errors && irq_on);
    finish(0);
}
