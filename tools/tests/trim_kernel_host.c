/* Complete product slots, caller/heap/paging transactions, gui and snapshot.
 * Linux MMU/IRQ and absent drivers/FDs are the fixture boundary. */
#include "mem_fixture.c"
#include "wm_source.c"

void *kmemcpy(void *dst, const void *src, u32 n)
{
    CHECK("copyout IRQ enabled", _irq_enabled());
    heap_copy(dst, src, n); return dst;
}
#include "mem_stat_source.c"

static int watch_request;
static u32 request_root, request_frees;
void appslot_trim_request_as(struct addrspace *as)
{
    if (watch_request) {
        CHECK("request after full rollback", as == &a && _irq_enabled() &&
              paging_current_cr3() == request_root && heap_free_calls == request_frees + 2 &&
              equal(&a, &f10_as, sizeof(a)) && ledger_owner_pages(a.owner) == f10_pages &&
              equal(pd_images[1], P2V(a.pd_phys), PAGE_SIZE));
        for (u32 k = 0; k < MEM_APP_BAND_MAX_PDES; k++) if (a.app_pt_phys[k])
            CHECK("request after full rollback", equal(pt_images[k], P2V(a.app_pt_phys[k]), PAGE_SIZE));
    }
    trim_product_request(as);
}

static u32 done_trim_calls;
static u32 done_trim(struct addrspace *as)
{
    done_trim_calls++;
    CHECK("DONE caller CR3", as == &a && paging_current_cr3() == as->pd_phys);
    return exec_heap_user_trim(as);
}
static int forged_other_as;
static int done_caller(struct caller_access *c)
{
    int ok = caller_access_get_user(c);
    /* Exercise the redundant AS identity guard after a valid capture. */
    if (ok && forged_other_as) c->as = &b;
    return ok;
}
#define caller_access_get_user done_caller
#define exec_heap_user_trim done_trim
#include "trim_exec_source.c"
#undef exec_heap_user_trim
#undef caller_access_get_user
void kbd_set_gui_mode(int on) { (void)on; }
void ime_set_render(void *table) { (void)table; }
#include "trim_gui_source.c"

static void check(int ok, const char *label)
{
    (void)label; CHECK("boot selftest", ok);
}
#include "trim_selftest_source.c"

static u32 wm_calls;
static i32 wm(u32 op, u32 arg, int owner)
{
    (void)arg; (void)owner;
    CHECK("WM does not receive DONE", op != GUI_OP_TRIM_DONE);
    wm_calls++; return 71;
}
static void trim_eligible(int id, struct addrspace *as, int state)
{
    g_slot[id].trim_pending = 0; g_slot[id].trim_epoch = 0;
    g_slot[id].as = as; g_slot[id].state = state;
    g_slot[id].gui = g_slot[id].cpl3 = 1;
}

