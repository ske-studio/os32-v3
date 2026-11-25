/* HAL v2 SYSMEM backing, owned by the surface's ledger owner. */
#include "gfx2_int.h"
#include "paging.h"
#include "kstring.h"

int gfx2_sysmem_alloc(struct gfx2_surf *s, u32 owner)
{
    u32 n = (s->bytes + PAGE_SIZE - 1) / PAGE_SIZE, pfn;

    if (!pgalloc_alloc_n_owner(owner, (int)n, MEM_PHYS_EXEC_FLOOR / PAGE_SIZE,
                               pgalloc_arena_end(), 0, &pfn))
        return OS32_ERR_NOSPC;
    s->sys_pfn = pfn;
    s->sys_n = n;
    s->kva = (u8 *)P2V(pfn << 12);
    kmemset(s->kva, 0, n * PAGE_SIZE);
    return 0;
}

int gfx2_sysmem_free(struct gfx2_surf *s)
{
    if (!pgalloc_free_n_owner(s->owner, s->sys_pfn, (int)s->sys_n))
        return OS32_ERR_IO;
    s->sys_pfn = s->sys_n = 0;
    s->kva = 0;
    return 0;
}
