#define _start unused_runtime_start
#include "module_runtime_host.c"
#undef _start
#ifndef CIRRUS_ADAPTER
#define CIRRUS_ADAPTER "../../drivers/video/cirrus/cirrus_module.c"
#endif
#include CIRRUS_ADAPTER
#ifndef CIRRUS_BODY
#define CIRRUS_BODY "../../drivers/video/cirrus/cirrus_body.c"
#endif
#include CIRRUS_BODY
GfxCounters gfx_counters;
int gfx_current_height, gfx_flip_enabled, gfx_display_page;
GfxBackend gfx_backend_pc98;
const GfxBackend *g_backend = &gfx_backend_pc98;
static u32 probes, enables, mode;
static int probe(void) { probes++; return 1; }
static void enable(void) { enables++; }
static int query(GFX_ScreenInfo *s) { (void)s; return 0; }
static void present(int x, int y, int w, int h) { (void)x; (void)y; (void)w; (void)h; }
static void palette(int x, int n, const u8 *p) { (void)x; (void)n; (void)p; }
static int fill(int x, int y, int w, int h, u8 c) { present(x,y,w,h); (void)c; return 0; }
static int blit(int x, int y, int sx, int sy, int w, int h) { present(x,y,w,h); (void)sx; (void)sy; return 0; }
static const GfxMode fixture_modes[] = {
    {640, 480, GFX_FMT_PACKED8, 6, 0, 0, 640},
    {640, 400, GFX_FMT_PLANAR4, 4, 0, 0, 80}
};
GfxBackend cirrus_backend_ops = {
    .name = "fixture", .probe = probe, .init = enable, .query = query,
    .shutdown = enable, .present_rect = present, .set_palette = palette,
    .enter = enable, .leave = enable, .fill_rect = fill, .blit = blit,
    .modes = fixture_modes, .mode_count = 2, .default_mode = 1,
    .selected_mode = &fixture_modes[0]
};
static int start_then_fail(struct module_record *r, u32 entry, u32 *ops)
{
    int rc = cirrus_start(r, entry, ops);
    CHECK(!probes && !enables); /* GUI=0 registration is not a probe/enable. */
    CHECK(gfx_backend_cirrus.probe == missing_probe); /* Not published yet. */
    if (mode == 1) return MODULE_INIT_FAILED;
    return rc;
}
void _start(void)
{
    read_input(input, sizeof(input)); read_input(file, sizeof(file));
    mode = input[1];
    kmemcpy(&imports[0].name_hash, file + 152, 4);
    kmemcpy(&exports[4].name_hash, file + 164, 4);
    exports[4].version = 1;
    struct module_request q = request();
    q.lifecycle = (struct module_lifecycle){.start = start_then_fail, .detach = cirrus_detach};
    CHECK(!gfx_backend_cirrus.probe());
    CHECK(!module_load(&q, &cirrus_handle));
    struct module_record *r = module_record_get(cirrus_handle);
    r->init = (u32)(uptr)cirrus_module_init;
    r->text = (u32)(uptr)probe & ~(PAGE_SIZE - 1);
    r->text_size = 16 * PAGE_SIZE;
    int rc = module_start(cirrus_handle);
    if (mode == 1 || mode == 3) {
        CHECK(rc == MODULE_INIT_FAILED && !registered && !r->registration_refs);
        CHECK(gfx_backend_cirrus.probe == missing_probe && !enables && !probes);
        empty(); finish(0);
    }
    CHECK(!rc && registered && r->registration_refs == 1);
    gfx_backend_cirrus = candidate; /* Same publication as cirrus_module_load. */
    CHECK(cirrus_context->backend == &gfx_backend_cirrus);
    CHECK(gfx_backend_cirrus.modes == fixture_modes && gfx_backend_cirrus.mode_count == 2);
    CHECK(gfx_backend_cirrus.default_mode == 1 &&
          gfx_backend_mode(&gfx_backend_cirrus) == &fixture_modes[0]);
    CHECK(!enables && !probes);
    CHECK(gfx_backend_cirrus.probe());
    gfx_backend_cirrus.init();
    CHECK(enables == 1 && probes == 1);
    if (mode == 2) g_backend = &gfx_backend_cirrus;
    u32 before = returned;
    rc = cirrus_module_abort();
    if (mode == 2) {
        CHECK(rc == MODULE_ISOLATED && r->state == MODULE_QUARANTINED);
        CHECK(returned == before && registered && r->registration_refs == 1);
        CHECK(gfx_backend_cirrus.probe == probe);
    } else {
        CHECK(!rc && !registered && !r->registration_refs);
        CHECK(gfx_backend_cirrus.probe == missing_probe);
        CHECK(!gfx_backend_cirrus.modes && !gfx_backend_cirrus.mode_count &&
              !gfx_backend_cirrus.default_mode && !gfx_backend_cirrus.selected_mode);
        CHECK(!cirrus_module_abort()); empty();
    }
    finish(0);
}