static void mark_cases(void)
{
    const int states[] = {APP_STATE_PARKED, APP_STATE_WAIT_KEY, APP_STATE_WAIT_POLL};
    for (u32 i = 0; i < 3; i++) {
        trim_eligible(2, &a, APP_STATE_PARKED); trim_eligible(3, &b, states[i]);
        u32 req = appslot_trim_request_count, marks = appslot_trim_mark_count;
        appslot_trim_request_as(&a);
        CHECK("requester excluded", !g_slot[2].trim_pending);
        CHECK("back states marked", g_slot[3].trim_pending &&
              g_slot[3].trim_epoch == appslot_trim_epoch && appslot_trim_mark_count == marks + 1 &&
              appslot_trim_request_count == req + 1);
        u32 epoch = g_slot[3].trim_epoch;
        appslot_trim_request_as(&a);
        CHECK("pending coalesces", g_slot[3].trim_pending == 1 &&
              g_slot[3].trim_epoch == epoch && appslot_trim_mark_count == marks + 1);
    }
    for (int mode = 0; mode < 9; mode++) {
        trim_eligible(3, &b, APP_STATE_PARKED);
        u32 pd = b.pd_phys;
        if (mode == 0) g_slot[3].gui = 0;
        if (mode == 1) g_slot[3].cpl3 = 0;
        if (mode == 2) g_slot[3].state = APP_STATE_RUNNING;
        if (mode == 3) g_slot[3].state = APP_STATE_FREE;
        if (mode == 4) g_slot[3].state = APP_STATE_ABORT_PENDING;
        if (mode == 5) g_slot[3].state = APP_STATE_FAULT_PENDING;
        if (mode == 6) g_slot[3].as = 0;
        if (mode == 7) b.pd_phys = 0;
        if (mode == 8) b.appmem_poisoned = 1;
        u32 marks = appslot_trim_mark_count;
        appslot_trim_request_as(&a);
        CHECK("ineligible excluded", !g_slot[3].trim_pending && appslot_trim_mark_count == marks);
        b.pd_phys = pd; b.appmem_poisoned = 0;
    }
    trim_eligible(3, &b, APP_STATE_PARKED);
    struct addrspace fake = {0};
    u32 req = appslot_trim_request_count, marks = appslot_trim_mark_count;
    appslot_trim_request_as(&fake);
    CHECK("unregistered AS counts only", appslot_trim_request_count == req + 1 &&
          appslot_trim_mark_count == marks && !g_slot[3].trim_pending);
    appslot_trim_epoch = ~0U;
    for (int i = 0; i < 2; i++) {
        appslot_trim_request_as(&a);
        CHECK("epoch saturation", appslot_trim_epoch == ~0U &&
              g_slot[3].trim_pending && g_slot[3].trim_epoch == ~0U && appslot_trim_done(3, ~0U));
    }
    g_slot[3].trim_pending = 1; g_slot[3].trim_epoch = 91;
    appslot_start_commit(3, 1, 17);
    CHECK("start clears trim", !g_slot[3].trim_pending && !g_slot[3].trim_epoch);
    g_slot[3].trim_pending = 1; g_slot[3].trim_epoch = 92;
    CHECK("reclaim returns pages", appslot_reclaim(3) == 17);
    CHECK("reclaim clears trim", g_slot[3].state == APP_STATE_FREE &&
          !g_slot[3].trim_pending && !g_slot[3].trim_epoch);
    trim_eligible(2, &a, APP_STATE_RUNNING); trim_eligible(3, &b, APP_STATE_PARKED);
    heap_select(2);
}

static void pressure_cases(void)
{
    struct appmem_table saved = a.appmem;
    struct appmem_layout l = a.appmem_layout;
    u32 req = appslot_trim_request_count, out = 0xabc;
    CHECK("prepare EINVAL", appmem_map(&a, &a.appmem, &l, 0, 0, 0, APPMEM_ANON, 0, &out) == APPMEM_EINVAL);
    l.primary_mapped_end = MEM_EXEC_HEAP_BASE;
    CHECK("prepare ENOVA", appmem_map(&a, &a.appmem, &l, PAGE_SIZE, 0, 0, APPMEM_ANON, 0, &out) == APPMEM_ENOVA);
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++)
        a.appmem.e[i] = (struct appmem_extent){MEM_EXEC_LOAD_ADDR + (4 + 2*i)*PAGE_SIZE,
            MEM_EXEC_LOAD_ADDR + (5 + 2*i)*PAGE_SIZE, APPMEM_ANON, 0};
    CHECK("prepare EFULL", appmem_map(&a, &a.appmem, &a.appmem_layout, PAGE_SIZE,
          MEM_EXEC_LOAD_ADDR + 100*PAGE_SIZE, APPMEM_MAP_EXACT, APPMEM_ANON, 0, &out) == APPMEM_EFULL);
    a.appmem = saved;
    kctx_irq_depth = 1;
    CHECK("stage context EINVAL", appmem_map(&a, &a.appmem, &a.appmem_layout, PAGE_SIZE,
          0, 0, APPMEM_ANON, 0, &out) == APPMEM_EINVAL);
    kctx_irq_depth = 0;
    CHECK("prepare errors do not mark", appslot_trim_request_count == req && !g_slot[3].trim_pending && out == 0xabc);
    /* Fail after PT and data allocations; the request must see a full rollback. */
    f10_snapshot(0);
    u32 root = paging_current_cr3(), frees = heap_free_calls;
    request_root = root; request_frees = frees; watch_request = 1;
    heap_alloc_rollback_fail = 3;
    CHECK("stage ENOSPC", appmem_map(&a, &a.appmem, &a.appmem_layout, 3*PAGE_SIZE,
          MEM_EXEC_LOAD_ADDR + MEM_APP_BAND_PDE_SIZE, APPMEM_MAP_EXACT, APPMEM_ANON, 0, &out) == APPMEM_ENOSPC);
    watch_request = 0;
    CHECK("rollback frees before mark", heap_free_calls == frees + 2 && !a.appmem_poisoned &&
          appslot_trim_request_count == req + 1 && g_slot[3].trim_pending && out == 0xabc &&
          paging_current_cr3() == root && _irq_enabled());
    /* f10_snapshot includes free-call count, so compare AS/PTE/page ownership here. */
    CHECK("rollback AS and pages", equal(&a, &f10_as, sizeof(a)) && ledger_owner_pages(a.owner) == f10_pages);
    CHECK("public NOSPC", appmem_error_public(APPMEM_ENOSPC) == OS32_ERR_NOSPC);
    g_slot[3].trim_pending = 0;
}

