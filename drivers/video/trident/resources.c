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
#include "hwdev/gf/production.h"

/* Intel 8255x SDM rev1.3 §4.1.10 Table3; FreeBSD if_fxpreg.h
 * FXP_PCI_MMBA/IOBA. Same exact apertures as gfinit_resources.c:31-37. */
#define TRI_FXP_CSR_BYTES 0x1000u
#define TRI_FXP_FLASH_BYTES 0x100000u
#define TRI_RA266_REVISION 0xD3u
#define TRI_BAR_COUNT 2u
#define TRI_BDF ((TRI_PCI_BUS << 8) | (TRI_PCI_SLOT << 3) | TRI_PCI_FUNCTION)

static struct trident_resources resources;
static bool attempted, client_allocated;
static bool mapped[TRI_BAR_COUNT], surface_created[TRI_BAR_COUNT];
static u32 rids[TRI_BAR_COUNT], client_pfn;
static int boot_rc;
static const u32 bases[TRI_BAR_COUNT] = {MEM_TRIDENT_LFB_VA, MEM_TRIDENT_REGS_VA};
static const u32 decodes[TRI_BAR_COUNT] = {TRI_BAR0_DECODE_BYTES, TRI_BAR1_DECODE_BYTES};
static const u32 maps[TRI_BAR_COUNT] = {TRI_BAR0_DECODE_BYTES, TRI_REG_MAPPED_BYTES};

static bool boot_context(void)
{
    return paging_current_cr3() == paging_kernel_pd_phys() &&
           !paging_live_addrspaces() && !kctx_irq_depth && !kctx_exc_depth;
}

static bool overlaps(u32 first, u32 end, u32 other_first, u32 other_end)
{
    return first < other_end && other_first < end;
}

/* Exact other-device lengths or a previously validated ledger interval.
 * RAW samples alone do not establish a length. Never shrink a window using
 * the Trident window whose non-overlap we are trying to prove. */
static int other_bar(u32 bdf, u16 vendor, u16 device, u32 bar, u32 raw)
{
    u32 first = (raw & PCI_BAR_MEM_ADDR_MASK) / PAGE_SIZE, end = first;
    if ((raw & PCI_BAR_TYPE_MASK) != PCI_BAR_MEM_TYPE32 || !first)
        return TRI_E_RESOURCE;
    if (vendor == PCI_VENDOR_INTEL && device == PCI_DEVICE_82557 &&
        (bar == 0 || bar == 2)) {
        u32 bytes = bar == 0 ? TRI_FXP_CSR_BYTES : TRI_FXP_FLASH_BYTES;
        u32 attr = bar == 0 ? PCI_BAR_MEM_PREFETCH : PCI_BAR_MEM_TYPE32;
        if ((raw & PCI_BAR_MEM_ATTR_MASK) != attr ||
            ((raw & PCI_BAR_MEM_ADDR_MASK) & (bytes - 1))) return TRI_E_RESOURCE;
        end = first + bytes / PAGE_SIZE;
    } else if (vendor == PCI_VENDOR_NVIDIA && device == PCI_DEVICE_NV44A && bar < 3) {
        /* Existing NV44A production apertures, not an inferred neighbour cap. */
        const u32 lengths[] = {GF_PRODUCTION_BAR0_BYTES, GF_PRODUCTION_BAR1_BYTES,
                               GF_PRODUCTION_BAR2_BYTES};
        u32 bytes = lengths[bar];
        u32 attr = bar == 1 ? PCI_BAR_MEM_PREFETCH : PCI_BAR_MEM_TYPE32;
        u32 dev = (bdf >> 3) & PCI_DEV_MASK, fn = bdf & PCI_FN_MASK;
        if ((raw & PCI_BAR_MEM_ATTR_MASK) != attr ||
            ((raw & PCI_BAR_MEM_ADDR_MASK) & (bytes - 1)) ||
            pci_cfg_read32(0, dev, fn, PCI_CFG_SUBSYS_VENDOR) != GF_PRODUCTION_SUBSYS)
            return TRI_E_RESOURCE;
        end = first + bytes / PAGE_SIZE;
    } else {
        for (u32 i = 0; i < LEDGER_MAX_RESOURCES; ++i) {
            const struct ledger_resource *r = &ledger_resources[i];
            if (r->bus == LEDGER_BUS_PCI && r->bdf == bdf && r->bar == bar &&
                r->vendor == vendor && r->device == device && r->raw_bar == raw &&
                (r->width_basis == LEDGER_WB_SIZING ||
                 r->width_basis == LEDGER_WB_DATASHEET ||
                 r->width_basis == LEDGER_WB_GLUE_CONST) &&
                r->decode_first == first && r->decode_end > first) {
                end = r->decode_end;
                break;
            }
        }
        if (end == first) return TRI_E_RESOURCE;
    }
    if (end > PHYSMEM_MAX_PFN) return TRI_E_RESOURCE;
    for (u32 i = 0; i < TRI_BAR_COUNT; ++i)
        if (overlaps(bases[i] / PAGE_SIZE, (bases[i] + decodes[i]) / PAGE_SIZE,
                     first, end)) return TRI_E_RESOURCE;
    return 0;
}

