/* Real allocator and real caller frames; only Linux/MMU is simulated. */
#include "mem_fixture.c"
#include "wm_source.c"
void *kmemcpy(void *dst, const void *src, u32 n)
{
    CHECK("copyout IRQ restored", _irq_enabled());
    u8 *d = dst; const u8 *s = src;
    while (n--) *d++ = *s++;
    return dst;
}
#include "mem_stat_source.c"

static union { MemStat stat; u8 bytes[sizeof(MemStat) + 16]; } output;
static void query(int id, int expected, int target)
{
    u8 before[sizeof(output)];
    for (u32 i = 0; i < sizeof(output); i++) before[i] = output.bytes[i] = 0xa5;
    int rc = kapi_mem_stat(id, &output, sizeof(output));
    CHECK("caller matrix", rc == expected);
    if (rc < 0) CHECK("error output unchanged", equal(before, &output, sizeof(output)));
    else {
        CHECK("caller target", output.stat.app_id == target);
        CHECK("caller flags", !!(output.stat.flags & MEMSTAT_HAS_AS) == !!target);
        if (!target) CHECK("system AS zero", all_zero(output.bytes + MEMSTAT_MIN, sizeof(MemStat) - MEMSTAT_MIN));
    }
}

static void matrix(void)
{
    /* IDs: 2 self, 3 live, 4 FREE (with stale AS), 5 live without AS. */
    const int ids[] = {0, -1, 2, 3, 4, 5, 1, 99, -2};
    const int trusted[] = {120, 120, 120, 120, OS32_ERR_NOTFOUND, OS32_ERR_NOTFOUND,
                           OS32_ERR_INVAL, OS32_ERR_INVAL, OS32_ERR_INVAL};
    const int user[] = {120, 120, 120, OS32_ERR_INVAL, OS32_ERR_INVAL, OS32_ERR_INVAL,
                        OS32_ERR_INVAL, OS32_ERR_INVAL, OS32_ERR_INVAL};
    g_slot[1].state = APP_STATE_RUNNING;
    g_slot[4] = g_slot[2]; g_slot[4].state = APP_STATE_FREE;
    g_slot[5].state = APP_STATE_RUNNING;
    for (int mode = 0; mode < 6; mode++) {
        ring3_in_syscall = 1; heap_select(2);
        struct caller_identity identity = caller_identity_get();
        CHECK("identity captured", identity.app_id == 2 && identity.owner == a.owner && identity.generation == a.generation);
        u32 owner = a.owner, generation = a.generation;
        if (mode == 0) ring3_wm_enter();
        if (mode == 1) { ring3_in_syscall = 0; current_slot = APP_ID_SHELL; caller_access_invalidate(); }
        if (mode == 3) a.generation++;
        if (mode == 4) a.owner++;
        if (mode == 5) g_slot[2].as = 0;
        for (u32 j = 0; j < sizeof(ids) / sizeof(ids[0]); j++) {
            int target = ids[j];
            if (target == -1) target = mode < 2 ? 0 : 2;
            query(ids[j], mode < 2 ? trusted[j] : mode == 2 ? user[j] : OS32_ERR_INVAL, target);
        }
        if (mode == 0) {
            ring3_wm_leave();
            struct caller_identity after = caller_identity_get();
            CHECK("WM identity preserved", equal(&identity, &after, sizeof(after)));
        }
        a.owner = owner; a.generation = generation; g_slot[2].as = &a;
    }
    heap_select(2);
    ring3_wm_enter();
    for (u32 state = APP_STATE_RUNNING; state <= APP_STATE_FAULT_PENDING; state++) {
        g_slot[3].state = state;
        query(3, sizeof(MemStat), 3);
        CHECK("live state", output.stat.state == state);
    }
    u32 pd = b.pd_phys; b.pd_phys = 0;
    query(3, OS32_ERR_NOTFOUND, 3); b.pd_phys = pd;
    g_slot[3].state = APP_STATE_RUNNING;
    ring3_wm_leave();
}

static void sizes(void)
{
    const u32 sizes[] = {0, MEMSTAT_MIN - 1, MEMSTAT_MIN, 73, sizeof(MemStat), sizeof(output)};
    MemStat full;
    CHECK("full size", kapi_mem_stat(-1, &full, sizeof(full)) == sizeof(full));
    CHECK("NULL direct", kapi_mem_stat(-1, 0, sizeof(full)) == OS32_ERR_INVAL);
    for (u32 j = 0; j < sizeof(sizes) / sizeof(sizes[0]); j++) {
        u32 n = sizes[j], copied = n < MEMSTAT_MIN ? 0 : n < sizeof(full) ? n : sizeof(full);
        for (u32 i = 0; i < sizeof(output); i++) output.bytes[i] = 0xa5;
        int rc = kapi_mem_stat(-1, &output, n);
        CHECK("size prefix", rc == (copied ? (int)copied : OS32_ERR_INVAL));
        full.size = copied;
        if (copied) CHECK("size all bytes", equal(&full, &output, copied));
        for (u32 i = copied; i < sizeof(output); i++) CHECK("size tail", output.bytes[i] == 0xa5);
    }
}

