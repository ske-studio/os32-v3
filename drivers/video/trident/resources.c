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
#include "exec/lease.h"
#include "gfx_format.h"
#include "io.h"
#include "lib/kstring.h"

#ifndef TRI_RAM_FALLBACK_DEFAULT
#define TRI_RAM_FALLBACK_DEFAULT 1
#endif

#define TRI_BAR_COUNT 2u
#define TRI_BDF ((TRI_PCI_BUS << 8) | (TRI_PCI_SLOT << 3) | TRI_PCI_FUNCTION)

static struct trident_resources resources;
static bool attempted;
static bool mapped[TRI_BAR_COUNT], surface_created[TRI_BAR_COUNT];
static u32 rids[TRI_BAR_COUNT], display_capacity;
static int boot_rc;
static bool capacity_final, sids_fixed;
static u32 fixed_sids[TRI_BAR_COUNT], live_sids[TRI_BAR_COUNT];
static u32 bases[TRI_BAR_COUNT];
static const u32 vas[TRI_BAR_COUNT] = {MEM_TRIDENT_LFB_VA, MEM_TRIDENT_REGS_VA};
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
    /* No lease may outlive backing or mapping. */
    u32 sids[TRI_BAR_COUNT] = {live_sids[0], live_sids[1]};
    unsigned int flags = irq_save();
    int revoked = 0;
    for (u32 i = 0; i < TRI_BAR_COUNT; ++i) {
        if (surface_created[i]) {
            gfx_surface_unready |= 1u << sids[i];
            revoked += lease_revoke_surface(sids[i]);
            if (ledger_surfaces[sids[i]].lease_count) revoked++;
        }
    }
    irq_restore(flags);
    if (revoked) return TRI_E_BUSY;
    resources.ready = false;
    for (u32 i = TRI_BAR_COUNT; i-- > 0;) {
        if (!surface_created[i]) continue;
        if (!ledger_surface_release(sids[i])) return TRI_E_SURFACE;
        surface_created[i] = false;
    }
    display_capacity = 0;
    capacity_final = false;
    sids_fixed = false;
    resources.generation = 0;
    resources.display = (struct tri_surface){0};
    resources.client_surface = (struct tri_surface){0};
    resources.client = 0;
    resources.client_bytes = 0;
    for (u32 i = TRI_BAR_COUNT; i-- > 0;) {
        if (!mapped[i]) continue;
        if (paging_set_not_present(vas[i], vas[i] + maps[i] - 1)) return TRI_E_MAP;
        if (!ledger_resource_set_map(rids[i], 0, 0)) return TRI_E_RESOURCE;
        mapped[i] = false;
    }
    resources.regs = resources.lfb = 0;
    resources.mapped_bytes = resources.regs_bytes = 0;
    if (attempted) boot_rc = TRI_E_STATE;
    return 0; /* DEVICE owner, records and decode reservations survive */
}

/* D:3534-3546 uses FbBase directly without ShadowFB. F5 reserves the
 * fixed display capacity before every offscreen allocation (D:3637-3661).
 * Initial prepare is provisional; ScreenInit fixes capacity, then only regen.
 * The callback is core text: it must not call module-owned F3/F5 functions. */
