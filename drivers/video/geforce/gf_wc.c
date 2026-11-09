/* GPU applicability; CPU attributes and lease lifetime belong to arch. */
#include "geforce_module.h"
#include "hwdev/gf/production.h"
#include "video_wc.h"
#include "mm/pgalloc.h"
#include "mm/paging.h"
#include "drivers/pci/pci.h"
#include "drivers/char/serial.h"

static int eligible(const struct geforce_platform *p)
{
    if (!p || !p->resources || !p->identify || !p->identify()) return 0;
    const struct recover_bar_extent *bar = &p->resources->bars[GF_MODE_VRAM_BAR];
    const struct recover_bar_extent *reg = &p->resources->bars[GF_MODE_REGISTER_BAR];
    u32 base = bar->base, first = base / PAGE_SIZE, end = first + GEFORCE_WC_BYTES / PAGE_SIZE;
    if ((bar->raw & PCI_BAR_MEM_ATTR_MASK) != PCI_BAR_MEM_PREFETCH ||
        (bar->raw & PCI_BAR_MEM_ADDR_MASK) != base || !base ||
        (base & (GEFORCE_WC_BYTES - 1)) || bar->length < GEFORCE_WC_BYTES ||
        (unsigned long long)base + bar->length > RECOVER_ADDRESS_END ||
        p->display_phys != base || p->bar0_phys != reg->base ||
        GF_MODE_VRAM_BYTES != MEM_GFX_BB8_SIZE || pgalloc_range_has_ram(first, end)) return 0;
    u32 rid;
    for (rid = 0; rid < LEDGER_MAX_RESOURCES; ++rid) {
        const struct ledger_resource *s = &ledger_resources[rid];
        if (s->decode_first >= end || s->decode_end <= first) continue;
        if (s->bus != LEDGER_BUS_PCI || s->bdf != RECOVER_GF_BDF ||
            s->bar != GF_MODE_VRAM_BAR || s->raw_bar != bar->raw ||
            s->decode_first != first || s->decode_end != first + bar->length / PAGE_SIZE ||
            s->map_first != first || s->map_end != first + GF_MODE_VRAM_BYTES / PAGE_SIZE) return 0;
        break;
    }
    if (rid == LEDGER_MAX_RESOURCES) return 0;
    for (u32 i = rid + 1; i < LEDGER_MAX_RESOURCES; ++i)
        if (ledger_resources[i].decode_first < end && ledger_resources[i].decode_end > first) return 0;
    u32 covered = first;
    for (u32 i = 0; i < ledger_region_count; ++i) {
        const struct ledger_region *s = &ledger_regions[i];
        if (s->first >= end || s->end <= first) continue;
        if (s->type != LEDGER_R_DEVICE || s->owner != LEDGER_OWNER_GFX ||
            s->cache != LEDGER_CACHE_UC || !(s->flags & LEDGER_RF_PERMANENT) ||
            s->res_mask != (1U << rid) || s->first > covered) return 0;
        if (s->end > covered) covered = s->end;
    }
    if (covered < end) return 0;
    const struct ledger_surface *sf = ledger_surface_find(LEDGER_SF_GEFORCE, LEDGER_ROLE_DISPLAY);
    if (!sf || sf->first != first || sf->npages != GF_MODE_VRAM_BYTES / PAGE_SIZE ||
        sf->cache != LEDGER_CACHE_UC || sf->backing != LEDGER_SB_MMIO) return 0;
    return arch_video_wc_eligible(base, GF_MODE_VRAM_BYTES, GEFORCE_WC_BYTES,
                                  reg->base, reg->length);
}

int geforce_wc_start(void)
{
    if (arch_video_wc_active()) return arch_video_wc_start(0, 0); /* Same fixed mode: never reacquire an active lease. */
    const struct geforce_platform *p = geforce_platform_get();
    if (!eligible(p)) return 0;
    return arch_video_wc_start(p->display_phys, GEFORCE_WC_BYTES);
}

int geforce_wc_stop(void)
{
    return arch_video_wc_stop(); /* Also drains borrowed WC after identity loss. */
}

void geforce_wc_report(const char *record)
{
    serial_puts(record);
}