/* PCI2050B SCPS076G §§4.20-4.25 / FreeBSD pcireg.h MEMBASE_1:
 * forwarding intervals are exclusions, never independent neighbour bounds. */
static int bridge_window(u32 dev, u32 fn, u32 reg)
{
    u32 raw = pci_cfg_read32(0, dev, fn, reg);
    u32 first = (raw & PCI_BRIDGE_WINDOW_MASK) << PCI_BRIDGE_WINDOW_SHIFT;
    u32 last = ((raw >> 16) & PCI_BRIDGE_WINDOW_MASK) << PCI_BRIDGE_WINDOW_SHIFT;
    u32 type = raw & PCI_BAR_MEM_ATTR_MASK;
    if (type != ((raw >> 16) & PCI_BAR_MEM_ATTR_MASK) ||
        type > (reg == PCI_CFG_PREF_WINDOW ? 1u : 0u)) return TRI_E_RESOURCE;
    if (type && (pci_cfg_read32(0, dev, fn, PCI_CFG_PREF_BASE_HIGH) ||
                 pci_cfg_read32(0, dev, fn, PCI_CFG_PREF_LIMIT_HIGH))) return TRI_E_RESOURCE;
    if (first > last) return 0;
    u32 end_pfn = last / PAGE_SIZE + PCI_BRIDGE_WINDOW_GRANULE / PAGE_SIZE;
    for (u32 i = 0; i < TRI_BAR_COUNT; ++i)
        if (overlaps(bases[i] / PAGE_SIZE, (bases[i] + decodes[i]) / PAGE_SIZE,
                     first / PAGE_SIZE, end_pfn)) return TRI_E_RESOURCE;
    return 0;
}

/* Complete bus-0 scan, independent of the bounded pci_get cache. Child
 * devices are covered by their bus-0 bridge's full forwarding windows. */
