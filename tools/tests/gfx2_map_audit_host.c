#include "gfx2_int.h"
#include "paging.h"

struct gfx2_surf gfx2_surfs[GFX2_MAX_SURFACES];
static struct addrspace as;
static u32 pt[PTE_COUNT] __attribute__((aligned(PAGE_SIZE)));

int gfx2_surf_page(const struct gfx2_surf *s, u32 k, u32 *pa, u32 *attr)
{
    *pa = (s == &gfx2_surfs[0] ? 0x100000 : 0x200000) + k * PAGE_SIZE;
    *attr = s == &gfx2_surfs[0] ? PTE_PCD : 0;
    return 0;
}

static int test(void)
{
    u32 di = (MEM_GFX2_MAP_BASE - MEM_LEASE_BASE) >> MEM_PDE_SHIFT;
    as.lease_pt_phys[di] = (u32)pt;
    for (u32 i = 0; i < 2; i++) {
        struct gfx2_surf *s = &gfx2_surfs[i];
        s->map_as = &as; s->map_va = MEM_GFX2_MAP_BASE + (i ? 2 : 0) * PAGE_SIZE;
        s->map_npages = i ? 3 : 2;
        for (u32 k = 0; k < s->map_npages; k++) {
            u32 pa, attr;
            gfx2_surf_page(s, k, &pa, &attr);
            pt[(i ? 2 : 0) + k] = pa | PAGE_RW | PTE_USER | attr | PTE_ACCESSED | PTE_DIRTY;
        }
    }
    if (gfx2_map_audit(&as)) return 1;
    pt[0] ^= PTE_PCD;
    if (gfx2_map_audit(&as) != -1) return 2;
    pt[0] ^= PTE_PCD;
    pt[5] = 0x300000 | PAGE_RW | PTE_USER;
    if (gfx2_map_audit(&as) != -1) return 3;
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    __builtin_unreachable();
}