static void stat_case(u32 cur_end)
{
    MemStat s;
    CHECK("system snapshot", kapi_mem_stat(0, &s, sizeof(s)) == sizeof(s) &&
          s.app_id == 0 && s.pressure_epoch == appslot_trim_epoch &&
          s.trim_pending_mask == (g_slot[2].trim_pending ? 1UL << 2 : 0) && !s.trim_epoch);
    CHECK("app snapshot", kapi_mem_stat(-1, &s, sizeof(s)) == sizeof(s) &&
          s.trim_epoch == g_slot[2].trim_epoch && s.exec_heap_cur_end == cur_end &&
          s.exec_heap_used == a.exec_heap_used &&
          !!(s.flags & MEMSTAT_TRIM_PENDING) == !!g_slot[2].trim_pending);
    union { MemStat align; u8 bytes[sizeof(MemStat) + 16]; } old;
    for (u32 i = 0; i < sizeof(old); i++) old.bytes[i] = 0xa5;
    CHECK("legacy 120 size", kapi_mem_stat(-1, &old, 120) == 120 && old.align.size == 120);
    s.size = 120;
    CHECK("legacy prefix", equal(&old, &s, 120));
    for (u32 i = 120; i < sizeof(old); i++) CHECK("legacy new fields untouched", old.bytes[i] == 0xa5);
}

