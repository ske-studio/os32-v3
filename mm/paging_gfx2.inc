/* Transactional mappings in the gfx2 portion of the lease window. */
int paging_gfx2_map(struct addrspace *as, u32 va, u32 pa, u32 npages, u32 pte_flags)
{
    u32 pending[MEM_LEASE_MAX_PDES] = {0};
    u32 i, k, addr, phys, *pd;
    unsigned int saved;
    int rc = -1;

    if (!lease_context(as) || va < MEM_GFX2_MAP_BASE || va >= MEM_GFX2_MAP_END ||
        va % PAGE_SIZE || pa % PAGE_SIZE || !npages ||
        npages > (MEM_GFX2_MAP_END - va) / PAGE_SIZE ||
        npages - 1 > (0xFFFFFFFFu - pa) / PAGE_SIZE ||
        (pte_flags & ~(PTE_PRESENT | PTE_USER | PTE_RW | PTE_PCD | PTE_PWT)) ||
        (pte_flags & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER))
        return -1;
    pd = P2V(as->pd_phys);
    for (i = 0; i < npages; i++) {
        addr = va + i * PAGE_SIZE;
        k = (addr - MEM_LEASE_BASE) >> PDE_SHIFT;
        phys = as->lease_pt_phys[k];
        if (phys) {
            if (((u32 *)P2V(phys))[(addr >> PAGE_SHIFT) % PTE_COUNT] & PTE_PRESENT)
                goto fail;
        } else if (!pending[k]) {
            pending[k] = pgalloc_alloc_phys(as->owner, 1);
            if (!pending[k]) { rc = -2; goto fail; }
            for (u32 z = 0; z < PTE_COUNT; z++) ((u32 *)P2V(pending[k]))[z] = 0;
        }
    }
    saved = irq_save();
    for (k = 0; k < MEM_LEASE_MAX_PDES; k++) if (pending[k]) {
        as->lease_pt_phys[k] = pending[k];
        pd[LEASE_PDE + k] = pending[k] | PAGE_RW | PTE_USER;
    }
    for (i = 0; i < npages; i++) {
        addr = va + i * PAGE_SIZE;
        phys = as->lease_pt_phys[(addr - MEM_LEASE_BASE) >> PDE_SHIFT];
        ((u32 *)P2V(phys))[(addr >> PAGE_SHIFT) % PTE_COUNT] =
            (pa + i * PAGE_SIZE) | pte_flags;
    }
    if (paging_current_cr3() == as->pd_phys) paging_load_cr3(as->pd_phys);
    irq_restore(saved);
    return 0;
fail:
    for (k = 0; k < MEM_LEASE_MAX_PDES; k++) if (pending[k])
        pgalloc_free_n_owner(as->owner, pending[k] / PAGE_SIZE, 1);
    return rc;
}
