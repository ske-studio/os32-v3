/* f12: real launch allocation block, paging, ledger, appmem and teardown.
 * File I/O/entry/parent heap are boundary fixtures; no guest execution here. */
#include "budget_fixture.c"
#include "exec.h"
static AppSlot slots[APP_SLOT_COUNT];
static u32 parent_heap = 73, saved_heap, restores, admits;
static u32 extra_pages, need_seen;
static int fail_kheap;
static u32 g_ring3_band_top = MEM_APP_STACK_TOP, g_ring3_band_pdes = MEM_APP_BAND_MAX_PDES;
AppSlot *appslot_at(int id) { return &slots[id]; }
static int g_cur = APP_ID_SHELL;
int appslot_alloc_id(void) { return 2; }
#include "budget_admit.inc"
static int budget_admit(int gui, u32 pages, u32 free_pages) {
    admits++; need_seen = pages;
    return appslot_start_admit(gui, pages, free_pages);
}
static void shell_print(const char *s, u8 attr) { (void)s; (void)attr; }
static void shell_print_dec(u32 n, u8 attr) { (void)n; (void)attr; }
static void exec_restore_band(int id) { CHECK(id == 1); restores++; }
static void exec_restore_context(int id) {
    exec_restore_band(id); parent_heap = saved_heap;
    paging_load_cr3(paging_kernel_pd_phys());
}
static void exec_heap_save_state(u32 *out) { *out = saved_heap = parent_heap; }
static int exec_disk_write_path_allowed(const char *p) { (void)p; return 0; }
static int exec_launch_abort(int launcher, int id, int rc) {
    exec_teardown_app(&slots[id]); exec_restore_context(launcher); return rc;
}
static void *budget_kmalloc(u32 bytes) { return fail_kheap ? 0 : kmalloc(bytes); }
static u32 shlib_data_pages(void) { return extra_pages; }
static u32 g_loaded, g_text_pages, g_data_pages, g_data_vaddr;
static u32 g_pages[5];
#include "budget_shlib.inc"
static KernelAPI api, *kapi = &api;
#include "budget_launch.inc"

