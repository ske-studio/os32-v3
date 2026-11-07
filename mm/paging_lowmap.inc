/* Generic low PT0 transaction; no guest/session or machine constants. */
static struct legacy_lowmap_snapshot *lowmap_token;
int paging_lowmap_busy(void) { return lowmap_token != 0; }

int paging_lowmap_snapshot(struct legacy_lowmap_snapshot *s, u32 *saved_pte,
                          u32 page_count, u32 limit)
{
    if (lowmap_token || !pg_enabled || !page_tables[0] || !saved_pte ||
        !page_count || page_count > PTE_COUNT || !limit ||
        limit > page_count * PAGE_SIZE || (limit & (PAGE_SIZE - 1))) return -1;
    s->saved_pte = saved_pte;
    s->page_count = page_count;
    s->limit = limit;
    s->root = paging_current_cr3();
    s->master_pde = page_directory[0];
    s->active_pde = ((u32 *)P2V(s->root))[0];
    for (u32 i = 0; i < s->page_count; i++)
        s->saved_pte[i] = page_tables[0][i];
    lowmap_token = s;
    return 0;
}

/* Restore is allocation-free and cannot leave a live backing alias behind.
 * Count a missing session, then restore the shared PT directly. No PDE USER
 * propagation to the current CR3 (which may differ from the captured CR3),
 * and only one TLB flush after all saved PTEs/PDEs have been written. */
int paging_lowmap_restore(struct legacy_lowmap_snapshot *s)
{
    u32 bad = 0;
    if (lowmap_token && lowmap_token != s) return -1;
    s->restore_pte_bad = s->restore_pde_bad = 0;
    for (u32 i = 0; i < s->page_count; i++) {
        u32 saved = s->saved_pte[i];
        if (lowmap_token != s) bad++;
        page_tables[0][i] = saved;
    }
    page_directory[0] = s->master_pde;
    ((u32 *)P2V(s->root))[0] = s->active_pde;
    arch_mmu_flush_tlb();
    for (u32 i = 0; i < s->page_count; i++)
        if (page_tables[0][i] != s->saved_pte[i]) bad++;
    s->restore_pte_bad = bad; /* Includes a rejected session/PTE restore. */
    if (page_directory[0] != s->master_pde ||
        ((u32 *)P2V(s->root))[0] != s->active_pde) {
        s->restore_pde_bad++;
        bad++;
    }
    lowmap_token = 0;
    return (int)bad;
}

/* Owning transaction only: never promote outside its captured limit. */
int paging_lowmap_map_range(struct legacy_lowmap_snapshot *s, u32 base, u32 end, u32 phys, u32 flags)
{
    u32 a;
    if (!lowmap_token || lowmap_token != s || base >= end || end > s->limit ||
        ((base | end | phys) & (PAGE_SIZE - 1)) ||
        phys > ~(u32)0 - (end - base - 1) || !page_tables[0]) return -1;
    for (a = base; a < end; a += PAGE_SIZE, phys += PAGE_SIZE)
        set_page_noflush(a, phys, flags);
    if (flags & PTE_USER)
        ((u32 *)P2V(paging_current_cr3()))[0] |= PTE_USER;
    if (pg_enabled) arch_mmu_flush_tlb();
    return 0;
}

