/* Real allocator and real caller frames; only Linux/MMU is simulated. */
#include "mem_fixture.c"
#include "wm_source.c"
#define kstack_high_water actual_kstack_high_water
#include "kernel/kstack_hw.c"
#undef kstack_high_water
static u32 expected_stack_water, stack_scans;
static u32 saved_host_sp __attribute__((used));
u32 kstack_high_water(void) {
    CHECK("kstack scan IRQ enabled", _irq_enabled());
    stack_scans++;
    return actual_kstack_high_water();
}
static void __attribute__((noinline)) deep_stack(void) {
    volatile u8 bytes[8192];
    for (u32 i = 0; i < sizeof(bytes); i++) bytes[i] = 0;
}
static void __attribute__((noinline)) boot_stack(void) {
    kstack_hw_init();
    u32 initial = actual_kstack_high_water();
    CHECK("boot stack under 4KiB", initial > 0 && initial < PAGE_SIZE);
    deep_stack();
    expected_stack_water = actual_kstack_high_water();
    CHECK("stack monotonic", expected_stack_water > initial && expected_stack_water >= 8192);
    CHECK("stack retained", actual_kstack_high_water() == expected_stack_water);
}
static void __attribute__((noinline)) shell_stack(void) {
    u32 initial = actual_kstack_high_water();
    CHECK("shell stack under 4KiB", (initial >> 16) > 0 && (initial >> 16) < PAGE_SIZE);
    deep_stack();
    u32 water = actual_kstack_high_water();
    CHECK("shell stack monotonic", (water >> 16) > (initial >> 16) && (water >> 16) >= 8192);
    CHECK("shell leaves fixed stack", (water & 0xffffU) == expected_stack_water);
    expected_stack_water = water;
}
static void on_stack(void (*entry)(void), u32 top) {
    __asm__ volatile("movl %%esp, saved_host_sp\nmovl %%edx, %%esp\ncall *%%eax\nmovl saved_host_sp, %%esp"
                     : "+a"(entry), "+d"(top) : : "ecx", "memory", "cc");
}
static void stack_diagnostics(void) {
    heap_mmap(MEM_KSTACK_BASE, MEM_SHELL_LOAD_ADDR - MEM_KSTACK_BASE, MEM_KSTACK_BASE);
    heap_mmap(SHELL_STACK_BASE, MEM_SHELL_STACK_SIZE, SHELL_STACK_BASE);
    on_stack(boot_stack, MEM_KSTACK_TOP);
    CHECK("shell stack initially unused", (actual_kstack_high_water() >> 16) == 0);
    on_stack(shell_stack, MEM_SHELL_STACK_TOP);
    /* A later frame may contain the marker value; an observed peak cannot fall. */
    volatile u32 *p = P2V(MEM_KSTACK_BASE), *end = P2V(MEM_KSTACK_TOP);
    while (p < end) *p++ = KSTACK_MARK;
    CHECK("stack retained after marker reuse", actual_kstack_high_water() == expected_stack_water);
    p = P2V(SHELL_STACK_BASE); end = P2V(MEM_SHELL_STACK_TOP);
    while (p < end) *p++ = KSTACK_MARK;
    CHECK("shell retained after marker reuse", actual_kstack_high_water() == expected_stack_water);
}

volatile u32 fault_kill_count = 11, appslot_reclaim_count = 12, ring3_stop_park_count = 13;
u32 memmap_audit_runs = 14, as_audit_runs = 15, v86_return_audit_runs = 16;
u32 memmap_audit_fail = 17, as_audit_fail = 18, v86_return_audit_fail = 19;
int kselftest_pass = 295, kselftest_fail = 0;
void *kmemcpy(void *dst, const void *src, u32 n)
{
    CHECK("copyout IRQ restored", _irq_enabled());
    u8 *d = dst; const u8 *s = src;
    while (n--) *d++ = *s++;
    return dst;
}
static u32 snapshot_free_pages(void)
{
    CHECK("snapshot IRQ disabled", !_irq_enabled());
    return pgalloc_free_pages();
}
static u32 snapshot_kheap_total(void)
{
    CHECK("KHEAP IRQ enabled", _irq_enabled());
    return kmalloc_total();
}
static u32 snapshot_kheap_used(void)
{
    CHECK("KHEAP IRQ enabled", _irq_enabled());
    return kmalloc_used();
}
static u32 snapshot_kheap_free(void)
{
    CHECK("KHEAP IRQ enabled", _irq_enabled());
    return kmalloc_free();
}
#define pgalloc_free_pages snapshot_free_pages
#define kmalloc_total snapshot_kheap_total
#define kmalloc_used snapshot_kheap_used
#define kmalloc_free snapshot_kheap_free
#include "mem_stat_source.c"
#undef pgalloc_free_pages
#undef kmalloc_total
#undef kmalloc_used
#undef kmalloc_free