static int validate_layout(void)
{
    bool csr_boundary = false;
    for (u32 dev = 0; dev < PCI_DEV_COUNT; ++dev) {
        if (pci_cfg_read16(0, dev, 0, PCI_CFG_VENDOR_ID) == PCI_VENDOR_NONE) continue;
        u8 header = pci_cfg_read8(0, dev, 0, PCI_CFG_HEADER_TYPE);
        u32 nfn = header & PCI_HDR_MULTIFUNCTION ? PCI_FN_COUNT : 1;
        for (u32 fn = 0; fn < nfn; ++fn) {
            u16 vendor = pci_cfg_read16(0, dev, fn, PCI_CFG_VENDOR_ID);
            if (vendor == PCI_VENDOR_NONE || (dev == TRI_PCI_SLOT && fn == TRI_PCI_FUNCTION))
                continue;
            if (!(pci_cfg_read16(0, dev, fn, PCI_CFG_COMMAND) & PCI_CMD_MEM_ENABLE)) continue;
            header = pci_cfg_read8(0, dev, fn, PCI_CFG_HEADER_TYPE) & PCI_HDR_LAYOUT_MASK;
            if (header != PCI_HDR_LAYOUT_DEVICE && header != PCI_HDR_LAYOUT_BRIDGE)
                return TRI_E_RESOURCE;
            u16 device = pci_cfg_read16(0, dev, fn, PCI_CFG_DEVICE_ID);
            u32 nb = header == PCI_HDR_LAYOUT_BRIDGE ? PCI_BRIDGE_BAR_COUNT : PCI_CFG_BAR_COUNT;
            for (u32 b = 0; b < nb; ++b) {
                u32 raw = pci_cfg_read32(0, dev, fn, PCI_CFG_BAR0 + b * PCI_CFG_BAR_STRIDE);
                if (!raw || (raw & PCI_BAR_SPACE_IO)) continue;
                int rc = other_bar((dev << 3) | fn, vendor, device, b, raw);
                if (rc) return rc;
                if (vendor == PCI_VENDOR_INTEL && device == PCI_DEVICE_82557 && b == 0 &&
                    (raw & PCI_BAR_MEM_ADDR_MASK) == MEM_TRIDENT_REGS_VA + TRI_BAR1_DECODE_BYTES)
                    csr_boundary = true;
            }
            u32 rom = header == PCI_HDR_LAYOUT_BRIDGE ? PCI_CFG_ROM_BRIDGE : PCI_CFG_ROM_ENDPOINT;
            if (pci_cfg_read32(0, dev, fn, rom) & 1u)
                return TRI_E_RESOURCE; /* enabled ROM of unknown length */
            if (header == PCI_HDR_LAYOUT_BRIDGE) {
                int rc = bridge_window(dev, fn, PCI_CFG_MEM_WINDOW);
                if (rc) return rc;
                rc = bridge_window(dev, fn, PCI_CFG_PREF_WINDOW);
                if (rc) return rc;
            }
        }
    }
    return csr_boundary ? 0 : TRI_E_RESOURCE;
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
        p->revision != TRI_RA266_REVISION || !(p->command & PCI_CMD_MEM_ENABLE) ||
        p->bar0 != MEM_TRIDENT_LFB_VA || p->bar1 != MEM_TRIDENT_REGS_VA ||
        (pci_cfg_read8(TRI_PCI_BUS, TRI_PCI_SLOT, TRI_PCI_FUNCTION, PCI_CFG_HEADER_TYPE) &
         PCI_HDR_LAYOUT_MASK) != PCI_HDR_LAYOUT_DEVICE)
        return fail("pci", TRI_E_DEVICE);
    /* BAR2/ROM cannot alias the two bounded windows. BAR2 is outside this
     * driver's mappings; the Ra266 baseline requires it above BAR1 decode. */
    u32 bar2 = pci_cfg_read32(TRI_PCI_BUS, TRI_PCI_SLOT, TRI_PCI_FUNCTION,
                             PCI_CFG_BAR0 + 2 * PCI_CFG_BAR_STRIDE);
    if ((bar2 & PCI_BAR_MEM_ATTR_MASK) || bar2 < MEM_TRIDENT_REGS_VA + TRI_BAR1_DECODE_BYTES ||
        (pci_cfg_read32(TRI_PCI_BUS, TRI_PCI_SLOT, TRI_PCI_FUNCTION, PCI_CFG_ROM_ENDPOINT) & 1u))
        return fail("layout", TRI_E_RESOURCE);
    int rc = validate_layout();
    if (rc) return fail("pci-overlap", rc);
    for (u32 i = 0; i < TRI_BAR_COUNT; ++i)
        if (pgalloc_range_has_ram(bases[i] / PAGE_SIZE, (bases[i] + decodes[i]) / PAGE_SIZE))
            return fail("ram-overlap", TRI_E_RESOURCE);
    if (!ledger_owner_new(LEDGER_KIND_DEVICE, TRI_BDF, "trident", &resources.owner))
        return fail("owner", TRI_E_RESOURCE);
    struct ledger_span spans[TRI_BAR_COUNT];
    for (u32 i = 0; i < TRI_BAR_COUNT; ++i) {
        struct ledger_resource r = {
            .raw_bar = bases[i], .decode_first = bases[i] / PAGE_SIZE,
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
    struct ledger_surface sf = {
        .first = MEM_TRIDENT_LFB_VA / PAGE_SIZE, .npages = TRI_DISPLAY_PAGES,
        .width = TRI_WIDTH, .height = TRI_HEIGHT, .pitch = TRI_PITCH,
        .owner = resources.owner, .backing = LEDGER_SB_MMIO, .backend = LEDGER_SF_TRIDENT,
        .role = LEDGER_ROLE_DISPLAY, .format = GFX_BB_PACKED8, .planes = 1,
        .cache = LEDGER_CACHE_UC, .perm_max = LEDGER_PERM_NONE
    };
    if (!ledger_surface_create(&sf, &resources.display_sid)) return fail("display", TRI_E_SURFACE);
    surface_created[0] = true;
    gfx_surface_unready |= 1u << resources.display_sid;
    sf.first = client_pfn; sf.owner = LEDGER_OWNER_BOOT; sf.backing = LEDGER_SB_RAM;
    sf.role = LEDGER_ROLE_CLIENT; sf.cache = LEDGER_CACHE_WB;
    if (!ledger_surface_create(&sf, &resources.client_sid)) return fail("client", TRI_E_SURFACE);
    surface_created[1] = true;
    gfx_surface_unready |= 1u << resources.client_sid;
    resources.lfb = P2V_IO(MEM_TRIDENT_LFB_VA);
    resources.regs = P2V_IO(MEM_TRIDENT_REGS_VA);
    resources.bar0_phys = MEM_TRIDENT_LFB_VA; resources.bar1_phys = MEM_TRIDENT_REGS_VA;
    resources.decode_bytes = TRI_BAR0_DECODE_BYTES;
    resources.mapped_bytes = TRI_BAR0_DECODE_BYTES;
    resources.regs_bytes = TRI_REG_MAPPED_BYTES;
    resources.client_bytes = TRI_DISPLAY_BYTES;
    resources.ready = true;
    return 0;
}
