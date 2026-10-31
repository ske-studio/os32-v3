/* Private lease window validation, transactional mapping and teardown. */

/* T2b: page-table transactions confined to the private lease window. */
/* Shared root/normal-context predicate; lease_context adds PT checks. */
int paging_lease_root_context(const struct addrspace *as)
{
    return as && as->pd_phys && !(as->pd_phys % PAGE_SIZE) && as->lease_pt_phys[0] &&
        pgalloc_page_owned(as->pd_phys / PAGE_SIZE, as->owner) &&
        (paging_current_cr3() == paging_kernel_pd_phys() ||
         paging_current_cr3() == as->pd_phys) && !kctx_irq_depth && !kctx_exc_depth;
}

static int lease_context(const struct addrspace *as)
{
    u32 i, *pd;
    if (!paging_lease_root_context(as)) return 0;
    /* T2c supervisor identity aliases: never install the master to walk.
     * Validate managed frames and exact PDE rights before dereferencing PTs. */
    pd = P2V(as->pd_phys);
    for (i = 0; i < MEM_LEASE_MAX_PDES; i++) {
        u32 phys = as->lease_pt_phys[i], d = pd[LEASE_PDE + i];
        if (!phys) { if (d) return 0; continue; }
        if (phys % PAGE_SIZE || !pgalloc_page_owned(phys / PAGE_SIZE, as->owner) ||
            (d & ~(PTE_ACCESSED | PTE_DIRTY)) != (phys | PAGE_RW | PTE_USER))
            return 0;
    }
    return 1;
}

int paging_addrspace_create_lease(struct addrspace *as, u32 owner)
{
    u32 phys, i;
    u32 *pd;
    if (paging_current_cr3() != paging_kernel_pd_phys() ||
        kctx_irq_depth || kctx_exc_depth) return -1;
    if (paging_addrspace_create(as, owner)) return -1;
    phys = pgalloc_alloc_phys(owner, 1);
    if (!phys) { paging_addrspace_destroy(as); return -1; }
    for (i = 0; i < PTE_COUNT; i++) ((u32 *)P2V(phys))[i] = 0;
    pd = P2V(as->pd_phys);
    for (i = 0; i < MEM_LEASE_MAX_PDES; i++) pd[LEASE_PDE + i] = 0;
    as->lease_pt_phys[0] = phys;
    pd[LEASE_PDE] = phys | PAGE_RW | PTE_USER;
    return 0;
}

u32 paging_lease_pte(const struct addrspace *as, u32 va)
{
    u32 phys, d, index;
    if (!paging_lease_root_context(as) || va < MEM_LEASE_BASE || va >= MEM_LEASE_END) return 0;
    index = (va - MEM_LEASE_BASE) >> PDE_SHIFT;
    phys = as->lease_pt_phys[index];
    if (!phys || phys % PAGE_SIZE || !pgalloc_page_owned(phys / PAGE_SIZE, as->owner)) return 0;
    d = ((u32 *)P2V(as->pd_phys))[LEASE_PDE + index];
    if ((d & ~(PTE_ACCESSED | PTE_DIRTY)) != (phys | PAGE_RW | PTE_USER)) return 0;
    return ((u32 *)P2V(phys))[(va >> PAGE_SHIFT) % PTE_COUNT];
}

