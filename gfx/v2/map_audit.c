/* HAL v2 mapping audit; called with the kernel address space and IRQs off. */
#include "gfx2_int.h"
#include "paging.h"
#include "physmem.h"

#define GFX2_FIRST_PDE ((MEM_GFX2_MAP_BASE - MEM_LEASE_BASE) >> MEM_PDE_SHIFT)

static u32 lease_pte_get(const struct addrspace *as, u32 va)
{
    u32 di = (va - MEM_LEASE_BASE) >> MEM_PDE_SHIFT;
    if (!as->lease_pt_phys[di]) return 0;
    const u32 *pt = P2V(as->lease_pt_phys[di]);
    return pt[PTE_INDEX(va)];
}

static u32 pte_present_count(const u32 *pt)
{
    u32 count = 0;
    for (u32 i = 0; i < PTE_COUNT; i++)
        if (pt[i] & PTE_PRESENT) count++;
    return count;
}

int gfx2_map_audit(const struct addrspace *as)
{
    u32 want = 0, have = 0, pa, attr;
    for (u32 i = 0; i < GFX2_MAX_SURFACES; i++) {
        const struct gfx2_surf *s = &gfx2_surfs[i];
        if (s->map_as != as) continue;
        if (s->map_va < MEM_GFX2_MAP_BASE || s->map_va >= MEM_GFX2_MAP_END ||
            s->map_npages > (MEM_GFX2_MAP_END - s->map_va) / PAGE_SIZE) return -1;
        for (u32 k = 0; k < s->map_npages; k++) {
            if (gfx2_surf_page(s, k, &pa, &attr)) return -1;
            if ((lease_pte_get(as, s->map_va + k * PAGE_SIZE) & ~(PTE_ACCESSED | PTE_DIRTY)) !=
                (pa | PAGE_RW | PTE_USER | attr)) return -1;
        }
        want += s->map_npages;
    }
    for (u32 di = GFX2_FIRST_PDE; di < MEM_LEASE_MAX_PDES; di++)
        if (as->lease_pt_phys[di]) have += pte_present_count(P2V(as->lease_pt_phys[di]));
    return have == want ? 0 : -1;
}
