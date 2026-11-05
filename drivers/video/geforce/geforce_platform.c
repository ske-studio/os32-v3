/* Boot-only reservation, before ledger freeze. No display/device writes. */
#include "geforce_module.h"
#include "gf_production.h"
#include "kprintf.h"
#include "../../../hwdev/gf/production.h"
#include "../../../hwdev/gf/vram_layout.h"
#include "../../../mm/pgalloc.h"
#include "../../../mm/paging.h"
#define GF_BOOT_INSTMEM_BAR 2U
#define GF_BOOT_INSTMEM_MAX_BYTES GF_INSTMEM_BYTES(GF_VS_MAX)
#define GF_BOOT_INSTMEM_PT_PAGES \
    ((GF_BOOT_INSTMEM_MAX_BYTES + MEM_DEVICE_APERTURE_PDE_SIZE - 1) / MEM_DEVICE_APERTURE_PDE_SIZE)
/* BAR1 is aligned to its 256MiB extent. DISPLAY, push (at most two PTs)
 * and all layer-3 views share these PTs; count their union only once. */
#define GF_BOOT_VRAM_PT_PAGES \
    (GF_PRODUCTION_BAR1_BYTES / MEM_DEVICE_APERTURE_PDE_SIZE)
STATIC_ASSERT(GF_PRODUCTION_PT_PAGES ==
              GF_PRODUCTION_BAR0_BYTES / MEM_DEVICE_APERTURE_PDE_SIZE +
              GF_BOOT_INSTMEM_PT_PAGES + GF_BOOT_VRAM_PT_PAGES,
              geforce_boot_pt_budget);
/* BAR1 DISPLAY may be effective WC through MTRR. Ledger cache/intervals,
 * permanent reservations and the existing 75 display PTEs remain unchanged. */
static struct gf_production_resources boot_resources;
static struct geforce_platform platform;
static int reserve_fail(const char *stage)
{
    kprintf(KLOG_ATTR, "[geforce] boot_reserve fail stage=%s\n", stage);
    return 0;
}
static int reserve(void)
{
    if (!gf_production_selected()) return reserve_fail("selection");
    if (gf_production_identify(&boot_resources)) return reserve_fail("identify");
    struct ledger_span spans[RECOVER_BAR_COUNT];
    u32 ids[RECOVER_BAR_COUNT];
    for (u32 i = 0; i < RECOVER_BAR_COUNT; ++i) {
        const struct recover_bar_extent *b = &boot_resources.bars[i];
        u32 first = b->base / PAGE_SIZE;
        u32 end = first + b->length / PAGE_SIZE;
        struct ledger_resource r = {
            .decode_first=first, .decode_end=end,
            .raw_bar=b->raw, .bus=LEDGER_BUS_PCI, .bdf=RECOVER_GF_BDF,
            .vendor=(u16)boot_resources.identity.device,
            .device=(u16)(boot_resources.identity.device >> 16),
            .revision=boot_resources.identity.revision,
            .bar=i, .width_basis=LEDGER_WB_DATASHEET
        };
        if (!ledger_resource_add(&r, &ids[i])) return reserve_fail("resource");
        spans[i] = (struct ledger_span){first, end, LEDGER_SPAN_MMIO, ids[i]};
    }
    if (!ledger_reserve_set(LEDGER_OWNER_GFX, spans, RECOVER_BAR_COUNT))
        return reserve_fail("reserve");
    for (u32 i = 0; i < RECOVER_BAR_COUNT; ++i) {
        const struct recover_bar_extent *b = &boot_resources.bars[i];
        u32 first = b->base / PAGE_SIZE;
        u32 pages = i == GF_MODE_REGISTER_BAR ? b->length / PAGE_SIZE :
                    i == GF_MODE_VRAM_BAR ? PAGE_ALIGN_UP(GF_MODE_VRAM_BYTES) / PAGE_SIZE : 0;
        if (pages && paging_map_phys(b->base, b->base, pages, PAGE_RW | PTE_PCD))
            return reserve_fail("bar-map");
        if (pages && !ledger_resource_set_map(ids[i], first, first + pages))
            return reserve_fail("bar-ledger-map");
    }
    /* Precreate shared PTs before any live AS. Keep access closed until B5 map. */
    const struct recover_bar_extent *instmem = &boot_resources.bars[GF_BOOT_INSTMEM_BAR];
    u32 bytes = instmem->length < GF_BOOT_INSTMEM_MAX_BYTES ?
                instmem->length : GF_BOOT_INSTMEM_MAX_BYTES;
    if (paging_map_phys(instmem->base, instmem->base, bytes / PAGE_SIZE, PAGE_RW | PTE_PCD))
        return reserve_fail("instmem-map");
    if (paging_set_not_present(instmem->base, instmem->base + bytes - 1))
        return reserve_fail("instmem-close");
    /* PFB is not initialized on a cold card. Prepare the whole BAR1 tail;
     * devinit and command start determine V, R and B later. */
    const struct recover_bar_extent *vram = &boot_resources.bars[GF_MODE_VRAM_BAR];
    u32 display_end = PAGE_ALIGN_UP(GF_MODE_VRAM_BYTES);
    u32 view_phys = vram->base + display_end;
    u32 view_bytes = vram->length - display_end;
    if (paging_map_phys(view_phys, view_phys, view_bytes / PAGE_SIZE, PAGE_RW | PTE_PCD))
        return reserve_fail("vram-map");
    if (paging_set_not_present(view_phys, view_phys + view_bytes - 1))
        return reserve_fail("vram-close");
    platform.bar0_phys = boot_resources.bars[GF_MODE_REGISTER_BAR].base;
    platform.display_phys = boot_resources.bars[GF_MODE_VRAM_BAR].base;
    platform.resources = &boot_resources;
    return 1;
}
struct geforce_platform *geforce_platform_get(void)
{
    platform.reserve = reserve;
    platform.wc_start = geforce_wc_start;
    platform.wc_stop = geforce_wc_stop;
    platform.report = geforce_wc_report;
    return gf_production_selected() ? &platform : 0;
}

int geforce_range_reserved(u32 base, u32 length)
{
    u32 first=base/PAGE_SIZE, end=first+length/PAGE_SIZE;
    while (first<end) {
        u32 next=first;
        for (u32 i=0;i<ledger_region_count;i++) {
            const struct ledger_region *r=&ledger_regions[i];
            if (r->type==LEDGER_R_DEVICE && r->owner==LEDGER_OWNER_GFX &&
                r->cache==LEDGER_CACHE_UC && r->first<=first && r->end>next)
                next=r->end;
        }
        if (next==first) return -1;
        first=next;
    }
    return 0;
}
