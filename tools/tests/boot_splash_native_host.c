/* Real splash + gfx dispatch + PC98 lifecycle; only hardware edges stubbed. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#undef strchr

#define __cdecl
#define IO_H
static unsigned int inp(unsigned int port) { (void)port; return 0; }
static void outp(unsigned int port, unsigned int value);
#include "../../gfx/gfx_core.c"
#include "../../gfx/backend_pc98.c"
#include "../../kernel/boot_splash.c"

volatile u32 tick_count;
static int cirrus_probes, pegc_probes, optional_inits;
static int cirrus_ok = 1, pegc_ok = 1, init_fail;
static int raster_frames, text_clears, stops, starts;
static int expected_pref;
static int native_state_fault;

#define CHECK(c, msg) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s (line %d)\n", msg, __LINE__); exit(1); \
} } while (0)

static int cirrus_probe_stub(void) { cirrus_probes++; return cirrus_ok; }
static int pegc_probe_stub(void) { pegc_probes++; return pegc_ok; }
static void optional_init(void)
{
    optional_inits++;
    if (init_fail) { cirrus_ok = 0; pegc_ok = 0; }
}
GfxBackend gfx_backend_cirrus = {
    .name = "test-cirrus", .probe = cirrus_probe_stub,
    .init = optional_init, .bb_format = GFX_BB_PACKED8
};
GfxBackend gfx_backend_pegc = {
    .name = "test-pegc", .probe = pegc_probe_stub,
    .init = optional_init, .bb_format = GFX_BB_PACKED8
};
static void outp(unsigned int port, unsigned int value)
{
    if (port == GDC_GFX_CMD && value == GDC_CMD_STOP) stops++;
    if (port == GDC_GFX_CMD && value == GDC_CMD_START) starts++;
}
void *kmemset(void *dst, int val, u32 n) { return memset(dst, val, n); }
/* 票 T8 の門 (gfx_kapi_init / gfx_kapi_init_200 / gfx_screen_owner) が引く
 * カーネル側の 3 本。このハーネスが見るのはバックエンドの選択と 9801 の
 * ライフサイクルで、画面の所有者は対象外 (そちらは
 * tools/tests/multiapp_impl_host.c ケース 20 が実物の exec/appslot.c で見る)。
 * なので「CUI 中 / 誰も所有していない」= 門が素通しになる値を返す。 */
int con_sink_is_enabled(void) { return 0; }
int appslot_gfx_claim(int gui_mode) { (void)gui_mode; return 0; }
int appslot_gfx_owner(void) { return 1; }   /* APP_ID_SHELL = GFX_OWNER_WM */
/* 票 T8-2 で門が拒否の理由を端末へ出すようになった (claim が常に通る上の
 * スタブでは呼ばれないが、リンクには要る)。 */
void shell_print(const char *str, u8 color) { (void)str; (void)color; }
void palette_init(void) { }
void palette_set(int idx, u8 r, u8 g, u8 b)
{ (void)idx; (void)r; (void)g; (void)b; }
void gfx_scroll_init(void)
{
    /* Inject a software state failure after the real native init body. */
    if (native_state_fault) bb[2] = (u8 *)0;
}
void tvram_clear(void) { text_clears++; }
void gfx_add_dirty_rect(int x, int y, int w, int h)
{ (void)x; (void)y; (void)w; (void)h; }
void gfx_present_dirty(void) { }
void gfx_present(void) { }
void gfx_present_raster(GFX_RasterPalTable *table)
{
    (void)table;
    CHECK(g_backend == &gfx_backend_pc98, "raster uses native PC98");
    CHECK(gfx_get_backend_pref() == expected_pref,
          "configured GUI preference restored before drawing");
    raster_frames++;
    tick_count += 100;
}
static void map_region(u32 base, u32 size)
{
    void *p = mmap((void *)base, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    CHECK(p != MAP_FAILED, "host maps simulated native memory");
}
int main(int argc, char **argv)
{
    int pref;
    const GfxBackend *want;
    CHECK(argc == 2 || argc == 3, "preference argument");
    pref = atoi(argv[1]);
    expected_pref = pref;
    map_region(MEM_GFX_BB_BASE, MEM_GFX_BB_SIZE);
    map_region(TVRAM_BASE, VRAM_PLANE_B - TVRAM_BASE);
    map_region(VRAM_PLANE_B, 0x18000);
    map_region(VRAM_PLANE_I, 0x8000);
    gfx_set_backend_pref(pref);
    if (argc == 3) {
        native_state_fault = 1;
        boot_splash();
        CHECK(!raster_frames, "invalid native buffer skips drawing");
        CHECK(stops == 1 && !gfx_flip_enabled && text_clears == 1,
              "invalid native state returns to text");
        CHECK(gfx_get_backend_pref() == pref, "failed boot preserves preference");
        CHECK(!cirrus_probes && !pegc_probes && !optional_inits,
              "failed boot never tries optional devices");
        native_state_fault = 0;
        stops = starts = text_clears = 0;
    }
    boot_splash();
    CHECK(cirrus_probes == 0 && pegc_probes == 0 && optional_inits == 0,
          "boot must not probe or initialize optional devices");
    CHECK(starts == 1 && raster_frames > 0, "boot actually renders native splash");
    CHECK(stops == 1 && text_clears == 1, "boot returns to text");
    CHECK(!gfx_flip_enabled && gfx_display_page == 0 &&
          gfx_current_height == GFX_HEIGHT, "shutdown restores native state");
    CHECK(gfx_get_backend_pref() == pref, "boot preserves GUI configuration");
    boot_splash();
    CHECK(starts == 2 && stops == 2, "repeat boot lifecycle");
    CHECK(!cirrus_probes && !pegc_probes, "repeat boot never probes options");
    want = pref == GFX_PREF_PC98 ? &gfx_backend_pc98 :
           pref == GFX_PREF_PEGC ? &gfx_backend_pegc : &gfx_backend_cirrus;
    gfx_init();
    CHECK(g_backend == want, "later GUI honors configured selection");
    gfx_shutdown();
    gfx_init();
    CHECK(g_backend == want, "GUI selection survives shutdown/reinit");
    gfx_shutdown();
    cirrus_ok = pegc_ok = 0;
    gfx_init();
    CHECK(g_backend == &gfx_backend_pc98, "optional probe failure falls back");
    gfx_shutdown();
    cirrus_ok = pegc_ok = 1;
    init_fail = 1;
    gfx_init();
    CHECK(g_backend == &gfx_backend_pc98, "optional init failure falls back");
    gfx_shutdown();
    CHECK(gfx_get_backend_pref() == pref, "failure preserves preference");
    puts("PASS: native boot, preference, shutdown, repeat, optional failures");
    return 0;
}
