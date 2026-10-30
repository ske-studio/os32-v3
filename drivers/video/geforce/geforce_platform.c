/* Boot-only reservation, before ledger freeze. No display/device writes. */
#include "geforce_module.h"
#include "gf_production.h"
#include "../../../hwdev/gf/production.h"
#include "../../../mm/pgalloc.h"
#include "../../../mm/paging.h"
STATIC_ASSERT(GF_PRODUCTION_PT_PAGES ==
              GF_PRODUCTION_BAR0_BYTES / MEM_DEVICE_APERTURE_PDE_SIZE + 1,
              geforce_boot_pt_budget);
static struct gf_production_resources boot_resources;
static struct geforce_platform platform;
static int reserve(void)
{
    if (!gf_production_selected() || gf_production_identify(&boot_resources)) return 0;
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
        if (!ledger_resource_add(&r, &ids[i])) return 0;
        spans[i] = (struct ledger_span){first, end, LEDGER_SPAN_MMIO, ids[i]};
    }
    if (!ledger_reserve_set(LEDGER_OWNER_GFX, spans, RECOVER_BAR_COUNT)) return 0;
    for (u32 i = 0; i < RECOVER_BAR_COUNT; ++i) {
        const struct recover_bar_extent *b = &boot_resources.bars[i];
        u32 first = b->base / PAGE_SIZE;
        u32 pages = i == GF_MODE_REGISTER_BAR ? b->length / PAGE_SIZE :
                    i == GF_MODE_VRAM_BAR ? PAGE_ALIGN_UP(GF_MODE_VRAM_BYTES) / PAGE_SIZE : 0;
        if (pages && (paging_map_phys(b->base, b->base, pages, PAGE_RW | PTE_PCD) ||
                      !ledger_resource_set_map(ids[i], first, first + pages))) return 0;
    }
    platform.bar0_phys = boot_resources.bars[GF_MODE_REGISTER_BAR].base;
    platform.display_phys = boot_resources.bars[GF_MODE_VRAM_BAR].base;
    platform.resources = &boot_resources;
    return 1;
}
struct geforce_platform *geforce_platform_get(void)
{
    platform.reserve = reserve;
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
