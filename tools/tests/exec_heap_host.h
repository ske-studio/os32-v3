/* f9: real allocator, caller frame, PTE/owner walk and appmem transactions.
 * Linux shared mappings implement the MMU aliases, including equal parent and
 * child VAs backed by different owner pages. */
#include "exec_heap.h"
#define kfree heap_kernel_free
#include "kernel/kmalloc.c"
#undef kfree
#include "exec/access_walk.c"
int shlib_read_page(u32 va, u32 pa) { (void)va; (void)pa; return 0; }
u32 exec_tramp_page_addr(void) { return 0; }
volatile int ring3_wm_depth;
int ring3_call_from_user(void) { return ring3_in_syscall && !ring3_wm_depth; }
#include "exec/redir_access.c"
static u32 heap_page_walks;
static int heap_access_page(const struct addrspace *as, u32 va, int write, u32 *pa) {
    heap_page_walks++;
    return as_access_page(as, va, write, pa);
}
static u32 heap_map_calls, heap_trim_calls;
static int heap_trim(struct addrspace *as, u32 base, u32 bytes) {
    heap_trim_calls++;
    return appmem_exec_trim(as, base, bytes);
}
#define appmem_exec_trim heap_trim
static int heap_map(struct addrspace *as, struct appmem_table *table,
                    const struct appmem_layout *layout, u32 bytes, u32 hint,
                    u32 flags, u32 kind, u32 ef, u32 *out) {
    heap_map_calls++;
    return appmem_map(as, table, layout, bytes, hint, flags, kind, ef, out);
}
#define appmem_map heap_map
#define as_access_page heap_access_page
#include "heap_source.c"
#undef as_access_page
#undef appmem_map
#undef appmem_exec_trim
#define KAPI_HIT(slot) ((void)(slot))
#include "heap_wrap_source.c"
#undef KAPI_HIT

static struct { u32 sbrk_heap_limit; } heap_kapi, *kapi = &heap_kapi;
static u32 ring3_tramp_page[KAPI_FUNC_COUNT + 2];
volatile int exec_nest_level;
AppSlot *appslot_at(int id) { return &g_slot[id]; }
static void ring3_band_set(int n) { (void)n; }
#include "heap_context_source.c"