static int configure(const struct tri_mode *mode, u32 display_bytes, bool final)
{
    if (!resources.ready || kctx_irq_depth || kctx_exc_depth) return TRI_E_STATE;
    if (!mode) return TRI_E_INVAL;
    u8 cpp, format, hal_format;
    if (mode->depth == 8 && mode->bpp == 8) {
        cpp = 1; format = TRI_FMT_INDEX8; hal_format = GFX_BB_PACKED8;
    } else if (mode->depth == 16 && mode->bpp == 16) {
        cpp = 2; format = TRI_FMT_RGB565; hal_format = GFX_FMT_RGB565;
    } else if (mode->depth == 24 && mode->bpp == 32) {
        cpp = 4; format = TRI_FMT_XRGB8888; hal_format = GFX_FMT_XRGB8888;
    } else return TRI_E_NOSYS;
    if (capacity_final && final && display_bytes != display_capacity) return TRI_E_STATE;
    if (capacity_final && !final) display_bytes = display_capacity;
    if (!display_bytes || display_bytes % PAGE_SIZE ||
        display_bytes > resources.mapped_bytes || display_bytes > resources.decode_bytes)
        return TRI_E_CAP_TOO_SMALL;
    u32 pitch = (u32)mode->display_width * cpp;
    if (!mode->hd || !mode->vd || mode->display_width < mode->hd ||
        pitch > (u16)~0u) return TRI_E_INVAL;
    if (mode->vd > display_bytes / pitch) return TRI_E_CAP_TOO_SMALL;
    struct ledger_surface sf = {
        .first = bases[0] / PAGE_SIZE, .npages = display_bytes / PAGE_SIZE,
        .width = mode->hd, .height = mode->vd, .pitch = pitch,
        .owner = resources.owner, .backing = LEDGER_SB_VRAM,
        .backend = LEDGER_SF_TRIDENT, .role = LEDGER_ROLE_DISPLAY,
        .format = hal_format, .planes = 1, .cache = LEDGER_CACHE_UC,
        .perm_max = LEDGER_PERM_RW
    };
    unsigned int flags = irq_save();
    u32 cr3 = paging_current_cr3();
    paging_load_cr3(paging_kernel_pd_phys());
    int rc = TRI_E_SURFACE;
    u32 sids[TRI_BAR_COUNT] = {live_sids[0], live_sids[1]};
    u32 bits = 0, unready = 0;
    if (display_capacity) {
        bits = (1u << sids[0]) | (1u << sids[1]);
        unready = gfx_surface_unready & bits;
        if (!final && resources.display.width == mode->hd &&
            resources.display.height == mode->vd && resources.display.pitch == pitch &&
            resources.display.format == format) { rc = TRI_OK; goto done; }
        for (u32 i = 0; i < TRI_BAR_COUNT; ++i) {
            const struct ledger_surface *old = &ledger_surfaces[sids[i]];
            if (!old->npages || old->closing || old->gen == LEDGER_SURFACE_GEN_MAX)
                goto done;
            if (!capacity_final && (!(unready & (1u << sids[i])) || old->lease_count)) {
                rc = TRI_E_BUSY; goto done;
            }
        }
        gfx_surface_unready |= bits;
        int revoked = 0;
        for (u32 i = 0; i < TRI_BAR_COUNT; ++i) {
            revoked += lease_revoke_surface(sids[i]);
            if (ledger_surfaces[sids[i]].lease_count) revoked++;
        }
        if (revoked) { rc = TRI_E_BUSY; goto done; }
    }
    bool recreate = display_capacity && display_bytes != display_capacity;
    if (recreate) {
        /* A2: release both aliases before creating either new extent. */
        for (u32 i = 0; i < TRI_BAR_COUNT; ++i) {
            if (!ledger_surface_release(sids[i])) goto done;
            surface_created[i] = false;
        }
    }
    if (!display_capacity || recreate) {
        if (surface_created[0] || surface_created[1]) goto done;
        u32 created[TRI_BAR_COUNT];
        if (!ledger_surface_create(&sf, &created[0])) goto cleanup;
        live_sids[0] = created[0];
        surface_created[0] = true;
        gfx_surface_unready |= 1u << live_sids[0];
        if (sids_fixed && created[0] != fixed_sids[0]) goto cleanup;
        sf.role = LEDGER_ROLE_CLIENT;
        if (resources.ram_fallback) {
            sf.owner = LEDGER_OWNER_BOOT;
            sf.backing = LEDGER_SB_RAM;
            sf.cache = LEDGER_CACHE_WB;
            if (!pgalloc_alloc_n_owner(sf.owner, sf.npages, MEM_PHYS_EXEC_FLOOR / PAGE_SIZE,
                                      pgalloc_arena_end(), LEDGER_TOP_DOWN, &sf.first))
                goto cleanup;
            kmemset(P2V(sf.first * PAGE_SIZE), 0, display_bytes);
        }
        if (!ledger_surface_create(&sf, &created[1])) {
            if (resources.ram_fallback)
                (void)pgalloc_free_n_owner(sf.owner, sf.first, sf.npages);
            goto cleanup;
        }
        live_sids[1] = created[1];
        surface_created[1] = true;
        gfx_surface_unready |= 1u << live_sids[1];
        if (sids_fixed && created[1] != fixed_sids[1]) goto cleanup;
    } else {
        if (!ledger_surface_regen(resources.display_sid, &sf)) goto done;
        if (resources.ram_fallback) {
            const struct ledger_surface *client = &ledger_surfaces[resources.client_sid];
            sf.first = client->first;
            sf.owner = client->owner;
            sf.backing = LEDGER_SB_RAM;
            sf.cache = LEDGER_CACHE_WB;
        }
        if (!ledger_surface_regen(resources.client_sid, &sf)) goto done;
    }
    resources.display_sid = live_sids[0];
    resources.client_sid = live_sids[1];
    fixed_sids[0] = resources.display_sid;
    fixed_sids[1] = resources.client_sid;
    sids_fixed = true;
    display_capacity = display_bytes;
    if (final) capacity_final = true;
    gfx_surface_unready = (gfx_surface_unready & ~bits) | unready;
    resources.display = (struct tri_surface){
        .off = 0, .bytes = pitch * mode->vd, .pitch = pitch,
        .width = mode->hd, .height = mode->vd, .cpp = cpp, .format = format
    };
    resources.client_surface = resources.display;
    resources.client = resources.ram_fallback ?
        P2V(ledger_surfaces[resources.client_sid].first * PAGE_SIZE) : (u8 *)resources.lfb;
    resources.client_bytes = resources.client_surface.bytes;
    resources.generation = ledger_surfaces[resources.client_sid].gen;
    rc = TRI_OK;
    goto done;
cleanup:
    for (u32 i = TRI_BAR_COUNT; i-- > 0;) {
        u32 sid = live_sids[i];
        if (surface_created[i] && ledger_surface_release(sid)) surface_created[i] = false;
    }
    display_capacity = 0;
    resources.generation = 0;
    resources.client = 0;
    resources.client_bytes = 0;
    resources.display = resources.client_surface = (struct tri_surface){0};
done:
    /* Revocation can alter the caller's lease PTEs: reload even its old CR3. */
    paging_load_cr3(cr3);
    irq_restore(flags);
    return rc;
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
    /* Boot maps fixed VAs; reject hardware outside that mapping contract. */
    if (bases[0] != TRI_BAR0_PHYS || bases[1] != TRI_BAR1_PHYS)
        return fail("bar", TRI_E_RESOURCE);
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
        if (paging_map_phys(vas[i], bases[i], maps[i] / PAGE_SIZE, PAGE_RW | PTE_PCD))
            return fail("map", TRI_E_MAP);
        mapped[i] = true;
        if (!ledger_resource_set_map(rids[i], bases[i] / PAGE_SIZE,
                                    (bases[i] + maps[i]) / PAGE_SIZE))
            return fail("map-record", TRI_E_RESOURCE);
    }
    /* These are mapped VAs; the ledger and surfaces retain physical PFNs. */
    resources.lfb = (volatile u8 *)(uptr)vas[0];
    resources.regs = (volatile u8 *)(uptr)vas[1];
    resources.bar0_phys = bases[0]; resources.bar1_phys = bases[1];
    resources.decode_bytes = TRI_BAR0_DECODE_BYTES;
    resources.mapped_bytes = TRI_BAR0_DECODE_BYTES;
    resources.regs_bytes = TRI_REG_MAPPED_BYTES;
    /* prepare creates the provisional aliases; boot only reserves/maps. */
    resources.configure = configure;
    resources.ram_fallback = TRI_RAM_FALLBACK_DEFAULT;
    resources.ready = true;
    return 0;
}
