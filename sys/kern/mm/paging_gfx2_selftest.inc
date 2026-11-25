/* I1: real private PT, application CR3 read/write, unmap and owner teardown.
 * Result bits: 1=owner, 2=AS, 4=page, 8=map, 16=read, 32=write,
 * 64=unmap, 128=PTE, 256=page free, 512=AS/PT cleanup, 1024=free count. */
u32 gfx2_map_selftest_runs, gfx2_map_selftest_result;

int gfx2_map_selftest(void)
{
    struct addrspace as;
    u32 owner, pfn, saved, before = pgalloc_free_pages();
    u32 va = MEM_GFX2_MAP_BASE, k = (va - MEM_LEASE_BASE) >> PDE_SHIFT;
    unsigned int flags;
    int rc = 0;
    gfx2_map_selftest_runs++;
    if (!ledger_owner_new(LEDGER_KIND_AS, 0, "gfx2-map-test", &owner)) {
        rc = 1; goto out;
    }
    if (paging_addrspace_create_lease(&as, owner)) {
        (void)ledger_owner_retire(owner);
        rc = 2; goto out;
    }
    if (!pgalloc_alloc_n_owner(owner, 1, MEM_PHYS_EXEC_FLOOR / PAGE_SIZE,
                              pgalloc_arena_end(), 0, &pfn)) {
        rc = 4; goto destroy;
    }
    *(volatile u32 *)P2V(pfn * PAGE_SIZE) = 0x12345678UL;
    if (paging_gfx2_map(&as, va, pfn * PAGE_SIZE, 1, PAGE_RW | PTE_USER)) {
        rc = 8; goto free_page;
    }
    flags = irq_save();
    saved = paging_current_cr3();
    paging_load_cr3(as.pd_phys);
    if (*(volatile u32 *)(uptr)va != 0x12345678UL) rc |= 16;
    *(volatile u32 *)(uptr)va = 0xa5a5f00dUL;
    paging_load_cr3(saved);
    irq_restore(flags);
    if (*(volatile u32 *)P2V(pfn * PAGE_SIZE) != 0xa5a5f00dUL) rc |= 32;
    if (paging_lease_unmap(&as, va, 1)) rc |= 64;
    /* lease_unmap may already return an empty additional PT. */
    if (as.lease_pt_phys[k] ?
        ((u32 *)P2V(as.lease_pt_phys[k]))[PTE_INDEX(va)] != 0 :
        ((u32 *)P2V(as.pd_phys))[LEASE_PDE + k] != 0) rc |= 128;
free_page:
    /* A failed unmap must never leave a PTE pointing at a returned page. */
    paging_addrspace_destroy(&as);
    if (!pgalloc_free_n_owner(owner, pfn, 1)) rc |= 256;
    if (ledger_owner_pages(owner) || !ledger_owner_retire(owner)) rc |= 512;
    goto out;
destroy:
    if (selftest_as_end(&as)) rc |= 512;
out:
    if (before != pgalloc_free_pages()) rc |= 1024;
    gfx2_map_selftest_result = (u32)rc;
    return rc;
}