static AppSlot slots_image[APP_SLOT_COUNT];
static u8 ledger_image[sizeof(ledger_owners)], page_image[sizeof(host_pool_storage)];
static void values(void)
{
    MemStat stat;
    struct addrspace before = a;
    KHeap resident = exec_heap;
    heap_copy(slots_image, g_slot, sizeof(g_slot));
    heap_copy(ledger_image, ledger_owners, sizeof(ledger_owners));
    heap_copy(page_image, host_pool_storage, sizeof(host_pool_storage));
    heap_other_snapshot(0); f10_snapshot(0);
    CHECK("values query", kapi_mem_stat(-1, &stat, sizeof(stat)) == sizeof(stat));
    heap_other_snapshot(1); f10_snapshot(1);
    CHECK("readonly AS slot heap ledger", equal(&before, &a, sizeof(a)) &&
          equal(slots_image, g_slot, sizeof(g_slot)) && equal(&resident, &exec_heap, sizeof(exec_heap)) &&
          equal(ledger_image, ledger_owners, sizeof(ledger_owners)) && equal(page_image, host_pool_storage, sizeof(host_pool_storage)));
    u32 kinds[5] = {0}, total = 0;
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++) if (a.appmem.e[i].kind) {
        kinds[a.appmem.e[i].kind - 1]++; total++;
    }
    CHECK("extent kinds", equal(kinds, stat.extents, sizeof(kinds)));
    CHECK("extent total", stat.extents_total == total);
    CHECK("extent free", stat.extents_free == APPMEM_EXTENT_MAX - total);
    CHECK("arena sum", stat.arenas == kinds[2] + kinds[3]);
    CHECK("AS used", stat.exec_heap_used == (a.exec_heap_used == ~0U ? 0 : a.exec_heap_used));
    CHECK("heap invalid", !!(stat.flags & MEMSTAT_HEAP_INVALID) == (a.exec_heap_used == ~0U));
    CHECK("poison flag", !!(stat.flags & MEMSTAT_POISONED) == !!a.appmem_poisoned);
    CHECK("current end", stat.exec_heap_cur_end == a.appmem_layout.exec_heap_cur_end);
    CHECK("layout", stat.img_end == a.appmem_layout.img_end && stat.primary_mapped_end == a.appmem_layout.primary_mapped_end && stat.guard_b == a.appmem_layout.guard_b);
    CHECK("slot values", stat.load_addr == g_slot[2].load_addr && stat.sbrk_heap_limit == g_slot[2].sbrk_heap_limit && stat.exec_heap_base == g_slot[2].exec_heap_base && stat.exec_heap_size == g_slot[2].exec_heap_size && stat.stack_top == g_slot[2].stack_top && stat.stack_size == g_slot[2].stack_size);
    CHECK("system values", stat.resident_heap_total == exec_heap_total() && stat.resident_heap_used == exec_heap_used() && stat.phys_total_pages == pgalloc_total_pages() && stat.phys_free_pages == pgalloc_free_pages() && stat.kheap_total == kmalloc_total() && stat.kheap_used == kmalloc_used() && stat.kheap_free == kmalloc_free());
}

void _start(void)
{
    heap_ram();
    host_map_fixed_paging(); paging_init(17408); host_pool_boot(17408);
    roots[0] = paging_kernel_pd_phys();
    integration = 1; heap_alias_on = 1;
    heap_mmap(MEM_SHELL_HEAP_BASE, MEM_SHELL_HEAP_SIZE, 0);
    exec_heap_init_at(MEM_SHELL_HEAP_BASE, MEM_SHELL_HEAP_SIZE);
    CHECK("resident fixture", exec_heap_alloc(40));
    ring3_in_syscall = 1;
    heap_create(&b, 3); roots[2] = b.pd_phys;
    heap_create(&a, 2); roots[1] = a.pd_phys;
    g_slot[2].exec_heap_used = 12345; /* saved resident usage != AS usage */
    g_slot[2].load_addr = MEM_EXEC_LOAD_ADDR;
    g_slot[2].sbrk_heap_limit = a.appmem_layout.primary_mapped_end;
    g_slot[2].stack_top = MEM_APP_STACK_TOP; g_slot[2].stack_size = MEM_EXEC_STACK_SIZE;
    matrix(); sizes(); values();
    u32 anon[2];
    for (u32 i = 0; i < 2; i++) {
        CHECK("ANON fixture", !appmem_map(&a, &a.appmem, &a.appmem_layout, PAGE_SIZE,
              MEM_EXEC_LOAD_ADDR + (4 + 2*i)*PAGE_SIZE, APPMEM_MAP_EXACT, APPMEM_ANON, 0, &anon[i]));
        values();
    }
    CHECK("initial full", exec_heap_user_alloc(&a, MEM_EXEC_HEAP_MIN - BLK_HDR_SIZE)); values();
    CHECK("ARENA fixture", exec_heap_user_alloc(&a, 128)); values();
    CHECK("LARGE fixture", exec_heap_user_alloc(&a, MEM_EXEC_HEAP_MIN)); values();
    CHECK("unmap fixture", !appmem_unmap(&a, &a.appmem, anon[0], PAGE_SIZE)); values();
    a.exec_heap_used = ~0U; values();
    a.appmem_poisoned = 1; values();
    say("PASS mem_stat CHECK=", sizeof("PASS mem_stat CHECK=") - 1); number(checks); say("\n", 1);
    die(0);
}
