/* Real gfx core + ledger, fake HAL callbacks. Device I/O and lifecycle
 * sequences are tested separately by tri-life using the same 3a device fake. */
#include "trident_device_fake.h"
#include "gfx/gfx_emergency.h"
#define run regression_run
#include "gfx_kernel_fb_host.c"
#undef run
extern int gfx_is_started(void);

static struct trident_fake tri_fake;
static struct trident_device tri_device;
static u8 tri_storage[TRI_FAKE_REAL_BYTES];
static int reserved, tri_probes, tri_inits, tri_leaves, tri_closes, tri_returns;
static int tri_failure, route_safe = 1, recovery_failure, prepare_failure;
static u32 tri_client, tri_display;
static const GfxMode tri_modes[] = {
    {.width=TRI_WIDTH, .height=TRI_HEIGHT, .format=GFX_FMT_PACKED8,
     .dac_bits=6, .pitch=TRI_PITCH}
};
int pc98_route_safe(void) { return route_safe; }
static int tri_probe(void) { tri_probes++; return !tri_failure; }
static int tri_query(GFX_ScreenInfo *out)
{
    if (tri_failure) return tri_failure;
    if (!out) return OS32_ERR_INVAL;
    *out = (GFX_ScreenInfo){.width=TRI_WIDTH, .height=TRI_HEIGHT,
        .format=GFX_FMT_PACKED8, .bpp=8};
    return 0;
}
static int tri_restore(void *opaque)
{
    CHECK(opaque == &tri_device);
    tri_returns++; tri_device.active = false;
    route_safe = !recovery_failure;
    return recovery_failure;
}
static void tri_init(void)
{
    const struct ledger_surface *sf = &ledger_surfaces[tri_client];
    tri_inits++;
    CHECK(gfx_backend_trident.bb_base == P2V(sf->first * PAGE_SIZE));
    CHECK(gfx_backend_trident.bb_size == TRI_DISPLAY_BYTES);
    CHECK(gfx_backend_trident.bb_pitch == TRI_PITCH);
    CHECK(!gfx_arm_emergency(tri_restore, &tri_device, (u32)appslot_gfx_owner()));
    tri_device.active = true; route_safe = 0;
    if (fail_after_init) { tri_failure = -71; return; }
    ledger_surfaces[tri_client].perm_max = LEDGER_PERM_RW;
    ledger_surfaces[tri_display].perm_max = LEDGER_PERM_RW;
    gfx_current_height = TRI_HEIGHT;
}
static void tri_enter(void) { CHECK(tri_device.active); }
static void tri_leave(void) { tri_leaves++; tri_device.active = false; route_safe = 1; }
static void tri_close(void) { tri_closes++; CHECK(!tri_device.active); }
static void tri_prepare(void) { tri_failure = prepare_failure; }
GfxBackend gfx_backend_trident = {
    .name="trident-test", .probe=tri_probe, .init=tri_init, .query=tri_query,
    .enter=tri_enter, .leave=tri_leave, .shutdown=tri_close, .prepare=tri_prepare,
    .modes=tri_modes, .mode_count=1
};
/* This seam models resources.c's committed descriptors, not its transaction.
 * Real resources.c has its own host test. Verify the core invokes this seam
 * before arena freeze and does not allocate PEGC/Cirrus as co-candidates. */
