static u32 ledger_persist_total(void)
{
    u32 i, n = 0;
    for (i = 1; i <= LEDGER_OWNER_FIXED_LAST; i++) n += ledger_owner_pages(i);
    return n;
}

#include "caller_boot.inc"

static void test_ledger(void)
{
    struct addrspace as;
    enum { data_pages = 2 };
    const u32 expected_pages = PDE_COUNT * sizeof(u32) / PAGE_SIZE + data_pages;
    u32 persist, owner, phys, left, generation;
    int ok;

    check(kctx_irq_depth == 0 && kctx_exc_depth == 0, "ledger:ctx depth 0");
    check(ledger_selfcheck("boot"), "ledger:selfcheck boot");
    check(!ledger_resource_set_map(LEDGER_MAX_RESOURCES, 0, 0), "ledger:map bad id");
    for (u32 rid = 0; rid < LEDGER_MAX_RESOURCES; rid++) {
        const struct ledger_resource *r = &ledger_resources[rid];
        if (!r->bus) continue;
        u32 first = r->map_first, end = r->map_end;
        check(!ledger_resource_set_map(rid, r->decode_first, r->decode_end + 1) &&
              r->map_first == first && r->map_end == end, "ledger:map outside decode");
        break;
    }
    persist = ledger_persist_total();
    ok = ledger_owner_new(LEDGER_KIND_AS, 0, "kstest", &owner);
    check(ok, "ledger:AS owner new");
    if (ok) {
        ok = paging_addrspace_create(&as, owner) == 0;
        phys = ok ? pgalloc_alloc_phys(owner, data_pages) : 0;
        /* T2c: PD (PDE_COUNT entries) + data_pages。高位 PT は map 時の
         * 疎確保なので 0。create() は lease 先頭 PT も 0 (起動用の
         * create_lease() だけが 1 枚事前確保)。AS 制御はここでは stack。 */
        check(ok && phys && ledger_owner_pages(owner) == expected_pages, "ledger:AS alloc");
        if (ok) {
            if (phys) test_caller_boot(&as, phys);
            generation = as.generation;
            paging_addrspace_destroy(&as);
            ok = paging_addrspace_create(&as, owner) == 0;
            check(ok && generation && as.generation > generation,
                  "ledger:AS generation not reused");
            if (ok) paging_addrspace_destroy(&as);
        }
        left = 0;
        check(ledger_reclaim_owner(owner, &left) && left == (phys ? data_pages : 0) &&
              ledger_owner_pages(owner) == 0, "ledger:AS pages 0 (R5a)");
        check(ledger_owner_retire(owner), "ledger:AS owner retire");
    }
    check(ledger_persist_total() == persist, "ledger:persist unchanged (R5b)");
    check(ledger_selfcheck("boot"), "ledger:selfcheck after AS");
    kprintf(KLOG_ATTR, "[ledger] irq_ops=%u exc_ops=%u check_fail=%u bad_free=%u\n",
            ledger_irq_ops, ledger_exc_ops, ledger_check_fail, ledger_bad_free);
}