static void done_cases(void)
{
    void *initial = exec_heap_user_alloc(&a, MEM_EXEC_HEAP_MIN - BLK_HDR_SIZE);
    void *p = exec_heap_user_alloc(&a, 128);
    u32 arena = (u32)p - BLK_HDR_SIZE, old_end = a.appmem_layout.exec_heap_cur_end;
    CHECK("heap growth fixture", initial && p && old_end == arena + MEM_EXEC_HEAP_MIN);
    f10_pattern(p, 128, 1);
    heap_select(3); g_slot[2].state = APP_STATE_PARKED;
    /* Real ENOSPC caused by the foreground requester records the background heap. */
    heap_alloc_fail = 1;
    u32 mapped;
    CHECK("back heap pressure", appmem_map(&b, &b.appmem, &b.appmem_layout, PAGE_SIZE, 0, 0,
          APPMEM_ANON, 0, &mapped) == APPMEM_ENOSPC);
    heap_alloc_fail = 0;
    g_slot[3].state = APP_STATE_PARKED; g_slot[2].state = APP_STATE_RUNNING;
    heap_select(2);
    u32 epoch = g_slot[2].trim_epoch;
    CHECK("back heap marked", g_slot[2].trim_pending && !g_slot[3].trim_pending);
    stat_case(old_end);
    u32 trims = done_trim_calls, rejects = appslot_trim_done_reject_count;
    f10_snapshot(0);
    CHECK("stale immutable", gui_call(GUI_OP_TRIM_DONE, epoch - 1) == OS32_ERR_STALE &&
          done_trim_calls == trims && g_slot[2].trim_pending && appslot_trim_done_reject_count == rejects + 1);
    f10_snapshot(1);
    for (int mode = 0; mode < 9; mode++) {
        CallerAccessFrame saved; caller_access_save(&saved);
        u32 root = host_cr3;
        if (mode == 0) ring3_wm_enter();
        if (mode == 1) kctx_irq_depth = 1;
        if (mode == 2) kctx_exc_depth = 1;
        if (mode == 3) caller_access_invalidate();
        if (mode == 4) host_cr3 = b.pd_phys;
        if (mode == 5) g_slot[2].as = &b;
        if (mode == 6) g_slot[2].gui = 0;
        if (mode == 7) g_slot[2].cpl3 = 0;
        if (mode == 8) { CallerAccessFrame previous; CHECK("trusted frame", caller_access_enter(&previous, CALLER_TRUSTED)); }
        CHECK("context refused", gui_call(GUI_OP_TRIM_DONE, epoch) == OS32_ERR_INVAL &&
              done_trim_calls == trims && g_slot[2].trim_pending);
        if (mode == 0) ring3_wm_leave();
        kctx_irq_depth = kctx_exc_depth = 0; host_cr3 = root;
        g_slot[2].as = &a; g_slot[2].gui = g_slot[2].cpl3 = 1;
        caller_access_leave(&saved);
    }
    forged_other_as = 1;
    CHECK("other AS refused", gui_call(GUI_OP_TRIM_DONE, epoch) == OS32_ERR_INVAL && done_trim_calls == trims);
    forged_other_as = 0;
    f10_snapshot(1);
    u32 pages = ledger_owner_pages(a.owner), used = a.exec_heap_used;
    u32 switches = ring3_switch_count, cr3_reloads = reloads;
    struct addrspace other = b;
    heap_copy(heap_resident_image, (void *)MEM_EXEC_HEAP_BASE, MEM_EXEC_HEAP_MIN);
    g_gui_handler = 0;
    i32 result = gui_call(GUI_OP_TRIM_DONE, epoch);
    CHECK("no WM DONE", result == 15);
    CHECK("DONE accepted", done_trim_calls == trims + 1 && !g_slot[2].trim_pending &&
          appslot_trim_pages_total == (u32)result && ledger_owner_pages(a.owner) == pages - result);
    CHECK("trim layout", a.appmem_layout.exec_heap_cur_end == arena + PAGE_SIZE && a.exec_heap_used == used);
    CHECK("INITIAL immutable", equal(heap_resident_image, (void *)MEM_EXEC_HEAP_BASE, MEM_EXEC_HEAP_MIN));
    CHECK("other AS and switch unchanged", equal(&other, &b, sizeof(b)) &&
          ring3_switch_count == switches && reloads == cr3_reloads && paging_current_cr3() == a.pd_phys);
    f10_pattern(p, 128, 0); stat_case(arena + PAGE_SIZE);
    CHECK("duplicate DONE stale", gui_call(GUI_OP_TRIM_DONE, epoch) == OS32_ERR_STALE && done_trim_calls == trims + 1);
    g_gui_handler = wm;
    CHECK("OWNER_EXIT refused", gui_call(GUI_OP_OWNER_EXIT, 0) == OS32_ERR_INVAL && !wm_calls);
    /* USED tail cannot be returned; its pattern survives the DONE path. */
    void *q = exec_heap_user_alloc(&a, MEM_EXEC_HEAP_MIN - BLK_HDR_SIZE);
    CHECK("EXACT regrowth", (u32)q == arena + PAGE_SIZE + BLK_HDR_SIZE);
    f10_pattern(q, MEM_EXEC_HEAP_MIN - BLK_HDR_SIZE, 1);
    g_slot[2].trim_pending = 1; g_slot[2].trim_epoch = ++appslot_trim_epoch;
    CHECK("USED tail retained", gui_call(GUI_OP_TRIM_DONE, appslot_trim_epoch) == 0);
    f10_pattern(q, MEM_EXEC_HEAP_MIN - BLK_HDR_SIZE, 0);
    CHECK("registered WM never receives DONE", !wm_calls);
    exec_heap_user_free(&a, q); exec_heap_user_free(&a, p);
    exec_heap_user_free(&a, initial);
}

