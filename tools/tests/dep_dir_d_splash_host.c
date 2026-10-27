/* Identical seam for b400acb6 and the L20 platform split. No host libc. */
#include "types.h"
#include "gfx.h"
#include "gfx_internal.h"
#include "gfx_hal.h"
#include "platform_splash.h"
#define TRACE_SIZE 24000000
static u8 trace[TRACE_SIZE];
static u32 count;
static u8 pixels[2][GVRAM_PLANES][GFX_PLANE_SZ];
static int pref, fault, generation;
volatile u32 tick_count;
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
static void bytes(const void *ptr, u32 n)
{
    const u8 *p = ptr;
    if (count + n > TRACE_SIZE) finish(2);
    while (n--) trace[count++] = *p++;
}
static void word(u32 v) { bytes(&v, sizeof(v)); }
static void event(u32 kind, u32 value) { word(kind); word(value); word(pref); }
void *kmemset(void *p, int value, u32 size)
{
    u8 *q = p;
    while (size--) *q++ = (u8)value;
    return p;
}
int gfx_get_backend_pref(void) { event(1, pref); return pref; }
void gfx_set_backend_pref(int value) { event(2, value); pref = value; }
void gfx_init(void) { event(3, 0); }
int gfx_kernel_framebuffer(struct gfx_kernel_fb *out)
{
    event(4, fault);
    out->format = fault == 2 ? GFX_BB_PACKED8 : GFX_BB_PLANAR4;
    for (int p = 0; p < GVRAM_PLANES; p++)
        out->planes[p] = fault == p + 3 ? 0 : pixels[generation][p];
    return fault == 1 ? -1 : 0;
}
void gfx_shutdown(void) { event(5, 0); }
void tvram_clear(void) { event(6, 0); }
void palette_init(void) { event(7, 0); }
void palette_set(int idx, u8 r, u8 g, u8 b)
{ event(8, idx); word(r); word(g); word(b); }
void gfx_clear_planar_pages(u32 size) { event(9, size); }
void gfx_add_dirty_rect(int x, int y, int w, int h)
{ event(10, x); word(y); word(w); word(h); }
void gfx_present_raster(GFX_RasterPalTable *table)
{
    event(11, tick_count);
    word(table->count);
    for (int i = 0; i < table->count; i++) {
        GFX_RasterPalEntry *e = &table->entries[i];
        word(e->line); word(e->pal_idx); word(e->r); word(e->g); word(e->b);
    }
    bytes(pixels[generation], sizeof(pixels[0]));
    tick_count += 100;
}
#include "splash_source.inc"
static void begin(void)
{
#ifdef BASELINE_CAPTURE
    splash_gfx_init();
    if (gfx_kernel_framebuffer(&splash_fb)) finish(3);
#else
    if (platform_splash_begin()) finish(3);
#endif
}
void _start(void)
{
    /* Full lifecycle: success, all invalid states, then retry with new pointers. */
    for (int p = 0; p < 4; p++) {
        pref = p;
        for (fault = 0; fault <= 6; fault++) {
            generation = fault & 1;
            tick_count = 0;
            boot_splash();
        }
    }
    fault = 0;
    for (generation = 0; generation < 2; generation++) {
        begin();
        for (u8 color = 0; color < 16; color++) {
            platform_splash_clear(color);
            bytes(pixels, sizeof(pixels));
        }
        static const int rects[][4] = {
            {3,2,2,1}, {5,3,20,2}, {0,0,640,400}, {-3,-2,8,4},
            {638,398,8,8}, {640,1,2,2}, {1,400,2,2}, {4,4,0,2},
            {4,4,2,-1}, {-8,1,3,1}, {7,5,2,1}, {8,6,8,1}
        };
        for (u32 i = 0; i < sizeof(rects)/sizeof(rects[0]); i++) {
            kmemset(pixels[generation], 0xa5, sizeof(pixels[0]));
            const int *r = rects[i];
            platform_splash_fill_rect(r[0],r[1],r[2],r[3], (u8)(i % 16));
            bytes(pixels, sizeof(pixels));
        }
    }
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(trace), "d"(count) : "memory");
    finish(0);
}
