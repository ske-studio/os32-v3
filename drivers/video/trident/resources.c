/* Core boot transaction, TRIDENT_S3_DESIGN §3 / BARLEN_DESIGN §2.
 * X.Org trident_driver-1.4.0.c:1564-1629 maps PCI framebuffer/MMIO;
 * :520,564 skip MMIO enable/disable for PCI PC-98. No display I/O here.
 * The Ra266 bounds are NOT measured VRAM capacity. No BAR sizing writes.
 */
#include "trident.h"
#include "drivers/pci/pci.h"
#include "mm/pgalloc.h"
#include "gfx_hal.h"
#include "kprintf.h"
#include "lib/kstring.h"

#define TRI_BAR_COUNT 2u
#define TRI_BDF ((TRI_PCI_BUS << 8) | (TRI_PCI_SLOT << 3) | TRI_PCI_FUNCTION)

static struct trident_resources resources;
static bool attempted, client_allocated;
static bool mapped[TRI_BAR_COUNT], surface_created[TRI_BAR_COUNT];
static u32 rids[TRI_BAR_COUNT], client_pfn;
static int boot_rc;
static u32 bases[TRI_BAR_COUNT] = {MEM_TRIDENT_LFB_VA, MEM_TRIDENT_REGS_VA};
static const u32 decodes[TRI_BAR_COUNT] = {TRI_BAR0_DECODE_BYTES, TRI_BAR1_DECODE_BYTES};
static const u32 maps[TRI_BAR_COUNT] = {TRI_BAR0_DECODE_BYTES, TRI_REG_MAPPED_BYTES};

static bool boot_context(void)
{
    return paging_current_cr3() == paging_kernel_pd_phys() &&
           !paging_live_addrspaces() && !kctx_irq_depth && !kctx_exc_depth;
}

const struct trident_resources *trident_resources_get(void)
{
    return resources.ready ? &resources : 0;
}

int trident_resources_rollback(bool armed)
{
    if (armed) return TRI_E_BUSY;
    if (!boot_context()) return TRI_E_STATE;
    /* No lease may outlive backing or mapping. 3b keeps both surfaces private. */
    u32 sids[TRI_BAR_COUNT] = {resources.display_sid, resources.client_sid};
    for (u32 i = 0; i < TRI_BAR_COUNT; ++i)
        if (surface_created[i] && ledger_surfaces[sids[i]].lease_count) return TRI_E_BUSY;
    resources.ready = false;
    for (u32 i = TRI_BAR_COUNT; i-- > 0;) {
        if (!surface_created[i]) continue;
        if (!ledger_surface_release(sids[i])) return TRI_E_SURFACE;
        surface_created[i] = false;
        if (i == 1) client_allocated = false; /* release frees RAM backing */
    }
    if (client_allocated) {
        if (!pgalloc_free_n_owner(LEDGER_OWNER_BOOT, client_pfn, TRI_DISPLAY_PAGES))
            return TRI_E_RESOURCE;
        client_allocated = false;
    }
    resources.client = 0;
    resources.client_bytes = 0;
    for (u32 i = TRI_BAR_COUNT; i-- > 0;) {
        if (!mapped[i]) continue;
        if (paging_set_not_present(bases[i], bases[i] + maps[i] - 1)) return TRI_E_MAP;
        if (!ledger_resource_set_map(rids[i], 0, 0)) return TRI_E_RESOURCE;
        mapped[i] = false;
    }
    resources.regs = resources.lfb = 0;
    resources.mapped_bytes = resources.regs_bytes = 0;
    if (attempted) boot_rc = TRI_E_STATE;
    return 0; /* DEVICE owner, records and decode reservations survive */
}

static int fail(const char *stage, int rc)
{
    int cleanup = trident_resources_rollback(false);
    kprintf(KLOG_ATTR, "[trident] resources stage=%s rc=%d rollback=%d\n", stage, rc, cleanup);
    boot_rc = rc;
    return rc;
}