static void fallback_cases(void)
{
    /* Recreate a clean heap for EXACT 17 pages vs TOPDOWN 16 pages. */
    f10_done(); heap_create(&a, 2); g_slot[2].gui = 1;
    void *initial = exec_heap_user_alloc(&a, MEM_EXEC_HEAP_MIN - BLK_HDR_SIZE);
    u32 pages = ledger_owner_pages(a.owner), maps = heap_map_calls;
    void *p = exec_heap_user_alloc(&a, MEM_EXEC_HEAP_MIN - 1);
    CHECK("EXACT 17 pages", initial && (u32)p == MEM_EXEC_HEAP_BASE + MEM_EXEC_HEAP_MIN + BLK_HDR_SIZE &&
          a.appmem_layout.exec_heap_cur_end == MEM_EXEC_HEAP_BASE + MEM_EXEC_HEAP_MIN + 17*PAGE_SIZE &&
          ledger_owner_pages(a.owner) == pages + 17 && heap_map_calls == maps + 1 && f10_extent((u32)p)->end - f10_extent((u32)p)->base == 17*PAGE_SIZE);
    exec_heap_user_free(&a, p); f10_done(); heap_create(&a, 2); g_slot[2].gui = 1;
    CHECK("TOPDOWN initial", exec_heap_user_alloc(&a, MEM_EXEC_HEAP_MIN - BLK_HDR_SIZE));
    u32 collision;
    CHECK("TOPDOWN collision", !appmem_map(&a, &a.appmem, &a.appmem_layout, PAGE_SIZE,
          a.appmem_layout.exec_heap_cur_end, APPMEM_MAP_EXACT, APPMEM_ANON, 0, &collision));
    pages = ledger_owner_pages(a.owner); maps = heap_map_calls;
    p = exec_heap_user_alloc(&a, 128);
    CHECK("TOPDOWN 16 pages", (u32)p == a.appmem_layout.guard_b - MEM_EXEC_HEAP_MIN + BLK_HDR_SIZE &&
          ledger_owner_pages(a.owner) == pages + 17 && heap_map_calls == maps + 2 && f10_extent((u32)p)->end - f10_extent((u32)p)->base == 16*PAGE_SIZE);
    /* 16 data pages plus a new PT in the high PDE. */
    exec_heap_user_free(&a, p); f10_done();
}

static void boot_cases(void)
{
    appslot_init();
    AppSlot before[APP_SLOT_COUNT]; heap_copy(before, g_slot, sizeof(before));
    FdRedirectState redirects[APP_SLOT_COUNT]; heap_copy(redirects, g_redir, sizeof(redirects));
    u32 epoch = appslot_trim_epoch, requests = appslot_trim_request_count, marks = appslot_trim_mark_count;
    u32 dones = appslot_trim_done_count, rejects = appslot_trim_done_reject_count;
    u32 reclaim = appslot_reclaim_count; int last = appslot_last_reclaim_id;
    host_cr3 = roots[0]; ring3_in_syscall = 0; caller_access_invalidate();
    test_appmem();
    CHECK("boot borrowed state restored", equal(before, g_slot, sizeof(before)) &&
          equal(redirects, g_redir, sizeof(redirects)) && appslot_trim_epoch == epoch &&
          appslot_trim_request_count == requests && appslot_trim_mark_count == marks &&
          appslot_trim_done_count == dones && appslot_trim_done_reject_count == rejects &&
          appslot_reclaim_count == reclaim && appslot_last_reclaim_id == last && !ring3_wm_depth);
}

void _start(void)
{
    heap_ram(); host_map_fixed_paging(); paging_init(17408); host_pool_boot(17408);
    roots[0] = paging_kernel_pd_phys(); integration = 1; heap_alias_on = 1;
    heap_mmap(MEM_SHELL_HEAP_BASE, MEM_SHELL_HEAP_SIZE, 0);
    exec_heap_init_at(MEM_SHELL_HEAP_BASE, MEM_SHELL_HEAP_SIZE);
    appslot_init(); ring3_in_syscall = 1;
    heap_create(&b, 3); roots[2] = b.pd_phys;
    heap_create(&a, 2); roots[1] = a.pd_phys;
    mark_cases(); pressure_cases(); done_cases(); fallback_cases();
    host_cr3 = roots[0]; heap_alias_on = 0;
    paging_addrspace_free_user_range(&b, MEM_EXEC_HEAP_BASE, MEM_EXEC_HEAP_BASE + MEM_EXEC_HEAP_MIN);
    paging_addrspace_destroy(&b);
    CHECK("other owner retired", ledger_owner_retire(b.owner));
    boot_cases();
    say("PASS trim_kernel CHECK=", sizeof("PASS trim_kernel CHECK=") - 1); number(checks); say("\n", 1);
    die(0);
}