/* Model a shell-launched USER's syscall on the caller's TSS.ESP0 stack. */
static void __attribute__((noinline)) shell_syscall(void) {
    volatile u8 bytes[12288];
    MemStat stat;
    for (u32 i = 0; i < sizeof(bytes); i++) bytes[i] = 0;
    CHECK("shell syscall snapshot", kapi_mem_stat(-1, &stat, sizeof(stat)) == sizeof(stat));
    CHECK("shell syscall grows", (stat.kstack_high_water >> 16) > (expected_stack_water >> 16));
    CHECK("shell syscall fixed unchanged", (stat.kstack_high_water & 0xffffU) == (expected_stack_water & 0xffffU));
    expected_stack_water = stat.kstack_high_water;
}

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
        if (!target) CHECK("system AS zero", all_zero(output.bytes + MEMSTAT_MIN,
              __builtin_offsetof(MemStat, pressure_epoch) - MEMSTAT_MIN) && !output.stat.trim_epoch);
        CHECK("diagnostic system", output.stat.kstack_high_water == expected_stack_water &&
            output.stat.kheap_peak == kmalloc_peak_bytes && output.stat.resident_heap_peak == resident_heap_peak &&
            output.stat.resident_heap_fail == resident_heap_fail && output.stat.leftover_pages == exec_as_leftover_pages &&
            output.stat.ledger_irq_ops == ledger_irq_ops && output.stat.ledger_exc_ops == ledger_exc_ops &&
            output.stat.ledger_bad_free == ledger_bad_free && output.stat.fault_kill_count == 11 &&
            output.stat.reclaim_count == 12 && output.stat.stop_park_count == 13 && output.stat.audit_runs == 45 &&
            output.stat.audit_fail == 54 && output.stat.kselftest_pass == 295 && !output.stat.kselftest_fail);
        CHECK("active leases", output.stat.lease_active == 2);
        CHECK("owner pages", output.stat.owner_pages == (target ? ledger_owner_pages(g_slot[target].as->owner) : 0));
        CHECK("pressure epoch", output.stat.pressure_epoch == appslot_trim_epoch);
        CHECK("pending mask", output.stat.trim_pending_mask == ((1UL << 2) | (1UL << 3)));
    }
}

static void matrix(void)
{
    /* IDs: 2 self, 3 live, 4 FREE (with stale AS), 5 live without AS. */
    const int ids[] = {0, -1, 2, 3, 4, 5, 1, 99, -2};
    const int trusted[] = {sizeof(MemStat), sizeof(MemStat), sizeof(MemStat), sizeof(MemStat), OS32_ERR_NOTFOUND, OS32_ERR_NOTFOUND,
                           OS32_ERR_INVAL, OS32_ERR_INVAL, OS32_ERR_INVAL};
    const int user[] = {sizeof(MemStat), sizeof(MemStat), sizeof(MemStat), OS32_ERR_INVAL, OS32_ERR_INVAL, OS32_ERR_INVAL,
                        OS32_ERR_INVAL, OS32_ERR_INVAL, OS32_ERR_INVAL};
    a.leases[3].token = 99; b.leases[4].token = 98;
    g_slot[1].state = APP_STATE_RUNNING;
    g_slot[4] = g_slot[2]; g_slot[4].state = APP_STATE_FREE;
    g_slot[4].trim_pending = 0;
    g_slot[5].state = APP_STATE_RUNNING;
    for (int mode = 0; mode < 8; mode++) {
        ring3_in_syscall = 1; heap_select(2);
        struct caller_identity identity = caller_identity_get();
        CHECK("identity captured", identity.app_id == 2 && identity.owner == a.owner && identity.generation == a.generation);
        u32 owner = a.owner, generation = a.generation;
        if (mode == 0) ring3_wm_enter();
        if (mode == 1) { ring3_in_syscall = 0; current_slot = APP_ID_SHELL; caller_access_invalidate(); }
        if (mode == 6) {
            ring3_in_syscall = 0; caller_access_invalidate(); g_slot[2].cpl3 = 0;
        }
        if (mode == 7) {
            ring3_in_syscall = 0; current_slot = APP_ID_SHELL;
            caller_access_invalidate(); g_slot[1].cpl3 = 1;
        }
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
        g_slot[1].cpl3 = 0; g_slot[2].cpl3 = 1;
        a.owner = owner; a.generation = generation; g_slot[2].as = &a;
    }
    ring3_in_syscall = 1;
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
    a.leases[3].token = b.leases[4].token = 0;
}