static int heap_fd, heap_alias_on;
static void heap_mmap(u32 va, u32 bytes, u32 offset) {
    u32 args[6] = {va, bytes, 3, 0x11, (u32)heap_fd, offset}, result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK("MMU alias", result == va);
}
static void heap_ram(void) {
    const char name[] = "f9-physical";
    int rc;
    __asm__ volatile("int $0x80" : "=a"(heap_fd) : "a"(356), "b"(name), "c"(0) : "memory");
    CHECK("RAM memfd", heap_fd >= 0);
    __asm__ volatile("int $0x80" : "=a"(rc) : "a"(93), "b"(heap_fd), "c"(MEM_POOL_BASE + 0x4000000) : "memory");
    CHECK("RAM file size", !rc);
    heap_mmap(MEM_POOL_BASE, 0x4000000, MEM_POOL_BASE);
}
static void heap_alias(u32 root) {
    if (!heap_alias_on) return;
    const u32 *pd = P2V(root);
    for (u32 i = APP_BAND_PDE; i < APP_BAND_PDE + MEM_APP_BAND_MAX_PDES; i++) {
        if (!(pd[i] & PTE_PRESENT)) continue;
        const u32 *pt = P2V(pd[i] & ~(PAGE_SIZE - 1U));
        for (u32 j = 0; j < PTE_COUNT; j++) if (pt[j] & PTE_PRESENT)
            heap_mmap((i << 22) + j * PAGE_SIZE, PAGE_SIZE, pt[j] & ~(PAGE_SIZE - 1U));
    }
}
static void heap_select(int id) {
    CallerAccessFrame prev;
    current_slot = id;
    paging_load_cr3(g_slot[id].as->pd_phys);
    CHECK("heap USER capture", caller_access_enter(&prev, CALLER_USER));
}
static void heap_create_size(struct addrspace *as, int id, u32 bytes) {
    u32 owner;
    paging_load_cr3(paging_kernel_pd_phys());
    CHECK("heap owner", ledger_owner_new(LEDGER_KIND_AS, 0, "heap", &owner));
    CHECK("heap AS", !paging_addrspace_create_lease(as, owner));
    appmem_init(as, MEM_EXEC_LOAD_ADDR + 1, MEM_EXEC_LOAD_ADDR + PAGE_SIZE,
                MEM_EXEC_HEAP_BASE + bytes,
                MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE - MEM_GUARD_SIZE);
    for (u32 va = MEM_EXEC_HEAP_BASE; va < MEM_EXEC_HEAP_BASE + bytes; va += PAGE_SIZE) {
        u32 phys = pgalloc_alloc_phys(owner, 1);
        CHECK("heap initial pages", phys && !paging_addrspace_map_user(as, va, phys, PAGE_RW | PTE_USER));
    }
    g_slot[id] = (AppSlot){0};
    g_slot[id].as = as; g_slot[id].cpl3 = 1; g_slot[id].state = APP_STATE_RUNNING;
    g_slot[id].exec_heap_base = MEM_EXEC_HEAP_BASE;
    g_slot[id].exec_heap_size = bytes;
    heap_select(id);
    heap_init_context(&g_slot[id], 1, MEM_EXEC_HEAP_BASE, bytes);
    CHECK("CPL3 init keeps resident", exec_heap.base == (u8 *)MEM_SHELL_HEAP_BASE && exec_heap.size == MEM_SHELL_HEAP_SIZE);
}
static void heap_create(struct addrspace *as, int id) {
    heap_create_size(as, id, MEM_EXEC_HEAP_MIN);
}
static void heap_copy(void *to, const void *from, u32 len) {
    u8 *d = to; const u8 *s = from; while (len--) *d++ = *s++;
}
static u8 heap_resident_image[MEM_SHELL_HEAP_SIZE], heap_other_image[4 * MEM_EXEC_HEAP_MIN];
static u8 heap_arena_image[4 * MEM_EXEC_HEAP_MIN];
static KHeap heap_resident_state;
static struct addrspace heap_other_state;
static u32 heap_owner_pages_a, heap_owner_pages_b, heap_owner_pages_kernel;
static void heap_other_snapshot(int compare) {
    u32 pos = 0;
    if (compare) CHECK("own PD unchanged", equal(pd_images[1], P2V(a.pd_phys), PAGE_SIZE));
    else heap_copy(pd_images[1], P2V(a.pd_phys), PAGE_SIZE);
    for (u32 i = 0; i < MEM_APP_BAND_MAX_PDES; i++) if (a.app_pt_phys[i]) {
        if (compare) CHECK("own PTE unchanged", equal(pt_images[i], P2V(a.app_pt_phys[i]), PAGE_SIZE));
        else heap_copy(pt_images[i], P2V(a.app_pt_phys[i]), PAGE_SIZE);
    }
    if (!compare) {
        heap_resident_state = exec_heap;
        heap_other_state = b;
        heap_copy(heap_resident_image, exec_heap.base, exec_heap.size);
        heap_owner_pages_a = ledger_owner_pages(a.owner);
        heap_owner_pages_b = ledger_owner_pages(b.owner);
        heap_owner_pages_kernel = ledger_owner_pages(LEDGER_OWNER_KERNEL);
    } else {
        CHECK("resident unchanged", equal(&exec_heap, &heap_resident_state, sizeof(exec_heap)) &&
              equal(heap_resident_image, exec_heap.base, exec_heap.size));
        CHECK("other AS unchanged", equal(&b, &heap_other_state, sizeof(b)));
        CHECK("owner ledger unchanged", heap_owner_pages_a == ledger_owner_pages(a.owner) &&
              heap_owner_pages_b == ledger_owner_pages(b.owner) &&
              heap_owner_pages_kernel == ledger_owner_pages(LEDGER_OWNER_KERNEL));
    }
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++) if (user_arena(&b.appmem.e[i]))
    for (u32 va = b.appmem.e[i].base; va < b.appmem.e[i].end; va += PAGE_SIZE) {
        u32 pa;
        CHECK("other snapshot bounds", pos + PAGE_SIZE <= sizeof(heap_other_image));
        CHECK("other page", !as_va_to_pa(b.pd_phys, va, &pa));
        if (compare) CHECK("other arena unchanged", equal(heap_other_image + pos, P2V(pa), PAGE_SIZE));
        else heap_copy(heap_other_image + pos, P2V(pa), PAGE_SIZE);
        pos += PAGE_SIZE;
    }
    other_image(b.pd_phys, 1, compare);
}
static void heap_reject(void *p, int disabled) {
    heap_copy(heap_arena_image, (void *)MEM_EXEC_HEAP_BASE, MEM_EXEC_HEAP_MIN);
    heap_other_snapshot(0);
    wrap_mem_free(p);
    CHECK("corrupt free immutable", equal(heap_arena_image, (void *)MEM_EXEC_HEAP_BASE, MEM_EXEC_HEAP_MIN));
    if (disabled) CHECK("corrupt alloc NULL", !wrap_mem_alloc(16));
    heap_other_snapshot(1);
}
static void heap_f10_cases(void);
static void heap_cases(void) {
    integration = 1; watching = unmapping = 0; heap_alias_on = 1;
    CHECK("free address bypasses early guard", !kapi_argptr[KAPI_SLOT_MEM_FREE]);
    /* Resident region is separate from the physical page pool. */
    heap_mmap(MEM_SHELL_HEAP_BASE, MEM_SHELL_HEAP_SIZE, 0);
    exec_heap_init_at(MEM_SHELL_HEAP_BASE, MEM_SHELL_HEAP_SIZE);
    caller_access_invalidate();
    void *resident = wrap_mem_alloc(40);
    CHECK("no frame resident", resident && (u32)resident < MEM_SHELL_HEAP_END);
    heap_create(&b, 3);
    void *child = wrap_mem_alloc(128);
    CHECK("child alloc", child);
    CHECK("other AS ARENA", wrap_mem_alloc(MEM_EXEC_HEAP_MIN - BLK_HDR_SIZE));
    for (u32 i = 0; i < 128; i++) ((u8 *)child)[i] = (u8)(i ^ 0x5a);
    heap_create(&a, 2);
    void *parent = wrap_mem_alloc(128);
    CHECK("USER own arena", (u32)parent == MEM_EXEC_HEAP_BASE + BLK_HDR_SIZE);
    for (u32 i = 0; i < 128; i++) ((u8 *)parent)[i] = (u8)(i ^ 0xa5);
    u32 saved;
    exec_heap_save_state(&saved);
    heap_select(3);
    CHECK("nested same VA", parent == child);
    for (u32 i = 0; i < 128; i++) CHECK("child pattern", ((u8 *)child)[i] == (u8)(i ^ 0x5a));
    heap_select(2);
    exec_heap_restore_state(MEM_SHELL_HEAP_BASE, MEM_SHELL_HEAP_SIZE, saved);
    for (u32 i = 0; i < 128; i++) CHECK("parent pattern", ((u8 *)parent)[i] == (u8)(i ^ 0xa5));
    for (u32 path = 0; path < 9; path++) {
        /* All nine restore call sites (including park_poll) share this real helper. */
        heap_restore_context(3); heap_restore_context(2);
        CHECK("CPL3 restore keeps resident", exec_heap.base == (u8 *)MEM_SHELL_HEAP_BASE && exec_heap.size == MEM_SHELL_HEAP_SIZE);
    }
    CHECK("resident remains shell", exec_heap.base == (u8 *)MEM_SHELL_HEAP_BASE && exec_heap.size == MEM_SHELL_HEAP_SIZE);
    /* USER frame + app root, but WM has explicit trusted origin. */
    ring3_wm_depth = 1;
    void *wm = wrap_mem_alloc(48);
    CHECK("WM resident", (u32)wm >= MEM_SHELL_HEAP_BASE && (u32)wm < MEM_SHELL_HEAP_END);
    KHeap res = exec_heap;
    wrap_mem_free(parent);
    CHECK("WM cross free", equal(&res, &exec_heap, sizeof(res)));
    wrap_mem_free(wm); ring3_wm_depth = 0;
    CallerAccessFrame prev;
    CHECK("trusted capture", caller_access_enter(&prev, CALLER_TRUSTED));
    wm = wrap_mem_alloc(48);
    CHECK("TRUSTED resident", (u32)wm >= MEM_SHELL_HEAP_BASE && (u32)wm < MEM_SHELL_HEAP_END);
    wrap_mem_free(wm); caller_access_leave(&prev);
    /* exec_finish: child frame survives while root/slot now identify parent. */
    current_slot = 3; paging_load_cr3(b.pd_phys);
    wm = wrap_mem_alloc(48);
    CHECK("finish window resident", (u32)wm >= MEM_SHELL_HEAP_BASE && (u32)wm < MEM_SHELL_HEAP_END);
    res = exec_heap; wrap_mem_free((void *)MEM_EXEC_HEAP_BASE);
    CHECK("finish window cross free", equal(&res, &exec_heap, sizeof(res)));
    wrap_mem_free(wm); heap_select(2);
    struct addrspace cross_before = a;
    heap_reject(resident, 0);
    CHECK("cross AS state unchanged", equal(&a, &cross_before, sizeof(a)));
    /* R1 must reject before either allocator or USER validation mutates. */
    for (u32 mode = 0; mode < 2; mode++) {
        u32 used = a.exec_heap_used;
        heap_other_snapshot(0);
        if (mode) kctx_exc_depth = 1; else kctx_irq_depth = 1;
        CHECK("R1 USER alloc", !wrap_mem_alloc(16)); wrap_mem_free(parent);
        ring3_wm_depth = 1;
        CHECK("R1 resident alloc", !wrap_mem_alloc(16)); wrap_mem_free(resident);
        ring3_wm_depth = 0; kctx_exc_depth = kctx_irq_depth = 0;
        CHECK("R1 USER unchanged", a.exec_heap_used == used && ((BlkHdr *)parent)[-1].magic == BLK_MAGIC_USED);
        heap_other_snapshot(1);
    }
    wrap_mem_free(parent);
    CHECK("parent reuse", wrap_mem_alloc(128) == parent);
    for (u32 test = 0; test < 9; test++) {
        exec_heap_user_init(&a);
        parent = wrap_mem_alloc(128);
        BlkHdr *hdr = (BlkHdr *)parent - 1;
        void *bad = parent;
        if (test == 0) hdr->size = 64;
        if (test == 1) hdr->size = 0xfffffff8;
        if (test == 2) hdr->size = 0x7ffffff8;
        if (test == 3) hdr->magic = 0;
        if (test == 4) { ((BlkHdr *)parent)[2] = (BlkHdr){16, BLK_MAGIC_USED}; bad = (BlkHdr *)parent + 3; }
        if (test == 5) hdr->size = 2 * MEM_EXEC_HEAP_MIN;
        if (test == 6) { /* A valid early block cannot hide a corrupt tail. */
            BlkHdr *tail = (BlkHdr *)((u8 *)parent + 128); tail->size -= BLK_ALIGN;
        }
        if (test == 7) hdr->size = 7;
        if (test == 8) wrap_mem_free(parent); /* Double free disables this AS. */
        heap_reject(bad, 1);
    }
    exec_heap_user_init(&a); parent = wrap_mem_alloc(128);
    /* Bad mapping/owner must fail before a mapped header can be trusted. */
    u32 *entry = &((u32 *)P2V(a.app_pt_phys[(MEM_EXEC_HEAP_BASE - MEM_APP_BAND_BASE) >> 22]))[(MEM_EXEC_HEAP_BASE >> PAGE_SHIFT) % PTE_COUNT];
    u32 old = *entry, foreign;
    CHECK("foreign PTE", !as_va_to_pa(b.pd_phys, MEM_EXEC_HEAP_BASE, &foreign));
    for (u32 mode = 0; mode < 2; mode++) {
        a.exec_heap_used = 136;
        *entry = mode ? (foreign | PAGE_RW | PTE_USER) : (old & ~PTE_PRESENT);
        heap_reject(parent, 1);
    }
    *entry = old; exec_heap_user_init(&a);
    u32 fake_anon;
    CHECK("ANON forged fixture", !appmem_map(&a, &a.appmem, &a.appmem_layout,
          PAGE_SIZE, 0, 0, APPMEM_ANON, 0, &fake_anon));
    *(BlkHdr *)fake_anon = (BlkHdr){16, BLK_MAGIC_USED};
    u8 anon_before[PAGE_SIZE];
    heap_copy(anon_before, (void *)fake_anon, PAGE_SIZE);
    heap_reject((void *)(fake_anon + BLK_HDR_SIZE), 1);
    CHECK("ANON header unread/unchanged", equal(anon_before, (void *)fake_anon, PAGE_SIZE));
    CHECK("ANON fixture return", !appmem_unmap(&a, &a.appmem, fake_anon, PAGE_SIZE));
    exec_heap_user_init(&a);
    /* Fill INITIAL, then grow EXACT twice: the ARENA entries merge. */
    void *p0 = wrap_mem_alloc(MEM_EXEC_HEAP_MIN - BLK_HDR_SIZE);
    void *p1 = wrap_mem_alloc(MEM_EXEC_HEAP_MIN - BLK_HDR_SIZE);
    void *p2 = wrap_mem_alloc(MEM_EXEC_HEAP_MIN - BLK_HDR_SIZE);
    CHECK("EXACT arena growth", p0 && (u32)p1 == MEM_EXEC_HEAP_BASE + MEM_EXEC_HEAP_MIN + BLK_HDR_SIZE &&
          (u32)p2 == MEM_EXEC_HEAP_BASE + 2 * MEM_EXEC_HEAP_MIN + BLK_HDR_SIZE &&
          a.appmem_layout.exec_heap_cur_end == MEM_EXEC_HEAP_BASE + 3 * MEM_EXEC_HEAP_MIN);
    int arena = -1;
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++) if (a.appmem.e[i].kind == APPMEM_EXEC_ARENA) arena = (int)i;
    CHECK("adjacent ARENA merged", arena >= 0 && a.appmem.e[arena].end - a.appmem.e[arena].base == 2 * MEM_EXEC_HEAP_MIN);
    u32 anon;
    CHECK("EXACT collision fixture", !appmem_map(&a, &a.appmem, &a.appmem_layout, PAGE_SIZE,
          a.appmem_layout.exec_heap_cur_end, APPMEM_MAP_EXACT, APPMEM_ANON, 0, &anon));
    void *p3 = wrap_mem_alloc(128);
    CHECK("TOPDOWN separate arena", (u32)p3 == a.appmem_layout.guard_b - MEM_EXEC_HEAP_MIN + BLK_HDR_SIZE);
    CHECK("aggregate after growth", a.exec_heap_used == 3 * MEM_EXEC_HEAP_MIN + 136);
    heap_page_walks = 0;
    wrap_mem_free(p3);
    CHECK("free walks only own arena", heap_page_walks == MEM_EXEC_HEAP_MIN / PAGE_SIZE);
    CHECK("aggregate free delta", a.exec_heap_used == 3 * MEM_EXEC_HEAP_MIN);
    wrap_mem_free(p0);
    heap_page_walks = 0;
    CHECK("first candidate reuse", wrap_mem_alloc(MEM_EXEC_HEAP_MIN - BLK_HDR_SIZE) == p0);
    CHECK("alloc stops at candidate", heap_page_walks == MEM_EXEC_HEAP_MIN / PAGE_SIZE);
    CHECK("aggregate alloc delta", a.exec_heap_used == 3 * MEM_EXEC_HEAP_MIN);
    p3 = wrap_mem_alloc(128);
    CHECK("aggregate all arenas", a.exec_heap_used == 3 * MEM_EXEC_HEAP_MIN + 136);
    /* A corrupt size that reaches a real, separate arena is still invalid. */
    BlkHdr *broken = (BlkHdr *)p1 - 1;
    BlkHdr intact = *broken;
    u32 used_before = a.exec_heap_used;
    broken->size = (u32)p3 - (u32)p1;
    heap_copy(heap_arena_image, (void *)((u32)p1 - BLK_HDR_SIZE), 2 * MEM_EXEC_HEAP_MIN);
    heap_other_snapshot(0);
    wrap_mem_free(p1);
    CHECK("separate arena crossing unchanged", equal(heap_arena_image, (void *)((u32)p1 - BLK_HDR_SIZE), 2 * MEM_EXEC_HEAP_MIN));
    CHECK("separate arena crossing rejected", !wrap_mem_alloc(16));
    heap_other_snapshot(1);
    *broken = intact; a.exec_heap_used = used_before; /* fixture-only repair */
    heap_other_snapshot(0);
    struct addrspace before = a;
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++) if (user_arena(&a.appmem.e[i])) {
        struct appmem_extent e = a.appmem.e[i];
        CHECK("public EXEC present rejected", appmem_unmap(&a, &a.appmem, e.base, e.end - e.base) == APPMEM_EINVAL);
    }
    CHECK("public metadata unchanged", equal(&a, &before, sizeof(a)));
    struct appmem_extent e = a.appmem.e[arena];
    CHECK("internal partial rejected", appmem_exec_unmap(&a, e.base, PAGE_SIZE) == APPMEM_EINVAL);
    CHECK("internal mixed rejected", appmem_exec_unmap(&a, e.base, e.end - e.base + PAGE_SIZE) == APPMEM_EINVAL);
    CHECK("internal other AS rejected", appmem_exec_unmap(&b, MEM_EXEC_HEAP_BASE + MEM_EXEC_HEAP_MIN, MEM_EXEC_HEAP_MIN) == APPMEM_EINVAL);
    CHECK("internal metadata unchanged", equal(&a, &before, sizeof(a)));
    heap_other_snapshot(1);
    CHECK("ANON removal", !appmem_unmap(&a, &a.appmem, anon, PAGE_SIZE));
    wrap_mem_free(p3);
    CHECK("compacted table arena routing", wrap_mem_alloc(128) == p3);
    wrap_mem_free(p1); wrap_mem_free(p2);
    u32 pages = ledger_owner_pages(a.owner);
    CHECK("internal whole return", !appmem_exec_unmap(&a, e.base, e.end - e.base));
    CHECK("internal pages returned", ledger_owner_pages(a.owner) == pages - 2 * MEM_EXEC_HEAP_MIN / PAGE_SIZE);
    CHECK("internal PTE returned", as_va_to_pa(a.pd_phys, e.base, &foreign));
    /* Remaining owner pages are released by the actual teardown path. */
    host_cr3 = roots[0]; heap_alias_on = 0;
    u32 leftover = exec_as_leftover_pages, owner = a.owner;
    g_slot[2].stack_base = g_slot[2].stack_top = MEM_APP_STACK_TOP;
    exec_teardown_app(&g_slot[2]);
    CHECK("heap teardown no leftover", exec_as_leftover_pages == leftover && !ledger_owner_pages(owner));
    paging_addrspace_free_user_range(&b, MEM_EXEC_HEAP_BASE, MEM_EXEC_HEAP_BASE + 2 * MEM_EXEC_HEAP_MIN);
    paging_addrspace_destroy(&b);
    CHECK("other owner retire", ledger_owner_retire(b.owner));
    heap_alias_on = 1;
    heap_f10_cases();
    caller_access_invalidate(); wrap_mem_free(resident);
    CHECK("resident no leak", !exec_heap.used);
}

