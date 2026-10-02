/* Real splash + gfx dispatch + PC98 lifecycle; only hardware edges stubbed. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#undef strchr

#define __cdecl
#define IO_H
static unsigned int inp(unsigned int port) { return port == 0x60 ? 0x20 : 0; }
static unsigned int irq_save(void) { return 0; }
static void irq_restore(unsigned int flags) { (void)flags; }
static void outp(unsigned int port, unsigned int value);
#include "../../gfx/gfx_core.c"
#include "../../gfx/backend_pc98.c"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable" /* existing bottom in gfx_vram.c */
#include "../../gfx/gfx_vram.c"
#pragma GCC diagnostic pop
#include "../../kernel/boot_splash.c"

volatile u32 tick_count;
int vram_scroll_y;
static int cirrus_probes, pegc_probes, optional_inits;
static int cirrus_ok = 1, pegc_ok = 1, init_fail;
static int raster_frames, text_clears, stops, starts;
static int expected_pref;
static int native_state_fault;
static unsigned char pages[2][4][GFX_PLANE_SZ];
static unsigned int access_page;
static unsigned int painted_pages;
static const u32 planes[4] = { VRAM_PLANE_B, VRAM_PLANE_R, VRAM_PLANE_G, VRAM_PLANE_I };
static void bank_sync(void)
{
    for (int p = 0; p < 4; p++)
        memcpy(pages[access_page][p], (void *)(unsigned long)planes[p], GFX_PLANE_SZ);
}
static void check_clear(const char *reason, int plane_size)
{
    bank_sync();
    for (int page = 0; page < 2; page++)
        for (int p = 0; p < 4; p++)
            for (int i = 0; i < plane_size; i++)
                if (pages[page][p][i]) {
                    fprintf(stderr, "FAIL: %s page=%d plane=%d byte=%d\n", reason, page, p, i);
                    exit(1);
                }
}

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
    if (port == GDC_ACCESS_PAGE) {
        bank_sync();
        access_page = value;
        for (int p = 0; p < 4; p++)
            memcpy((void *)(unsigned long)planes[p], pages[access_page][p], GFX_PLANE_SZ);
    }
    if (port == GDC_DISP_PAGE && gfx_flip_enabled) {
        bank_sync();
        for (int p = 0; p < 4; p++)
            for (int i = 0; i < GFX_PLANE_SZ; i++)
                if (pages[value][p][i]) painted_pages |= 1U << value;
    }
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
/* 起動時の ⑥ / ⑨ (gfx_boot_reserve / gfx_client_to_gshell、TASK_T1_LEDGER
 * §3-8) が引く台帳と写像。このハーネスは ⑥ を走らせない (試験は
 * tools/tests/test_gfx_boot.py)。描画用 CLIENT だけ実型板から返す。 */
struct ledger_surface ledger_surfaces[LEDGER_MAX_SURFACES];
struct ledger_surface *ledger_surface_find(u32 b, u32 r)
{
    static struct ledger_surface client;
    if (b != LEDGER_SF_PC98 || r != LEDGER_ROLE_CLIENT) return 0;
    client = gfx_sf[0];
    if (native_state_fault && starts) client.planes = 2;
    return &client;
}
int ledger_surface_transfer(u32 sid, u32 to) { (void)sid; (void)to; return 0; }
int ledger_surface_create(const struct ledger_surface *sf, u32 *sid)
{ (void)sf; (void)sid; return 0; }
int ledger_resource_add(const struct ledger_resource *rec, u32 *rid)
{ (void)rec; (void)rid; return 0; }
int ledger_reserve_set(u32 o, const struct ledger_span *s, u32 n)
{ (void)o; (void)s; (void)n; return 0; }
int ledger_selfcheck(const char *tag) { (void)tag; return 1; }
void ledger_arena_freeze(void) { }
u32 ledger_arena_top(void) { return 0; }
u32 pgalloc_arena_end(void) { return 0; }
int pgalloc_alloc_n_owner(u32 o, int n, u32 f, u32 e, u32 d, u32 *p)
{ (void)o; (void)n; (void)f; (void)e; (void)d; (void)p; return 0; }
int paging_map_phys(u32 v, u32 p, u32 n, u32 f) { (void)v; (void)p; (void)n; (void)f; return -1; }
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
void palette_init(void) { }
void palette_set(int idx, u8 r, u8 g, u8 b)
{ (void)idx; (void)r; (void)g; (void)b; }
void gfx_scroll_init(void)
{
    /* After GDC START, ledger fixture omits the last two planes. */
}
void tvram_clear(void) { text_clears++; }
void cpu_delay_us(u32 us)
{
    (void)us;
    CHECK(g_backend == &gfx_backend_pc98, "raster uses native PC98");
    CHECK(gfx_get_backend_pref() == expected_pref,
          "configured GUI preference restored before drawing");
    raster_frames++;
    tick_count++;
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
    if (argc == 3 && (!strcmp(argv[2], "init400") || !strcmp(argv[2], "init200"))) {
        memset(pages, 0xA5, sizeof(pages));
        for (int p = 0; p < 4; p++)
            memset((void *)(unsigned long)planes[p], 0xA5, GFX_PLANE_SZ);
        gfx_set_backend_pref(GFX_PREF_PC98);
        if (!strcmp(argv[2], "init200")) {
            gfx_init_200();
            check_clear("gfx_init_200 residual", GFX_PLANE_SZ_200);
        } else {
            gfx_init();
            check_clear("gfx_init residual", GFX_PLANE_SZ);
        }
        gfx_shutdown();
        puts("PASS: native init clears both pages");
        return 0;
    }
    gfx_set_backend_pref(pref);
    if (argc == 3) {
        native_state_fault = 1;
        boot_splash();
        CHECK(starts == 1 && !raster_frames, "incomplete planes after GDC start skip drawing");
        CHECK(stops == 1 && !gfx_flip_enabled && text_clears == 1,
              "invalid native state returns to text");
        CHECK(gfx_get_backend_pref() == pref, "failed boot preserves preference");
        CHECK(!cirrus_probes && !pegc_probes && !optional_inits,
              "failed boot never tries optional devices");
        native_state_fault = 0;
        stops = starts = text_clears = 0;
    }
    boot_splash();
    CHECK(painted_pages == 3, "real raster transfer painted both pages");
    check_clear("residual logo", GFX_PLANE_SZ);
    CHECK(cirrus_probes == 0 && pegc_probes == 0 && optional_inits == 0,
          "boot must not probe or initialize optional devices");
    CHECK(starts == 1 && raster_frames > 0, "boot actually renders native splash");
    CHECK(stops == 1 && text_clears == 1, "boot returns to text");
    CHECK(!gfx_flip_enabled && gfx_display_page == 0 &&
          gfx_current_height == GFX_HEIGHT, "shutdown restores native state");
    CHECK(gfx_get_backend_pref() == pref, "boot preserves GUI configuration");
    boot_splash();
    check_clear("residual logo", GFX_PLANE_SZ);
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