static void sizes(void)
{
    const u32 sizes[] = {0, MEMSTAT_MIN - 1, MEMSTAT_MIN, 73, 120, 132, 133, 199, sizeof(MemStat), sizeof(output)};
    MemStat full;
    CHECK("full size", kapi_mem_stat(-1, &full, sizeof(full)) == sizeof(full));
    CHECK("NULL direct", kapi_mem_stat(-1, 0, sizeof(full)) == OS32_ERR_INVAL);
    for (u32 j = 0; j < sizeof(sizes) / sizeof(sizes[0]); j++) {
        u32 n = sizes[j], copied = n < MEMSTAT_MIN ? 0 : n < sizeof(full) ? n : sizeof(full);
        for (u32 i = 0; i < sizeof(output); i++) output.bytes[i] = 0xa5;
        u32 scans = stack_scans;
        int rc = kapi_mem_stat(-1, &output, n);
        CHECK("prefix avoids stack scan", stack_scans == scans + (n > 132));
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
    CHECK("trim flag", !!(stat.flags & MEMSTAT_TRIM_PENDING) == !!g_slot[2].trim_pending);
    CHECK("app trim epoch", stat.trim_epoch == g_slot[2].trim_epoch);
    CHECK("pressure epoch", stat.pressure_epoch == appslot_trim_epoch);
    CHECK("pending mask", stat.trim_pending_mask == ((1UL << 2) | (1UL << 3)));
    CHECK("current end", stat.exec_heap_cur_end == a.appmem_layout.exec_heap_cur_end);
    CHECK("layout", stat.img_end == a.appmem_layout.img_end && stat.primary_mapped_end == a.appmem_layout.primary_mapped_end && stat.guard_b == a.appmem_layout.guard_b);
    CHECK("slot values", stat.load_addr == g_slot[2].load_addr && stat.sbrk_heap_limit == g_slot[2].sbrk_heap_limit && stat.exec_heap_base == g_slot[2].exec_heap_base && stat.exec_heap_size == g_slot[2].exec_heap_size && stat.stack_top == g_slot[2].stack_top && stat.stack_size == g_slot[2].stack_size);
    CHECK("system values", stat.resident_heap_total == exec_heap_total() && stat.resident_heap_used == exec_heap_used() && stat.phys_total_pages == pgalloc_total_pages() && stat.phys_free_pages == pgalloc_free_pages() && stat.kheap_total == kmalloc_total() && stat.kheap_used == kmalloc_used() && stat.kheap_free == kmalloc_free());
}


static void owner_diagnostics(void)
{
    struct addrspace diag = {0};
    AppSlot saved = g_slot[5];
    diag.owner = 23; diag.generation = 42;
    g_slot[5].as = &diag;
    owner_serial_len = 0;
    exec_owner_diag(&g_slot[5], 0, 0, 0);
    exec_owner_diag(&g_slot[5], 1, 100, 7);
    const char expected[] = "OS32: owner-start id=5 owner=23 gen=42\r\n"
        "OS32: owner-exit id=5 owner=23 gen=42 pages=100 leftover=7 irq=0 exc=0\r\n";
    CHECK("owner wire", owner_serial_len == sizeof(expected)-1 && equal(owner_serial, expected, sizeof(expected)));
    owner_serial_len = 0;
    kctx_irq_depth = 2;
    exec_owner_diag(&g_slot[5], 0, 0, 0); exec_owner_diag(&g_slot[5], 1, 100, 7);
    kctx_irq_depth = 0; kctx_exc_depth = 3;
    exec_owner_diag(&g_slot[5], 0, 0, 0); exec_owner_diag(&g_slot[5], 1, 100, 7);
    kctx_exc_depth = 0;
    CHECK("owner context gate", owner_serial_len == 0);
    g_slot[5] = saved;
}

void _start(void)
{
    owner_diagnostics();
    heap_ram();
    stack_diagnostics();
    host_map_fixed_paging(); paging_init(17408); host_pool_boot(17408);
    roots[0] = paging_kernel_pd_phys();
    integration = 1; heap_alias_on = 1;
    heap_mmap(MEM_SHELL_HEAP_BASE, MEM_SHELL_HEAP_SIZE, 0);
    exec_heap_init_at(MEM_SHELL_HEAP_BASE, MEM_SHELL_HEAP_SIZE);
    CHECK("resident fixture", exec_heap_alloc(40));
    CHECK("resident peak", resident_heap_peak >= exec_heap_used());
    u32 fails = resident_heap_fail, peak = resident_heap_peak;
    CHECK("resident failure", !exec_heap_alloc(MEM_SHELL_HEAP_SIZE) && resident_heap_fail == fails + 1);
    exec_heap_reset();
    CHECK("resident peak retained", resident_heap_peak == peak);
    CHECK("resident fixture", exec_heap_alloc(40));
    ring3_in_syscall = 1;
    heap_create(&b, 3); roots[2] = b.pd_phys;
    heap_create(&a, 2); roots[1] = a.pd_phys;
    g_slot[2].exec_heap_used = 12345; /* saved resident usage != AS usage */
    g_slot[2].load_addr = MEM_EXEC_LOAD_ADDR;
    g_slot[2].sbrk_heap_limit = a.appmem_layout.primary_mapped_end;
    g_slot[2].stack_top = MEM_APP_STACK_TOP; g_slot[2].stack_size = MEM_EXEC_STACK_SIZE;
    appslot_trim_epoch = 37;
    g_slot[2].trim_pending = g_slot[3].trim_pending = 1;
    g_slot[2].trim_epoch = 31; g_slot[3].trim_epoch = 36;
    on_stack(shell_syscall, MEM_SHELL_STACK_TOP);
    /* Include the copyout/return path after the syscall snapshot was taken. */
    expected_stack_water = actual_kstack_high_water();
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
