/* Exhaust only free SLACK, keeping a snapshot of every live owner. The
 * snapshot lives on the test stack, never in the resident kernel BSS. */
static void test_slack_cycle(void)
{
    u8 saved[(MEM_FIXED_PAGING_GUARD_BASE - KERNEL_LOAD_ADDR) / PAGE_SIZE];
    u32 first = memory_boot_slack_first();
    u32 end = MEM_FIXED_PAGING_GUARD_BASE / PAGE_SIZE;
    u32 before = pgalloc_free_pages(), failures = ksel_fail;
    u32 owner, free = 0, busy = 0, taken = 0, pfn;
    int ok = ledger_owner_new(LEDGER_KIND_AS, 0, "slack", &owner);
    check(ok, "slack_cycle: owner");
    if (!ok) return;
    for (u32 p = first; p < end; p++) {
        u32 old = 0;
        for (u32 o = 1; o < LEDGER_MAX_OWNERS; o++)
            if (pgalloc_page_owned(p, o)) { old = o; break; }
        saved[p - first] = (u8)old;
        if (old) busy++;
        else free++;
    }
    for (u32 p = first; p < end; p++) {
        ok = pgalloc_alloc_n_owner(owner, 1, p, p + 1, LEDGER_BOTTOM_UP, &pfn);
        if (saved[p - first]) {
            check(!ok && pgalloc_page_owned(p, saved[p - first]),
                  "slack_cycle: live page retained");
        } else {
            check(ok && pfn == p && pgalloc_page_owned(p, owner),
                  "slack_cycle: free page allocated");
            taken += ok != 0;
        }
        check(!pgalloc_alloc_n_owner(owner, 1, p, p + 1, LEDGER_BOTTOM_UP, &pfn),
              "slack_cycle: second allocation refused");
    }
    check(taken == free && pgalloc_free_pages() == before - free,
          "slack_cycle: exhausted");
    check(!pgalloc_alloc_n_owner(owner, 1, first, end, LEDGER_BOTTOM_UP, &pfn),
          "slack_cycle: whole range exhausted");
    for (u32 p = first; p < end; p++) {
        if (!saved[p - first] && pgalloc_page_owned(p, owner))
            check(pgalloc_free_n_owner(owner, p, 1), "slack_cycle: return page");
        if (saved[p - first])
            check(pgalloc_page_owned(p, saved[p - first]), "slack_cycle: owner unchanged");
    }
    check(pgalloc_free_pages() == before && ledger_owner_pages(owner) == 0,
          "slack_cycle: free count restored");
    check(ledger_owner_retire(owner), "slack_cycle: retire");
    kprintf(KLOG_ATTR, "[selftest] slack_cycle free=%u busy=%u taken=%u fail=%u\n",
            free, busy, taken, ksel_fail - failures);
}
