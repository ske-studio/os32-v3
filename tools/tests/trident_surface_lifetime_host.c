/* Real core, resources, ledger, lease, HAL and lifecycle. Only PCI/MMIO/ports
 * and Linux's MMU are seams; SPR and mode preparation remain product code. */
#include "trident_device_fake.h"
#include "drivers/video/trident/trident_module.h"
#define run regression_run
#define HOST_OWN_PC98_ROUTE 1
#include "lifetime_kernel_host.c"
#undef run
int lifetime_bind(struct trident_device *);
#define report tri_backend_report
#include "lifetime_backend.c"
#undef report
static struct trident_fake life_fake;
static u8 life_storage[TRI_FAKE_REAL_BYTES];
static u32 life_status;
static struct trident_io life_io;
static const struct TridentDiagnosticOps *life_diagnostic;
const TridentModuleContext *trident_context;
extern int gfx_is_started(void);
int pc98_route_safe(void) { return !device.active; }
void serial_puts(const char *s) { serial_puts_polled(s); }
int sys_time_now(u32 *lo, u32 *hi) { *lo = *hi = 0; return 0; }
static int life_read(void *opaque, enum trident_space space, u32 off, u8 *value)
{
    if (space == TRI_SPACE_REGS && off == TRI_CR_COLOR + 6)
        life_fake.regs[off] = (++life_status & 1u) ? 0 : 8;
    return trident_fake_io.read8(opaque, space, off, value);
}
int lifetime_bind(struct trident_device *d)
{
    d->io = &life_io; d->io_opaque = &life_fake; return 0;
}
u32 pci_cfg_read32(u32 bus, u32 slot_id, u32 fn, u32 reg)
{
    CHECK(bus == TRI_PCI_BUS && slot_id == TRI_PCI_SLOT && fn == TRI_PCI_FUNCTION);
    const struct trident_pci_info *p = &life_fake.resources.pci;
    switch (reg) {
    case 0: return p->vendor | ((u32)p->device << 16);
    case 4: return p->command;
    case 8: return p->revision;
    case 16: return p->bar0;
    case 20: return p->bar1;
    default: CHECK(0); return 0;
    }
}
u16 pci_cfg_read16(u32 bus, u32 dev, u32 fn, u32 reg)
{ return pci_cfg_read32(bus, dev, fn, reg & ~3u) >> ((reg & 2u) * 8); }
u8 pci_cfg_read8(u32 bus, u32 dev, u32 fn, u32 reg)
{ return pci_cfg_read32(bus, dev, fn, reg & ~3u) >> ((reg & 3u) * 8); }
static int life_owner(void) { return appslot_gfx_owner(); }
static void life_shape(u32 cpp, u32 pitch, u32 pages)
{
    const struct trident_resources *r = trident_resources_get();
    CHECK(r && r->client_surface.cpp == cpp && r->client_surface.pitch == pitch);
    CHECK(ledger_surfaces[r->display_sid].npages == pages);
    CHECK(ledger_surfaces[r->client_sid].npages == pages);
    CHECK(ledger_surfaces[r->client_sid].pitch == pitch);
    if (TRI_RAM_FALLBACK_DEFAULT) {
        CHECK(ledger_surfaces[r->client_sid].first != ledger_surfaces[r->display_sid].first);
        CHECK(ledger_surfaces[r->client_sid].backing == LEDGER_SB_RAM);
        CHECK(ledger_surfaces[r->client_sid].cache == LEDGER_CACHE_WB);
        CHECK(r->client == P2V(ledger_surfaces[r->client_sid].first * PAGE_SIZE));
    } else CHECK(ledger_surfaces[r->client_sid].first == ledger_surfaces[r->display_sid].first);
    CHECK(device.vram.first_off == pages * PAGE_SIZE);
    for (struct tri_vram_block *b = device.vram.blocks; b; b = b->next)
        CHECK(b->off >= pages * PAGE_SIZE);
}
static void run(void) __attribute__((used));
static void run(void)
{
    CHECK(!trident_fake_reset(&life_fake, life_storage, sizeof(life_storage), 0, 0));
    life_fake.cr_color[0x1F] = 0x07; /* SPR: 2 MiB */
    life_io = trident_fake_io; life_io.read8 = life_read;
    map_host(0x10000, 0xFF0000);
    map_host(MEM_TRIDENT_LFB_VA, TRI_BAR0_DECODE_BYTES);
    boot();
    gfx_set_backend_pref(GFX_PREF_TRIDENT);
    gfx_boot_reserve();
    const struct trident_resources *r = trident_resources_get();
    CHECK(r && !r->client && !ledger_surface_find(LEDGER_SF_TRIDENT, LEDGER_ROLE_CLIENT));
    static TridentModuleContext context;
    context = (TridentModuleContext){&gfx_backend_trident, &gfx_counters, r,
        gfx_arm_emergency, &gfx_current_height, &gfx_flip_enabled, &gfx_display_page,
        life_owner, TRIDENT_MODULE_CONTEXT_SIZE, &life_diagnostic, 300, 1000000};
    trident_context = &context;
    device.settings.vram_bytes = 307200u - 1u;
    gfx_prepare_backend();
    CHECK(g_backend == &gfx_backend_pc98 && !life_fake.operations);
    device.settings.vram_bytes = 0;
    gfx_prepare_backend();
    CHECK(g_backend == &gfx_backend_trident && !life_fake.operations && !device.declared_bytes);
    u32 display_sid = r->display_sid, client_sid = r->client_sid;
    CHECK(ledger_surfaces[client_sid].npages == 75);
    gfx_init();
    CHECK(gfx_is_started() && g_backend == &gfx_backend_trident && device.screen_ready);
    CHECK(r->display_sid == display_sid && r->client_sid == client_sid);
    life_shape(1, TRI_RAM_FALLBACK_DEFAULT ? 640 : 1024, TRI_RAM_FALLBACK_DEFAULT ? 300 : 480);
    gfx_init(); CHECK(gfx_is_started() && g_backend == &gfx_backend_trident);
    life_shape(1, TRI_RAM_FALLBACK_DEFAULT ? 640 : 1024, TRI_RAM_FALLBACK_DEFAULT ? 300 : 480);
    CHECK(!tri_hal_mode(3)); life_shape(2, TRI_RAM_FALLBACK_DEFAULT ? 1280 : 2048, TRI_RAM_FALLBACK_DEFAULT ? 300 : 480);
    CHECK(!tri_hal_mode(5)); life_shape(4, TRI_RAM_FALLBACK_DEFAULT ? 2560 : 4096, TRI_RAM_FALLBACK_DEFAULT ? 300 : 480);
    device.settings.no_accel = true;
    CHECK(!tri_hal_mode(3)); life_shape(2, 1280, TRI_RAM_FALLBACK_DEFAULT ? 300 : 480);
    CHECK(!device.accel.initialized);
    CHECK(!tri_hal_mode(5)); life_shape(4, 2560, TRI_RAM_FALLBACK_DEFAULT ? 300 : 480);
    device.settings.no_accel = false;
    CHECK(!tri_hal_mode(3)); life_shape(2, TRI_RAM_FALLBACK_DEFAULT ? 1280 : 2048, TRI_RAM_FALLBACK_DEFAULT ? 300 : 480);
    gfx_shutdown();
    u32 operations = life_fake.operations;
    gfx_prepare_backend();
    CHECK(g_backend == &gfx_backend_trident && life_fake.operations == operations);
    CHECK(device.mode.selected.id == TRI_MODE_DEFAULT_ID);
    /* A1 applies before init too: regen both surfaces to the built default. */
    life_shape(1, TRI_RAM_FALLBACK_DEFAULT ? 640 : 1024, TRI_RAM_FALLBACK_DEFAULT ? 300 : 480);
    CHECK(r->display.pitch == r->client_surface.pitch &&
          ledger_surfaces[display_sid].pitch == r->client_surface.pitch &&
          gfx_backend_trident.bb_pitch == r->client_surface.pitch);
    CHECK(device.mode.selected.display_width == r->client_surface.pitch);
    CHECK(r->client_surface.cpp == 1 && ledger_surfaces[client_sid].npages == (TRI_RAM_FALLBACK_DEFAULT ? 300 : 480));
    gfx_init();
    CHECK(gfx_is_started() && g_backend == &gfx_backend_trident);
    life_shape(1, TRI_RAM_FALLBACK_DEFAULT ? 640 : 1024, TRI_RAM_FALLBACK_DEFAULT ? 300 : 480);
    /* Obtain an actual application lease; the mode change must revoke it. */
    u32 owner;
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "lifetime", &owner));
    CHECK(!paging_addrspace_create_lease(&space, owner));
    payload = pgalloc_alloc_phys(owner, 1);
    CHECK(payload && !paging_addrspace_map_user(&space, MEM_EXEC_LOAD_ADDR, payload, PAGE_RW | PTE_USER));
    slot.state = APP_STATE_RUNNING; slot.cpl3 = 1; slot.as = &space;
    slot.hdr_flags = OS32X_FLAG_GFX;
    host_cr3 = space.pd_phys;
    CallerAccessFrame previous;
    CHECK(caller_access_enter(&previous, CALLER_USER));
    struct surface_query_source source;
    CHECK(!gfx_surface_source(LEDGER_ROLE_CLIENT, &source));
    *(struct surface_ref *)P2V(payload) = source.refs[0];
    struct lease_view *view = P2V(payload + 64);
    CHECK(!surface_lease(&source, (void *)MEM_EXEC_LOAD_ADDR, LEDGER_PERM_RW,
                         (void *)(MEM_EXEC_LOAD_ADDR + 64)));
    u32 token = view->token, base = view->base;
    CHECK(token && ledger_surfaces[client_sid].lease_count == 1);
    CHECK(!tri_hal_mode(3));
    CHECK(!ledger_surfaces[client_sid].lease_count && !paging_lease_pte(&space, base));
    CHECK(lease_release(&space, token) == LEASE_INVAL);
    /* The existing core shutdown path also revokes a new lease. */
    CHECK(!gfx_surface_source(LEDGER_ROLE_CLIENT, &source));
    *(struct surface_ref *)P2V(payload) = source.refs[0];
    CHECK(!surface_lease(&source, (void *)MEM_EXEC_LOAD_ADDR, LEDGER_PERM_RW,
                         (void *)(MEM_EXEC_LOAD_ADDR + 64)));
    base = view->base;
    gfx_shutdown();
    CHECK(!ledger_surfaces[client_sid].lease_count && !paging_lease_pte(&space, base));
    caller_access_leave(&previous);
    gfx_prepare_backend(); gfx_init();
    CHECK(g_backend == &gfx_backend_trident && gfx_is_started());
    CHECK(!tri_hal_mode(8)); /* RGB565, 400 lines before Close/reinit. */
    /* Active HAL reinit closes first, then prepare selects the default.
     * Never save the active Trident registers as the CUI return state. */
    gfx_backend_trident.init();
    CHECK(gfx_backend_trident.probe() && device.screen_ready && device.mode.client.height == TRI_HEIGHT);
    SAY("GREEN lifetime: real prepare/core/resources/init, reinit, modes, Close/default, offscreen, lease revoke");
    die(0);
}