int paging_lease_map(struct addrspace *as, const struct lease_mapping *maps, u32 n)
{
    u32 pending[MEM_LEASE_MAX_PDES] = {0};
    u32 i, j, k, va, phys, *pd;
    unsigned int saved;
    int rc = -1;
    if (!lease_context(as) || !maps || !n || n > MEM_LEASE_MAX) return -1;
    pd = P2V(as->pd_phys);
    for (i = 0; i < n; i++) {
        const struct lease_mapping *m = &maps[i];
        if (m->slot >= MEM_LEASE_MAX || as->leases[m->slot].token || !m->token ||
            m->sid >= LEDGER_MAX_SURFACES ||
            ledger_surfaces[m->sid].gen != m->generation ||
            ledger_surfaces[m->sid].first * PAGE_SIZE != m->phys ||
            ledger_surfaces[m->sid].npages != m->npages ||
            ledger_surfaces[m->sid].closing ||
            (m->flags & (PTE_PCD | PTE_PWT)) !=
                (ledger_surfaces[m->sid].cache == LEDGER_CACHE_UC ? PTE_PCD : 0) ||
            ledger_surfaces[m->sid].perm_max == LEDGER_PERM_NONE ||
            ((m->flags & PTE_RW) && ledger_surfaces[m->sid].perm_max != LEDGER_PERM_RW)) goto fail;
        if (m->base < MEM_LEASE_BASE || m->base >= MEM_LEASE_END ||
            (m->base % PAGE_SIZE) || (m->phys % PAGE_SIZE) || !m->npages ||
            m->npages > (MEM_LEASE_END - m->base) / PAGE_SIZE ||
            m->npages > PHYSMEM_MAX_PFN - m->phys / PAGE_SIZE ||
            (m->flags & ~(PTE_PRESENT | PTE_USER | PTE_RW | PTE_PCD | PTE_PWT)) ||
            (m->flags & (PTE_PRESENT | PTE_USER)) != (PTE_PRESENT | PTE_USER)) goto fail;
        for (j = 0; j < i; j++)
            if (m->slot == maps[j].slot ||
                (m->base < maps[j].base + maps[j].npages * PAGE_SIZE &&
                 maps[j].base < m->base + m->npages * PAGE_SIZE)) goto fail;
        for (j = 0; j < m->npages; j++) {
            va = m->base + j * PAGE_SIZE;
            k = (va - MEM_LEASE_BASE) >> PDE_SHIFT;
            phys = as->lease_pt_phys[k];
            if (phys) {
                if (!pgalloc_page_owned(phys / PAGE_SIZE, as->owner) ||
                    (pd[LEASE_PDE + k] & PTE_FRAME_MASK) != phys ||
                    (((u32 *)P2V(phys))[(va >> PAGE_SHIFT) % PTE_COUNT] & PTE_PRESENT)) goto fail;
            } else if (!pending[k]) {
                pending[k] = pgalloc_alloc_phys(as->owner, 1);
                if (!pending[k]) { rc = -2; goto fail; }
                for (u32 z = 0; z < PTE_COUNT; z++) ((u32 *)P2V(pending[k]))[z] = 0;
            }
        }
    }
    saved = irq_save();
    for (k = 0; k < MEM_LEASE_MAX_PDES; k++) if (pending[k]) {
        as->lease_pt_phys[k] = pending[k];
        pd[LEASE_PDE + k] = pending[k] | PAGE_RW | PTE_USER;
    }
    for (i = 0; i < n; i++) for (j = 0; j < maps[i].npages; j++) {
        va = maps[i].base + j * PAGE_SIZE;
        phys = as->lease_pt_phys[(va - MEM_LEASE_BASE) >> PDE_SHIFT];
        ((u32 *)P2V(phys))[(va >> PAGE_SHIFT) % PTE_COUNT] =
            (maps[i].phys + j * PAGE_SIZE) | maps[i].flags;
    }
    for (i = 0; i < n; i++) {
        struct as_lease *l = &as->leases[maps[i].slot];
        l->token = maps[i].token;
        l->sid = maps[i].sid;
        l->generation = maps[i].generation;
        l->base = maps[i].base;
        l->npages = maps[i].npages;
        l->flags = maps[i].flags;
        ledger_surfaces[l->sid].lease_count++;
    }
    /* No callback/AS switch during preparation; inactive AS reloads on resume. */
    if (paging_current_cr3() == as->pd_phys) paging_load_cr3(as->pd_phys);
    irq_restore(saved);
    return 0;
fail:
    for (k = 0; k < MEM_LEASE_MAX_PDES; k++) if (pending[k])
        pgalloc_free_n_owner(as->owner, pending[k] / PAGE_SIZE, 1);
    return rc;
}

int paging_lease_unmap(struct addrspace *as, u32 base, u32 npages)
{
    u32 j, k, *pt, *pd;
    unsigned int saved;
    if (!lease_context(as) || base < MEM_LEASE_BASE || base >= MEM_LEASE_END ||
        base % PAGE_SIZE || !npages || npages > (MEM_LEASE_END - base) / PAGE_SIZE) return -1;
    for (j = 0; j < npages; j++) {
        u32 va = base + j * PAGE_SIZE;
        k = (va - MEM_LEASE_BASE) >> PDE_SHIFT;
        if (!as->lease_pt_phys[k] ||
            !pgalloc_page_owned(as->lease_pt_phys[k] / PAGE_SIZE, as->owner) ||
            (((u32 *)P2V(as->pd_phys))[LEASE_PDE + k] & PTE_FRAME_MASK) !=
            as->lease_pt_phys[k]) return -1;
    }
    saved = irq_save();
    for (j = 0; j < npages; j++) {
        u32 va = base + j * PAGE_SIZE;
        k = (va - MEM_LEASE_BASE) >> PDE_SHIFT;
        if (as->lease_pt_phys[k])
            ((u32 *)P2V(as->lease_pt_phys[k]))[(va >> PAGE_SHIFT) % PTE_COUNT] = 0;
    }
    pd = P2V(as->pd_phys);
    /* Remove empty additional PDEs before TLB synchronization and free. */
    for (k = 1; k < MEM_LEASE_MAX_PDES; k++) if (as->lease_pt_phys[k]) {
        pt = P2V(as->lease_pt_phys[k]);
        for (j = 0; j < PTE_COUNT && !pt[j]; j++) {}
        if (j == PTE_COUNT) pd[LEASE_PDE + k] = 0;
    }
    if (paging_current_cr3() == as->pd_phys) paging_load_cr3(as->pd_phys);
    for (k = 1; k < MEM_LEASE_MAX_PDES; k++)
        if (!as->appmem_poisoned && as->lease_pt_phys[k] &&
            !pd[LEASE_PDE + k]) {
            if (!pgalloc_free_n_owner(as->owner, as->lease_pt_phys[k] / PAGE_SIZE, 1)) {
                paging_addrspace_poison(as);
                continue;
            }
            as->lease_pt_phys[k] = 0;
        }
    irq_restore(saved);
    return 0;
}

