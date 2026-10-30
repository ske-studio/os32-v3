static void test_appmem(void)
{
    struct addrspace as;
    u32 owner = 0, base = 0;
    int created = 0, mapped = 0;
    int ok = ledger_owner_new(LEDGER_KIND_AS, 0, "appmem", &owner);
    if (ok) created = paging_addrspace_create_lease(&as, owner) == 0;
    check(ok && created && _irq_enabled() && !kctx_irq_depth && !kctx_exc_depth,
          "appmem:owner lease AS context");
    if (created) {
        appmem_init(&as, MEM_EXEC_LOAD_ADDR + 1, MEM_EXEC_LOAD_ADDR + PAGE_SIZE,
                    MEM_EXEC_HEAP_BASE, MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE - MEM_GUARD_SIZE);
        mapped = appmem_map(&as, &as.appmem, &as.appmem_layout, PAGE_SIZE, 0,
                            0, APPMEM_ANON, 0, &base) == 0;
    }
    check(mapped && base && as.appmem.e[0].base == base, "appmem:map extent");
    {
        MemStat stat;
        int id = appslot_alloc_id();
        AppSlot *slot = appslot_at(id);
        int valid = 0;
        if (mapped && slot) {
            AppSlot saved = *slot;
            slot->state = APP_STATE_PARKED;
            slot->as = &as;
            ring3_wm_enter();
            valid = kapi_mem_stat(id, &stat, sizeof(stat)) == sizeof(stat) &&
                    stat.app_id == id && stat.flags == MEMSTAT_HAS_AS &&
                    stat.extents_total == 1 && stat.extents[APPMEM_ANON - 1] == 1 &&
                    stat.extents_free == APPMEM_EXTENT_MAX - 1 && stat.arenas == 1;
            ring3_wm_leave();
            {
                AppSlot *requester = appslot_at(APP_ID_SHELL);
                struct addrspace *saved_as = requester->as;
                u32 epoch = appslot_trim_epoch;
                u32 requests = appslot_trim_request_count, marks = appslot_trim_mark_count;
                u32 dones = appslot_trim_done_count, rejects = appslot_trim_done_reject_count;
                int last_reclaim = appslot_last_reclaim_id, stop = requester->stop_wm_req;
                requester->as = (struct addrspace *)&saved;
                slot->gui = slot->cpl3 = 1;
                appslot_trim_request_as(requester->as);
                /* ID 1 is a valid requester; app 0 remains a system query. */
                check(slot->trim_pending, "trim:mark parked GUI");
                u32 marked = slot->trim_epoch;
                appslot_trim_request_as(requester->as);
                check(slot->trim_epoch == marked && appslot_trim_mark_count == marks + 1,
                      "trim:pending coalesces");
                check(!appslot_trim_done(id, marked + 1) && appslot_trim_done(id, marked) &&
                      !appslot_trim_done(id, marked), "trim:done and stale");
                /* Eligible states are CUI here; all other states must exclude
                 * even GUI. The borrowed slot is FREE, so its redirect is empty. */
                for (int s = APP_STATE_FREE; s <= APP_STATE_FAULT_PENDING; s++) {
                    slot->state = s;
                    slot->gui = s < APP_STATE_PARKED || s > APP_STATE_WAIT_POLL;
                    appslot_trim_request_as(requester->as);
                }
                int excluded = !slot->trim_pending;
                slot->state = APP_STATE_PARKED;
                slot->gui = 1;
                appslot_trim_request_as(&as);
                excluded &= !slot->trim_pending;
                appslot_trim_request_as(requester->as);
                appslot_reclaim(id);
                check(excluded && !slot->trim_pending && !slot->trim_epoch, "trim:exclude/reclaim");
                kdi_set_appslot_reclaim_count(appslot_reclaim_count - 1);
                kdi_set_appslot_last_reclaim_id(last_reclaim);
                requester->stop_wm_req = stop;
                requester->as = saved_as;
                kdi_set_appslot_trim_epoch(epoch);
                kdi_set_appslot_trim_request_count(requests);
                kdi_set_appslot_trim_mark_count(marks);
                kdi_set_appslot_trim_done_count(dones);
                kdi_set_appslot_trim_done_reject_count(rejects);
            }
            *slot = saved;
        }
        check(valid, "mem_stat:TRUSTED AS extents");
    }
    ok = mapped && appmem_unmap(&as, &as.appmem, base, PAGE_SIZE) == 0;
    check(ok && !as.appmem.e[0].base, "appmem:unmap extent");
    if (created) paging_addrspace_destroy(&as);
    check(owner && ledger_owner_pages(owner) == 0, "appmem:owner pages 0");
    check(owner && ledger_owner_retire(owner), "appmem:owner retire");
}