static u32 digest(const void *p, u32 n) {
    const u8 *b = p; u32 h = 0;
    while (n--) h = h * 33 + *b++;
    return h;
}
static void unchanged_failure(u32 heap_bytes, int exhaust) {
    u32 free = pgalloc_free_pages(), before_admits = admits;
    u32 pt = digest(page_directory, sizeof(page_directory));
    u32 ext = digest(&slots[1], sizeof(slots[1]));
    u32 owners = digest(ledger_owners, sizeof(ledger_owners));
    u32 restore = restores;
    fail_kheap = exhaust;
    CHECK(budget_launch(0, PAGE_SIZE + 17, heap_bytes, 0) == EXEC_ERR_NOMEM);
    fail_kheap = 0;
    CHECK(pgalloc_free_pages() == free && parent_heap == 73);
    CHECK(pt == digest(page_directory, sizeof(page_directory)));
    CHECK(ext == digest(&slots[1], sizeof(slots[1])));
    if (exhaust) {
        CHECK(restores == restore + 1 && !slots[2].as);
        for (u32 i = 0; i < sizeof(ledger_owners) / sizeof(ledger_owners[0]); i++)
            CHECK(ledger_owners[i].kind != LEDGER_KIND_AS);
    } else {
        CHECK(owners == digest(ledger_owners, sizeof(ledger_owners)));
        if (heap_bytes >= MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE - PAGE_SIZE - MEM_EXEC_HEAP_BASE + 1)
            CHECK(admits == before_admits); /* VA rejection precedes count/admit. */
    }
}
static void launch_case(u32 image, u32 requested, u32 stack, u32 extra, u32 expected_heap) {
    u32 free = pgalloc_free_pages();
    extra_pages = extra;
    g_loaded = extra != 0; g_text_pages = g_loaded; g_data_pages = extra;
    g_data_vaddr = MEM_SHLIB_BASE + PAGE_SIZE;
    for (u32 i = 0; i < 5; i++) g_pages[i] = MEM_PHYS_EXEC_FLOOR;
    CHECK(budget_launch(0, image, requested, stack) == 2);
    AppSlot *a = &slots[2];
    CHECK(a->exec_heap_size == expected_heap);
    CHECK(a->sbrk_heap_limit == PAGE_ALIGN_UP(MEM_EXEC_LOAD_ADDR + image) + PAGE_SIZE);
    CHECK(free - pgalloc_free_pages() == a->pages && a->pages == need_seen);
    CHECK(a->as->appmem.e[0].kind == APPMEM_LIBC_INITIAL &&
          a->as->appmem.e[0].end - a->as->appmem.e[0].base == PAGE_SIZE);
    CHECK(a->as->appmem.e[1].kind == APPMEM_EXEC_INITIAL &&
          a->as->appmem.e[1].end - a->as->appmem.e[1].base == expected_heap);
    CHECK(a->stack_size == (stack ? stack : MEM_EXEC_STACK_SIZE));
    exec_teardown_app(a);
    CHECK(pgalloc_free_pages() == free);
    CHECK(!kmalloc_used());
    CHECK(!exec_as_leftover_pages && !ledger_bad_free && parent_heap == 73);
}
void _start(void) {
    u32 args[6] = {0x400000, 0xC00000, 3, 0x32, 0xFFFFFFFF, 0}, result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x400000);
    host_map_fixed_paging(); paging_init(16384); host_pool_boot(16384);
    kmalloc_init(heap, sizeof(heap));
    launch_case(PAGE_SIZE + 17, 0, 0, 0, MEM_EXEC_HEAP_MIN);
    launch_case(PAGE_SIZE, 1, 0, 0, MEM_EXEC_HEAP_MIN);
    launch_case(PAGE_SIZE, 65537, 0, 0, 69632);
    launch_case(PAGE_SIZE, 2 * 1024 * 1024, 0, 0, 2 * 1024 * 1024);
    launch_case(PAGE_SIZE, 0, 1024 * 1024, 0, MEM_EXEC_HEAP_MIN);
    launch_case(PAGE_SIZE, 0, 0, 4, MEM_EXEC_HEAP_MIN);
    launch_case(3 * 1024 * 1024 + 1, 0, 0, 4, MEM_EXEC_HEAP_MIN);
    /* > old 0xC00000 physical ceiling's available bytes, with real free. */
    launch_case(PAGE_SIZE, 7 * 1024 * 1024, 0, 0, 7 * 1024 * 1024);
    unchanged_failure(0xfffff000U, 0);
    unchanged_failure(MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE - PAGE_SIZE - MEM_EXEC_HEAP_BASE + 1, 0);
    unchanged_failure(12 * 1024 * 1024, 0);
    unchanged_failure(0, 1);
    const u32 resident[] = {0, 65536, 1024 * 1024};
    for (u32 i = 0; i < 3; i++) {
        CHECK(budget_launch(1, PAGE_SIZE, resident[i], 0) == 1);
        CHECK(slots[1].sbrk_heap_limit == MEM_SHELL_GUARD && api.sbrk_heap_limit == MEM_SHELL_GUARD);
        CHECK(slots[1].exec_heap_size == (resident[i] && resident[i] < MEM_SHELL_HEAP_SIZE ? resident[i] : MEM_SHELL_HEAP_SIZE));
    }
    SAY("PASS resident_heap_0_64K_1M");
    /* K7: cap available pool to precisely 768 pages; gui_demo header is built. */
    u32 free = pgalloc_free_pages(), reserve = free - 768, held;
    CHECK(pgalloc_alloc_n_owner(LEDGER_OWNER_KERNEL, reserve, MEM_POOL_BASE / PAGE_SIZE,
                                PHYSMEM_LEGACY_MAX_PFN, LEDGER_BOTTOM_UP, &held));
    CHECK(pgalloc_free_pages() == 768);
    launch_case(GUI_IMAGE_BYTES, 0, 0, 4, MEM_EXEC_HEAP_MIN);
    CHECK(need_seen == GUI_NEED_PAGES && need_seen <= 768);
    CHECK(pgalloc_free_n_owner(LEDGER_OWNER_KERNEL, held, reserve));
    /* Each partial image/heap/stack allocation failure goes through actual abort. */
    extra_pages = 0; g_loaded = 0;
    for (u32 i = 1; i <= 2 + 16 + 64; i++) {
        app_fail_at = i; app_alloc_calls = 0;
        CHECK(budget_launch(0, PAGE_SIZE, 0, 0) == EXEC_ERR_NOMEM);
        app_fail_at = 0;
        CHECK(pgalloc_free_pages() == free && !kmalloc_used() && !slots[2].as);
        CHECK(!exec_as_leftover_pages && !ledger_bad_free && parent_heap == 73);
    }
    SAY("PASS f12 layout, PDE consumption, K7 768 pages, rollback, VA overflow, KHEAP, resident");
    die(0);
}