int __attribute__((cold)) trident_boot_reserve(void)
{
    if (!boot_context()) return TRI_E_STATE;
    if (attempted) return boot_rc;
    attempted = true;
    resources.pci = (struct trident_pci_info){
        .vendor = pci_cfg_read16(TRI_PCI_BUS, TRI_PCI_SLOT, TRI_PCI_FUNCTION, PCI_CFG_VENDOR_ID),
        .device = pci_cfg_read16(TRI_PCI_BUS, TRI_PCI_SLOT, TRI_PCI_FUNCTION, PCI_CFG_DEVICE_ID),
        .command = pci_cfg_read16(TRI_PCI_BUS, TRI_PCI_SLOT, TRI_PCI_FUNCTION, PCI_CFG_COMMAND),
        .revision = pci_cfg_read8(TRI_PCI_BUS, TRI_PCI_SLOT, TRI_PCI_FUNCTION, PCI_CFG_REVISION_ID),
        .bus = TRI_PCI_BUS, .slot = TRI_PCI_SLOT, .function = TRI_PCI_FUNCTION,
        .bar0 = pci_cfg_read32(TRI_PCI_BUS, TRI_PCI_SLOT, TRI_PCI_FUNCTION, PCI_CFG_BAR0),
        .bar1 = pci_cfg_read32(TRI_PCI_BUS, TRI_PCI_SLOT, TRI_PCI_FUNCTION,
                               PCI_CFG_BAR0 + PCI_CFG_BAR_STRIDE)
    };
    const struct trident_pci_info *p = &resources.pci;
    if (p->vendor != TRI_PCI_VENDOR || p->device != TRI_PCI_DEVICE ||
        !(p->command & PCI_CMD_MEM_ENABLE)) return fail("pci", TRI_E_DEVICE);
    if ((p->bar0 & 0x07u) || (p->bar1 & 0x07u) || !p->bar0 || !p->bar1)
        return fail("bar", TRI_E_RESOURCE);
    bases[0] = p->bar0 & ~0x0Fu;
    bases[1] = p->bar1 & ~0x0Fu;
    for (u32 i = 0; i < TRI_BAR_COUNT; ++i)
        if (pgalloc_range_has_ram(bases[i] / PAGE_SIZE, (bases[i] + decodes[i]) / PAGE_SIZE))
            return fail("ram-overlap", TRI_E_RESOURCE);
    if (!ledger_owner_new(LEDGER_KIND_DEVICE, TRI_BDF, "trident", &resources.owner))
        return fail("owner", TRI_E_RESOURCE);
    struct ledger_span spans[TRI_BAR_COUNT];
    for (u32 i = 0; i < TRI_BAR_COUNT; ++i) {
        struct ledger_resource r = {
            .raw_bar = i ? p->bar1 : p->bar0, .decode_first = bases[i] / PAGE_SIZE,
            .decode_end = (bases[i] + decodes[i]) / PAGE_SIZE,
            .bdf = TRI_BDF, .vendor = p->vendor, .device = p->device,
            .bus = LEDGER_BUS_PCI, .revision = p->revision, .bar = i,
            .width_basis = LEDGER_WB_GLUE_CONST
        };
        if (!ledger_resource_add(&r, &rids[i])) return fail("record", TRI_E_RESOURCE);
        spans[i] = (struct ledger_span){r.decode_first, r.decode_end, LEDGER_SPAN_MMIO, rids[i]};
    }
    resources.bar0_rid = rids[0]; resources.bar1_rid = rids[1];
    if (!ledger_reserve_set(resources.owner, spans, TRI_BAR_COUNT))
        return fail("reserve", TRI_E_RESOURCE);
    for (u32 i = 0; i < TRI_BAR_COUNT; ++i) {
        if (paging_map_phys(bases[i], bases[i], maps[i] / PAGE_SIZE, PAGE_RW | PTE_PCD))
            return fail("map", TRI_E_MAP);
        mapped[i] = true;
        if (!ledger_resource_set_map(rids[i], bases[i] / PAGE_SIZE,
                                    (bases[i] + maps[i]) / PAGE_SIZE))
            return fail("map-record", TRI_E_RESOURCE);
    }
    if (!pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, TRI_DISPLAY_PAGES,
            MEM_PHYS_EXEC_FLOOR / PAGE_SIZE, pgalloc_arena_end(), LEDGER_TOP_DOWN, &client_pfn))
        return fail("client-ram", TRI_E_RESOURCE);
    client_allocated = true;
    resources.client = P2V(client_pfn * PAGE_SIZE);
    kmemset(resources.client, 0, TRI_DISPLAY_BYTES);
    /* perm_max is the lease ceiling, not publication readiness. The core
     * keeps both surfaces unready through init and gates USER access
     * on gfx_started. NONE would also forbid CLIENT attach after success. */
    struct ledger_surface sf = {
        .first = bases[0] / PAGE_SIZE, .npages = TRI_DISPLAY_PAGES,
        .width = TRI_WIDTH, .height = TRI_HEIGHT, .pitch = TRI_PITCH,
        .owner = resources.owner, .backing = LEDGER_SB_MMIO, .backend = LEDGER_SF_TRIDENT,
        .role = LEDGER_ROLE_DISPLAY, .format = GFX_BB_PACKED8, .planes = 1,
        .cache = LEDGER_CACHE_UC, .perm_max = LEDGER_PERM_RW
    };
    if (!ledger_surface_create(&sf, &resources.display_sid)) return fail("display", TRI_E_SURFACE);
    surface_created[0] = true;
    gfx_surface_unready |= 1u << resources.display_sid;
    sf.first = client_pfn; sf.owner = LEDGER_OWNER_BOOT; sf.backing = LEDGER_SB_RAM;
    sf.role = LEDGER_ROLE_CLIENT; sf.cache = LEDGER_CACHE_WB;
    if (!ledger_surface_create(&sf, &resources.client_sid)) return fail("client", TRI_E_SURFACE);
    surface_created[1] = true;
    gfx_surface_unready |= 1u << resources.client_sid;
    resources.lfb = P2V_IO(bases[0]);
    resources.regs = P2V_IO(bases[1]);
    resources.bar0_phys = bases[0]; resources.bar1_phys = bases[1];
    resources.decode_bytes = TRI_BAR0_DECODE_BYTES;
    resources.mapped_bytes = TRI_BAR0_DECODE_BYTES;
    resources.regs_bytes = TRI_REG_MAPPED_BYTES;
    resources.client_bytes = TRI_DISPLAY_BYTES;
    resources.ready = true;
    return 0;
}