int trident_boot_reserve(void)
{
    struct ledger_resource r = {
        .raw_bar=TRI_BAR0_PHYS,
        .decode_first=TRI_BAR0_PHYS/PAGE_SIZE,
        .decode_end=(TRI_BAR0_PHYS+TRI_BAR0_DECODE_BYTES)/PAGE_SIZE,
        .bus=LEDGER_BUS_PCI, .width_basis=LEDGER_WB_GLUE_CONST
    };
    u32 rid, pfn;
    reserved++;
    CHECK(pgalloc_arena_end() > MEM_PHYS_EXEC_FLOOR / PAGE_SIZE);
    CHECK(ledger_resource_add(&r, &rid));
    struct ledger_span sp = {r.decode_first, r.decode_end, LEDGER_SPAN_MMIO, rid};
    CHECK(ledger_reserve_set(LEDGER_OWNER_GFX, &sp, 1));
    CHECK(!paging_map_phys(TRI_BAR0_PHYS, TRI_BAR0_PHYS, TRI_DISPLAY_PAGES, PAGE_RW|PTE_PCD));
    CHECK(ledger_resource_set_map(rid, r.decode_first, r.decode_first+TRI_DISPLAY_PAGES));
    CHECK(pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, TRI_DISPLAY_PAGES,
          MEM_PHYS_EXEC_FLOOR/PAGE_SIZE, pgalloc_arena_end(), LEDGER_TOP_DOWN, &pfn));
    struct ledger_surface sf = {
        .first=r.decode_first, .npages=TRI_DISPLAY_PAGES, .width=TRI_WIDTH,
        .height=TRI_HEIGHT, .pitch=TRI_PITCH, .owner=LEDGER_OWNER_GFX,
        .backing=LEDGER_SB_MMIO, .backend=LEDGER_SF_TRIDENT,
        .role=LEDGER_ROLE_DISPLAY, .format=GFX_FMT_PACKED8, .planes=1,
        .cache=LEDGER_CACHE_UC, .perm_max=LEDGER_PERM_NONE
    };
    CHECK(ledger_surface_create(&sf, &tri_display));
    sf.first=pfn; sf.owner=LEDGER_OWNER_BOOT; sf.backing=LEDGER_SB_RAM;
    sf.role=LEDGER_ROLE_CLIENT; sf.cache=LEDGER_CACHE_WB;
    CHECK(ledger_surface_create(&sf, &tri_client));
    return 0;
}
static void run(void) __attribute__((used));
static void run(void)
{
    CHECK(!trident_fake_reset(&tri_fake, tri_storage, sizeof(tri_storage), 0, 0));
    trident_fake_bind(&tri_fake, &tri_device);
    CHECK(!tri_device.active && !tri_device.init_done && !tri_fake.pc98_initialized);
    map_host(0x10000,0xFF0000);
    boot();
    gfx_set_backend_pref(GFX_PREF_TRIDENT);
    CHECK(gfx_get_backend_pref() == GFX_PREF_TRIDENT);
    gfx_boot_reserve();
    CHECK(reserved == 1 && !tri_probes && !tri_inits);
    CHECK(!ledger_surface_find(LEDGER_SF_PEGC,LEDGER_ROLE_CLIENT));
    CHECK(!ledger_surface_find(LEDGER_SF_CIRRUS,LEDGER_ROLE_CLIENT));
    gfx_set_backend_pref(GFX_PREF_AUTO); gfx_prepare_backend();
    CHECK(!tri_probes && g_backend == &gfx_backend_pc98);
    gfx_set_backend_pref(GFX_PREF_TRIDENT); gfx_prepare_backend();
    CHECK(g_backend == &gfx_backend_trident && !tri_inits);
    CHECK(gfx_sf_backend() == LEDGER_SF_TRIDENT);
    CHECK(gfx_backend_pc98.bb_size == MEM_GFX_BB_SIZE);
    struct surface_query_source source;
    CHECK(gfx_surface_source(LEDGER_ROLE_CLIENT, &source) == OS32_ERR_INVAL);
    host_user = 0; gfx_init();
    CHECK(g_backend == &gfx_backend_trident && gfx_is_started() && tri_inits == 1);
    CHECK(gfx_selected_selfcheck());
    CHECK(!gfx_surface_source(LEDGER_ROLE_CLIENT, &source) && source.backend == LEDGER_SF_TRIDENT);
    CHECK(!gfx_surface_source(LEDGER_ROLE_DISPLAY, &source) && source.count == 1);
    gfx_shutdown();
    CHECK(!gfx_is_started() && tri_leaves == 1 && tri_closes == 1);
    gfx_init(); CHECK(gfx_is_started() && tri_inits == 2);
    gfx_shutdown();
    prepare_failure = -69; gfx_prepare_backend();
    CHECK(g_backend == &gfx_backend_pc98 && tri_failure == -69);
    prepare_failure = tri_failure = 0;
    fail_after_init = 1; gfx_init();
    CHECK(g_backend == &gfx_backend_pc98 && gfx_is_started() && tri_returns == 1);
    CHECK(gfx_backend_pc98.bb_base == bb[0] && gfx_current_height == GFX_HEIGHT);
    gfx_shutdown();
    tri_failure = 0; recovery_failure = -72; gfx_init();
    CHECK(g_backend == &gfx_backend_trident && !gfx_is_started() && tri_returns == 2);
    CHECK(!route_safe);
    SAY("GREEN core: forced/auto, boot exclusivity, SF5/CLIENT, prepare/init failure, recovery failure");
    die(0);
}