/* f10: snapshot includes every PTE, allocator header, layout and owner count. */
static struct addrspace f10_as;
static u32 f10_pages, f10_frees;
static void f10_snapshot(int compare) {
    u32 pos = 0;
    if (!compare) {
        f10_as = a; f10_pages = ledger_owner_pages(a.owner); f10_frees = heap_free_calls;
        heap_copy(pd_images[1], P2V(a.pd_phys), PAGE_SIZE);
    } else {
        CHECK("f10 unchanged AS", equal(&a, &f10_as, sizeof(a)));
        CHECK("f10 unchanged ledger", f10_pages == ledger_owner_pages(a.owner) && f10_frees == heap_free_calls);
        CHECK("f10 unchanged PD", equal(pd_images[1], P2V(a.pd_phys), PAGE_SIZE));
    }
    for (u32 k = 0; k < MEM_APP_BAND_MAX_PDES; k++) if (a.app_pt_phys[k]) {
        if (!compare) heap_copy(pt_images[k], P2V(a.app_pt_phys[k]), PAGE_SIZE);
        else CHECK("f10 unchanged PTE", equal(pt_images[k], P2V(a.app_pt_phys[k]), PAGE_SIZE));
    }
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++) if (user_arena(&a.appmem.e[i])) {
        u32 bytes = a.appmem.e[i].end - a.appmem.e[i].base;
        CHECK("f10 snapshot capacity", pos + bytes <= sizeof(heap_arena_image));
        if (!compare) heap_copy(heap_arena_image + pos, (void *)a.appmem.e[i].base, bytes);
        else CHECK("f10 unchanged headers", equal(heap_arena_image + pos, (void *)a.appmem.e[i].base, bytes));
        pos += bytes;
    }
}
static void f10_done(void) {
    u32 owner = a.owner, left = exec_as_leftover_pages;
    host_cr3 = roots[0]; heap_alias_on = 0;
    g_slot[2].stack_base = g_slot[2].stack_top = MEM_APP_STACK_TOP;
    exec_teardown_app(&g_slot[2]);
    CHECK("f10 teardown LARGE no leftover", exec_as_leftover_pages == left && !ledger_owner_pages(owner));
    heap_alias_on = 1;
}
static struct appmem_extent *f10_extent(u32 va) {
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++)
        if (a.appmem.e[i].base <= va && va < a.appmem.e[i].end) return &a.appmem.e[i];
    return 0;
}
static void f10_pattern(void *ptr, u32 bytes, int write) {
    for (u32 j = 0; j < bytes; j++) {
        if (write) ((u8 *)ptr)[j] = (u8)(j ^ 0x59);
        else CHECK("f10 live all bytes", ((u8 *)ptr)[j] == (u8)(j ^ 0x59));
    }
}
static void heap_f10_cases(void) {
    heap_create(&a, 2);
    /* Adjacent extents cannot merge; forged USER headers cannot identify LARGE. */
    for (u32 forged = 0; forged < 2; forged++) {
        u8 *hi = wrap_mem_alloc(65536), *lo = wrap_mem_alloc(65536);
        CHECK("f10 adjacent LARGE", hi && lo && lo + 65536 == hi &&
              f10_extent((u32)hi) != f10_extent((u32)lo) &&
              f10_extent((u32)hi)->kind == 5 && f10_extent((u32)lo)->kind == 5);
        f10_pattern(hi, 65536, 1); f10_pattern(lo, 65536, 1);
        *(BlkHdr *)hi = (BlkHdr){16, forged ? BLK_MAGIC_USED : 0};
        ((BlkHdr *)hi)[-1] = (BlkHdr){16, forged ? BLK_MAGIC_USED : 0};
        u32 pa, pages = ledger_owner_pages(a.owner);
        wrap_mem_free(hi);
        CHECK("f10 LARGE ignores USER header", !f10_extent((u32)hi) && as_va_to_pa(a.pd_phys, (u32)hi, &pa) &&
              a.exec_heap_used == 65536 && ledger_owner_pages(a.owner) == pages - 16);
        /* Restore only the fake header in the still-live neighbor. */
        f10_pattern(lo + 65536 - BLK_HDR_SIZE, BLK_HDR_SIZE, 1);
        /* Pattern index restarts on a multiple of 256 except the final header. */
        for (u32 j = 65536 - BLK_HDR_SIZE; j < 65536; j++) lo[j] = (u8)(j ^ 0x59);
        f10_pattern(lo, 65536, 0);
        wrap_mem_free(lo);
        CHECK("f10 adjacent free used", !a.exec_heap_used);
    }
    void *small = wrap_mem_alloc(65535);
    CHECK("unaligned small growth", small && (u32)small % PAGE_SIZE == BLK_HDR_SIZE &&
          f10_extent((u32)small)->kind == APPMEM_EXEC_ARENA);
    CHECK("f10 small used", a.exec_heap_used == 65536 + BLK_HDR_SIZE);
    f10_pattern(small, 65535, 1); f10_pattern(small, 65535, 0);
    wrap_mem_free(small);
    CHECK("f10 small free used", !a.exec_heap_used);
    /* Rust raw = Layout.size + max(align,8)-1 + 16, including page alignment. */
    const u32 sizes[] = {65536, 65537, 131072, 65513 + 8 - 1 + 16, 61441 + 4096 - 1 + 16};
    for (u32 n = 0; n < sizeof(sizes)/sizeof(sizes[0]); n++) {
        u32 bytes = sizes[n], pages = ledger_owner_pages(a.owner), pa;
        void *p = wrap_mem_alloc(bytes);
        CHECK("f10 LARGE classification", p && !((u32)p % PAGE_SIZE) && f10_extent((u32)p)->kind == 5);
        CHECK("f10 LARGE used", a.exec_heap_used == PAGE_ALIGN_UP(bytes));
        CHECK("f10 LARGE zero", all_zero(p, PAGE_ALIGN_UP(bytes)));
        f10_pattern(p, bytes, 1); f10_pattern(p, bytes, 0);
        wrap_mem_free(p);
        CHECK("f10 LARGE free", !f10_extent((u32)p) && as_va_to_pa(a.pd_phys, (u32)p, &pa) &&
              !a.exec_heap_used && ledger_owner_pages(a.owner) == pages);
        f10_snapshot(0); wrap_mem_free(p); f10_snapshot(1); /* unused VA double free */
        void *again = wrap_mem_alloc(bytes);
        CHECK("f10 LARGE same VA", again == p);
        wrap_mem_free(again);
    }
    /* Free the later allocation while the earlier one remains intact. */
    void *hi = wrap_mem_alloc(65536), *lo = wrap_mem_alloc(65536);
    f10_pattern(hi, 65536, 1); wrap_mem_free(lo); f10_pattern(hi, 65536, 0);
    CHECK("f10 later free used", a.exec_heap_used == 65536);
    f10_done(); /* hi deliberately left mapped */

    for (u32 mode = 0; mode < 2; mode++) {
        heap_create(&a, 2);
        void *p = wrap_mem_alloc(65536), *other = wrap_mem_alloc(65536);
        if (mode) {
            wrap_mem_free(p);
            u32 out;
            CHECK("f10 double free ANON fixture", !appmem_map(&a, &a.appmem, &a.appmem_layout,
                  65536, (u32)p, APPMEM_MAP_EXACT, APPMEM_ANON, 0, &out));
        }
        wrap_mem_free(mode ? p : (u8 *)p + 8);
        CHECK("f10 interior or ANON disables", a.exec_heap_used == ~0U && !wrap_mem_alloc(16));
        f10_snapshot(0); wrap_mem_free(other);
        CHECK("f10 disabled trim", !exec_heap_user_trim(&a)); f10_snapshot(1);
        f10_done();
    }
    /* Every map failure falls back only to existing free space. */
    for (u32 mode = 0; mode < 4; mode++) {
        heap_create_size(&a, 2, 4 * MEM_EXEC_HEAP_MIN);
        u32 keep_guard = a.appmem_layout.guard_b;
        if (!mode) { /* Real EFULL, using disjoint ANON pages. */
            for (u32 i = 0; i < APPMEM_EXTENT_MAX - 1; i++) {
                u32 out;
                CHECK("f10 full fixture", !appmem_map(&a, &a.appmem, &a.appmem_layout,
                      PAGE_SIZE, MEM_EXEC_LOAD_ADDR + (4 + 2*i)*PAGE_SIZE,
                      APPMEM_MAP_EXACT, APPMEM_ANON, 0, &out));
            }
        } else if (mode == 1) a.appmem_layout.guard_b = a.appmem_layout.exec_heap_cur_end;
        else if (mode == 3) _disable(); /* EINVAL at prepare. */
        heap_alloc_fail = mode == 2;
        struct appmem_plan plan;
        int rc = appmem_prepare(&a.appmem, &a.appmem_layout, 65536, 0, APPMEM_MAP_TOPDOWN, APPMEM_EXEC_LARGE, 0, &plan);
        CHECK("f10 EFULL fixture", mode != 0 || rc == APPMEM_EFULL);
        CHECK("f10 ENOVA fixture", mode != 1 || rc == APPMEM_ENOVA);
        void *p = wrap_mem_alloc(65536);
        CHECK("f10 fallback existing", (u32)p == MEM_EXEC_HEAP_BASE + BLK_HDR_SIZE &&
              a.exec_heap_used == 65536 + BLK_HDR_SIZE);
        void *q = wrap_mem_alloc(131072);
        CHECK("f10 fallback 128KiB", (u32)q == MEM_EXEC_HEAP_BASE + 65536 + 2*BLK_HDR_SIZE &&
              a.exec_heap_used == 196608 + 2*BLK_HDR_SIZE);
        f10_pattern(p, 65536, 1); f10_pattern(q, 131072, 1);
        f10_snapshot(0);
        u32 maps = heap_map_calls;
        CHECK("f10 fallback no growth", !wrap_mem_alloc(65536) && heap_map_calls == maps + 1); f10_snapshot(1);
        f10_pattern(p, 65536, 0); f10_pattern(q, 131072, 0);
        wrap_mem_free(p);
        CHECK("f10 fallback one free used", a.exec_heap_used == 131072 + BLK_HDR_SIZE);
        wrap_mem_free(q);
        CHECK("f10 fallback free used", !a.exec_heap_used);
        _enable(); heap_alloc_fail = 0; a.appmem_layout.guard_b = keep_guard;
        f10_done();
    }
    heap_create(&a, 2);
    f10_snapshot(0);
    for (u32 n = 0; n < 7; n++) CHECK("rounding overflow rejected", !wrap_mem_alloc(0xfffffff9U + n));
    CHECK("LARGE rounding overflow rejected", !wrap_mem_alloc(0xfffff001U));
    CHECK("LARGE rounding overflow rejected", !wrap_mem_alloc(0xfffffff0U));
    f10_snapshot(1);
    f10_done();

    /* LARGE rollback poisons this AS while an existing arena could satisfy it. */
    heap_create_size(&a, 2, 4 * MEM_EXEC_HEAP_MIN);
    f10_snapshot(0);
    u32 rollback_frees = heap_free_calls;
    heap_alloc_rollback_fail = 2; /* Allocate one page, then fail; rollback cannot free it. */
    void *poisoned = wrap_mem_alloc(65536);
    CHECK("f10 map rollback poison fixture", !heap_alloc_rollback_fail &&
          integration_free_fail && a.appmem_poisoned && heap_free_calls > rollback_frees);
    CHECK("f10 poisoned fallback rejected", !poisoned);
    CHECK("f10 poisoned fallback arena unchanged", !a.exec_heap_used &&
          equal(&a.appmem, &f10_as.appmem, sizeof(a.appmem)) &&
          equal(heap_arena_image, (void *)MEM_EXEC_HEAP_BASE, 4 * MEM_EXEC_HEAP_MIN));
    /* Host-only cleanup: product quarantines this owner forever. */
    host_cr3 = roots[0]; heap_alias_on = 0;
    integration_free_fail = 0; a.appmem_poisoned = 0; live_addrspaces++;
    u32 rollback_owner = a.owner, rollback_reclaimed;
    paging_addrspace_destroy(&a);
    CHECK("f10 rollback fixture reclaim", ledger_reclaim_owner(rollback_owner, &rollback_reclaimed));
    CHECK("f10 rollback fixture retire", ledger_owner_retire(rollback_owner));
    heap_alias_on = 1;

    /* Boundary positions 0, 8, 4088; minimum payload forces the last into the next page. */
    const u32 offsets[] = {0, 8, 4088};
    for (u32 n = 0; n < 3; n++) {
        heap_create(&a, 2);
        void *initial = wrap_mem_alloc(65528);
        u32 live = PAGE_SIZE + offsets[n] - BLK_HDR_SIZE;
        void *p = wrap_mem_alloc(live);
        u32 arena_base = (u32)p - BLK_HDR_SIZE, old_end = a.appmem_layout.exec_heap_cur_end;
        u32 kept_end = arena_base + (n == 2 ? 3 : 2)*PAGE_SIZE;
        u32 used = 65536 + live + BLK_HDR_SIZE, pages = ledger_owner_pages(a.owner);
        f10_pattern(p, live, 1);
        CHECK("f10 trim tail pages", exec_heap_user_trim(&a) == (old_end - kept_end)/PAGE_SIZE);
        CHECK("f10 trim header boundary", f10_extent((u32)p)->end == kept_end &&
              a.appmem_layout.exec_heap_cur_end == kept_end && a.exec_heap_used == used &&
              ledger_owner_pages(a.owner) == pages - (old_end - kept_end)/PAGE_SIZE &&
              ((BlkHdr *)(arena_base + PAGE_SIZE + offsets[n]))->size == kept_end - arena_base - PAGE_SIZE - offsets[n] - BLK_HDR_SIZE);
        f10_pattern(p, live, 0);
        f10_snapshot(0); CHECK("f10 trim zero pages", !exec_heap_user_trim(&a)); f10_snapshot(1);
        /* EXACT growth at the new end merges back into the trimmed arena. */
        void *q = wrap_mem_alloc(65528);
        CHECK("f10 trim regrowth", (u32)q == kept_end + BLK_HDR_SIZE && a.exec_heap_used == used + 65536);
        f10_pattern(p, live, 0); wrap_mem_free(q); wrap_mem_free(p);
        CHECK("f10 empty trim", exec_heap_user_trim(&a) == (kept_end + 65536 - arena_base)/PAGE_SIZE &&
              !f10_extent(arena_base) && a.appmem_layout.exec_heap_cur_end == arena_base && a.exec_heap_used == 65536);
        void *regrow = wrap_mem_alloc(128);
        CHECK("f10 whole trim regrowth", (u32)regrow == arena_base + BLK_HDR_SIZE && a.exec_heap_used == 65536 + 136);
        wrap_mem_free(regrow);
        CHECK("f10 whole regrowth return", exec_heap_user_trim(&a) == 16 && a.exec_heap_used == 65536);
        wrap_mem_free(initial);
        u32 trims = heap_trim_calls;
        f10_snapshot(0); CHECK("f10 INITIAL never trim", !exec_heap_user_trim(&a) && heap_trim_calls == trims);
        CHECK("f10 INITIAL internal refused", appmem_exec_unmap(&a, MEM_EXEC_HEAP_BASE, 65536) == APPMEM_EINVAL);
        f10_snapshot(1); f10_done();
    }
    /* Adjacent FREE blocks after ARENA merge: only the last block shrinks. */
    heap_create(&a, 2);
    CHECK("f10 merged initial", wrap_mem_alloc(65528));
    void *p = wrap_mem_alloc(128);
    CHECK("f10 merged growth", wrap_mem_alloc(65528));
    void *last = (void *)(a.appmem_layout.exec_heap_cur_end - 65536 + BLK_HDR_SIZE);
    /* Fixture reproduces adjacent FREE at a merge boundary without coalescing free(). */
    ((BlkHdr *)last)[-1].magic = BLK_MAGIC_FREE; a.exec_heap_used -= 65536;
    CHECK("f10 adjacent FREE trim", exec_heap_user_trim(&a) == 15 && a.exec_heap_used == 65536 + 136);
    CHECK("f10 adjacent FREE reallocate", wrap_mem_alloc(256) && a.exec_heap_used == 65536 + 136 + 264);
    wrap_mem_free(p); f10_done();
    /* Used tail must not be returned. */
    heap_create(&a, 2); CHECK("f10 used initial", wrap_mem_alloc(65528)); CHECK("f10 used arena", wrap_mem_alloc(65528));
    f10_snapshot(0); CHECK("f10 used tail stays", !exec_heap_user_trim(&a)); f10_snapshot(1); f10_done();

    /* All validation precedes any unmap, including a bad INITIAL and a later arena. */
    for (u32 mode = 0; mode < 4; mode++) {
        heap_create(&a, 2); CHECK("f10 corrupt initial", wrap_mem_alloc(65528));
        void *p1 = wrap_mem_alloc(128); u32 anon;
        CHECK("f10 split fixture", !appmem_map(&a, &a.appmem, &a.appmem_layout, PAGE_SIZE,
              a.appmem_layout.exec_heap_cur_end, APPMEM_MAP_EXACT, APPMEM_ANON, 0, &anon));
        void *p2 = wrap_mem_alloc(65528); wrap_mem_free(p2);
        u32 va = mode == 0 ? MEM_EXEC_HEAP_BASE : (u32)p1 - BLK_HDR_SIZE;
        u32 *entry = &((u32 *)P2V(a.app_pt_phys[(va - MEM_APP_BAND_BASE) >> 22]))[(va >> PAGE_SHIFT) % PTE_COUNT];
        u32 old = *entry;
        if (mode < 2) ((BlkHdr *)va)->magic = 0;
        else *entry = mode == 2 ? old & ~PTE_PRESENT : old & ~PTE_USER;
        f10_snapshot(0);
        CHECK("f10 corrupt trim rejects all", !exec_heap_user_trim(&a) && a.exec_heap_used == ~0U);
        f10_as.exec_heap_used = ~0U; f10_snapshot(1);
        *entry = old; f10_done();
    }
    /* Prepare rejection vs physical-free failure: preserve headers/used in both. */
    for (u32 large = 0; large < 2; large++) for (u32 poison = 0; poison < 2; poison++) {
        heap_create(&a, 2);
        CHECK("f10 failure initial", wrap_mem_alloc(65528));
        void *p = wrap_mem_alloc(large ? 65536 : 128);
        u32 va = large ? (u32)p : PAGE_ALIGN_UP((u32)p + 128 + BLK_HDR_SIZE + BLK_ALIGN);
        u32 pa; CHECK("f10 failure page", !as_va_to_pa(a.pd_phys, va, &pa));
        BlkHdr *tail = large ? (BlkHdr *)p : (BlkHdr *)((u8 *)p + 128);
        BlkHdr hdr = *tail;
        if (poison) integration_free_fail = pa / PAGE_SIZE;
        else _disable(); /* prepare EINVAL; USER validation itself still succeeds */
        f10_snapshot(0);
        if (large) wrap_mem_free(p); else CHECK("f10 failure trim zero", !exec_heap_user_trim(&a));
        CHECK("f10 failure header used", equal(tail, &hdr, sizeof(hdr)) && a.exec_heap_used == f10_as.exec_heap_used &&
              a.appmem_layout.exec_heap_cur_end == f10_as.appmem_layout.exec_heap_cur_end && equal(&a.appmem, &f10_as.appmem, sizeof(a.appmem)));
        if (!poison) { f10_snapshot(1); _enable(); f10_done(); }
        else {
            CHECK("f10 free failure poisoned", a.appmem_poisoned && !wrap_mem_alloc(16) && !exec_heap_user_trim(&a));
            u32 frees = heap_free_calls; wrap_mem_free(p); CHECK("f10 poison free ignored", frees == heap_free_calls);
            u32 pages = ledger_owner_pages(a.owner), owner = a.owner, left = exec_as_leftover_pages;
            host_cr3 = roots[0]; heap_alias_on = 0;
            exec_teardown_app(&g_slot[2]);
            CHECK("f10 poison leftover", exec_as_leftover_pages == left + pages && ledger_owner_pages(owner) == pages);
            /* Host-only cleanup: product quarantines this owner forever. */
            integration_free_fail = 0; a.appmem_poisoned = 0; live_addrspaces++;
            paging_addrspace_destroy(&a);
            u32 reclaimed; CHECK("f10 fixture reclaim", ledger_reclaim_owner(owner, &reclaimed));
            CHECK("f10 fixture retire", ledger_owner_retire(owner)); heap_alias_on = 1;
        }
    }
}
