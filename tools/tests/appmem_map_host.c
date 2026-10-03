/* Real paging/pgalloc/physmem/appmem; Linux/MMU/IRQ are the only host boundary. */
#include "types.h"
#include "tvram.h"
#include "appmem.h"
static u32 host_cr3;
static void map_sync(u32 root);
#define HOST_MMU_LOAD_CHECK(root) map_sync(root)
#include "paging_source.c"
#include "pgalloc_source.c"
#include "pgalloc_host_fixture.h"
#include "paging_app.h"
u32 map_alloc(u32 owner, int n);
int map_free(u32 owner, u32 pfn, int n);
unsigned int publish_save(void);
unsigned int host_arch_if = 0x202U;
__asm__(".globl __sqlite_start\n.set __sqlite_start, 0x200000\n"
        ".globl __sqlite_end\n.set __sqlite_end, 0x240000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n");
static void say(const char *s, u32 n) {
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static void die(int n) __attribute__((noreturn));
static void die(int n) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(n));
    for (;;) {}
}
static u32 checks;
#define CHECK(label, x) do { checks++; if (!(x)) { \
    say("FAIL: " label "\n", sizeof("FAIL: " label "\n") - 1); die(1); } } while (0)
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
void *kmemset(void *p, int c, u32 n) {
    CHECK("zero IF enabled", _irq_enabled());
    u8 *b = p; while (n--) *b++ = (u8)c; return p;
}
static int equal(const void *a, const void *b, u32 n) {
    const u8 *x = a, *y = b;
    while (n--) if (*x++ != *y++) return 0;
    return 1;
}
#define DATA_MAX (PTE_COUNT + 4)
static struct addrspace a, b, saved_as, saved_b;
static struct appmem_table table, saved_table;
static struct appmem_layout layout;
static u32 base, end, allocs[DATA_MAX + MEM_APP_BAND_MAX_PDES];
static u32 alloc_count, need_pt, calls, reloads, publish_count;
static int fail_at, failed, watching;
static u32 pd_images[3][PDE_COUNT], pt_images[MEM_APP_BAND_MAX_PDES][PTE_COUNT];
static u8 pool_image[sizeof(host_pool_storage)], owner_image[PHYSMEM_LEGACY_MAX_PFN];
static u8 owners_image[sizeof(ledger_owners)];
static u32 saved_used;
#define SNAP_WORDS (PTE_COUNT * 40)
static u32 other_images[2][SNAP_WORDS];
static u32 roots[3];
static u32 *range_entry(u32 va) {
    u32 k = (va - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    u32 phys = a.app_pt_phys[k];
    if (!phys) {
        u32 ordinal = 0;
        u32 first = (base - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
        for (u32 i = first; i <= k; i++) if (!saved_as.app_pt_phys[i]) {
            if (i == k && ordinal < alloc_count && ordinal < need_pt) phys = allocs[ordinal];
            ordinal++;
        }
    }
    return phys ? &((u32 *)P2V(phys))[(va >> PAGE_SHIFT) % PTE_COUNT] : 0;
}
static void other_image(u32 root, u32 bank, int compare) {
    u32 *pd = P2V(root), pos = 0;
    for (u32 k = 0; k < PDE_COUNT; k++) if (pd[k] & PTE_PRESENT) {
        u32 *pt = P2V(pd[k] & ~(PAGE_SIZE - 1U));
        for (u32 j = 0; j < PTE_COUNT; j++, pos++) {
            CHECK("snapshot capacity", pos < SNAP_WORDS);
            if (compare) CHECK("other PT unchanged", other_images[bank][pos] == pt[j]);
            else other_images[bank][pos] = pt[j];
        }
    }
}
static void snapshot(int compare) {
    other_image(roots[0], 0, compare); other_image(roots[2], 1, compare);

    for (u32 r = 0; r < 3; r++) {
        if (compare) CHECK("PD rollback unchanged", equal(pd_images[r], P2V(roots[r]), PAGE_SIZE));
        else for (u32 i = 0; i < PDE_COUNT; i++) pd_images[r][i] = ((u32 *)P2V(roots[r]))[i];
    }
    for (u32 k = 0; k < MEM_APP_BAND_MAX_PDES; k++) if (a.app_pt_phys[k]) {
        if (compare) CHECK("existing PT rollback unchanged", equal(pt_images[k], P2V(a.app_pt_phys[k]), PAGE_SIZE));
        else for (u32 i = 0; i < PTE_COUNT; i++) pt_images[k][i] = ((u32 *)P2V(a.app_pt_phys[k]))[i];
    }
    if (compare) {
        CHECK("AS rollback unchanged", equal(&a, &saved_as, sizeof(a)) && equal(&b, &saved_b, sizeof(b)));
        CHECK("failure table unchanged", equal(&table, &saved_table, sizeof(table)));
        CHECK("PFN rollback", equal(owner_image, owner_map, limit_pfn) &&
              equal(pool_image, host_pool_storage, sizeof(host_pool_storage)) &&
              equal(owners_image, ledger_owners, sizeof(ledger_owners)) && used_pages == saved_used);
    } else {
        saved_as = a; saved_b = b; saved_table = table; saved_used = used_pages;
        for (u32 i = 0; i < sizeof(pool_image); i++) pool_image[i] = ((u8 *)host_pool_storage)[i];
        for (u32 i = 0; i < limit_pfn; i++) owner_image[i] = owner_map[i];
        for (u32 i = 0; i < sizeof(owners_image); i++) owners_image[i] = ((u8 *)ledger_owners)[i];
    }
}
static void prepublication(void) {
    CHECK("prepare table unchanged", equal(&table, &saved_table, sizeof(table)));
    CHECK("prepare AS unchanged", equal(&a, &saved_as, sizeof(a)));
    CHECK("prepare PD unchanged", equal(pd_images[1], P2V(a.pd_phys), PAGE_SIZE));
    for (u32 i = 0; i < need_pt && i < alloc_count; i++) {
        u32 *pt = P2V(allocs[i]);
        for (u32 j = 0; j < PTE_COUNT; j++) {
            if (!pt[j]) continue;
            int found = 0;
            for (u32 va = base; va < end; va += PAGE_SIZE)
                if (range_entry(va) == &pt[j]) found = 1;
            CHECK("PT zero", found);
        }
    }
    for (u32 i = need_pt; i < alloc_count; i++) {
        u32 va = base + (i - need_pt) * PAGE_SIZE;
        u32 *entry = range_entry(va);
        CHECK("staged PRESENT clear", entry && *entry == (allocs[i] | PTE_RW | PTE_USER));
        CHECK("staged ownership", pgalloc_page_owned(allocs[i] / PAGE_SIZE, a.owner));
        for (u32 j = 0; j < PAGE_SIZE; j++)
            CHECK("data zero", ((u8 *)P2V(allocs[i]))[j] == 0);
    }
}
u32 map_alloc(u32 owner, int n) {
    CHECK("alloc IF enabled", _irq_enabled());
    CHECK("one page allocations", n == 1);
    prepublication();
    calls++;
    if (fail_at && calls == (u32)fail_at) { failed = 1; return 0; }
    u32 p = pgalloc_alloc_phys(owner, n);
    CHECK("fixture pool available", p && alloc_count < sizeof(allocs) / sizeof(allocs[0]));
    allocs[alloc_count++] = p;
    /* Dirty every fresh page so omitted zeroing cannot pass accidentally. */
    for (u32 i = 0; i < PAGE_SIZE; i++) ((u8 *)P2V(p))[i] = 0xa5;
    return p;
}
int map_free(u32 owner, u32 pfn, int n) {
    CHECK("no free before publication", failed);
    CHECK("rollback IF enabled", _irq_enabled());
    u32 i;
    for (i = 0; i < alloc_count; i++) if (allocs[i] == pfn * PAGE_SIZE) break;
    CHECK("rollback tracked PFN", i < alloc_count);
    if (i < need_pt) {
        for (u32 j = need_pt; j < alloc_count; j++)
            CHECK("data before PT free", !pgalloc_page_owned(allocs[j] / PAGE_SIZE, owner));
        u32 *pt = P2V(pfn * PAGE_SIZE);
        for (u32 j = 0; j < PTE_COUNT; j++) CHECK("PT cleared before free", !pt[j]);
    }
    return pgalloc_free_n_owner(owner, pfn, n);
}
unsigned int publish_save(void) {
    prepublication();
    CHECK("publish after all pages", alloc_count == need_pt + (end - base) / PAGE_SIZE);
    for (u32 i = 0; i < need_pt; i++) {
        u32 *pt = P2V(allocs[i]);
        for (u32 j = 0; j < PTE_COUNT; j++) {
            if (!pt[j]) continue;
            int found = 0;
            for (u32 va = base; va < end; va += PAGE_SIZE) if (range_entry(va) == &pt[j]) found = 1;
            CHECK("PT zero", found);
        }
    }
    publish_count++;
    return irq_save();
}
static void map_sync(u32 root) {
    if (!watching) return;
    CHECK("reload target", root == a.pd_phys);
    CHECK("reload IF disabled", !_irq_enabled());
    CHECK("extent before CR3", table.e[0].base == base && table.e[0].end == end);
    for (u32 va = base; va < end; va += PAGE_SIZE) {
        u32 *entry = range_entry(va);
        u32 k = (va - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
        CHECK("PTE PDE before CR3", entry && (*entry & (PAGE_RW | PTE_USER)) == (PAGE_RW | PTE_USER) &&
              ((u32 *)P2V(a.pd_phys))[APP_BAND_PDE + k] == (a.app_pt_phys[k] | PAGE_RW | PTE_USER));
    }
    reloads++;
}
static void start_watch(u32 lo, u32 hi, int failure) {
    base = lo; end = hi; fail_at = failure; alloc_count = calls = reloads = publish_count = 0;
    failed = 0; need_pt = 0;
    for (u32 k = (base - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
         k <= (end - 1 - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE; k++)
        if (!a.app_pt_phys[k]) need_pt++;
    snapshot(0); watching = 1;
}
static void no_np(void) {
    for (u32 k = 0; k < MEM_APP_BAND_MAX_PDES; k++) if (a.app_pt_phys[k]) {
        u32 *pt = P2V(a.app_pt_phys[k]);
        for (u32 i = 0; i < PTE_COUNT; i++) CHECK("no NP residue", !pt[i] || (pt[i] & PTE_PRESENT));
    }
}
static int map(u32 size, u32 hint, u32 flags, u32 *out) {
    return appmem_map(&a, &table, &layout, size, hint, flags, APPMEM_ANON, 0, out);
}
static void cleanup(void) {
    watching = 0; paging_load_cr3(paging_kernel_pd_phys());
    paging_addrspace_free_user_range(&a, base, end);
    paging_addrspace_destroy(&a);
    CHECK("owner all returned", !ledger_owner_pages(a.owner));
    table = (struct appmem_table){0};
}
static void make_as(u32 owner) {
    CHECK("create AS", !paging_addrspace_create_lease(&a, owner));
    roots[1] = a.pd_phys;
}
static void case_map(u32 owner, int existing, int active, u32 size) {
    make_as(owner);
    u32 lo = MEM_EXEC_LOAD_ADDR + MEM_APP_BAND_PDE_SIZE - PAGE_SIZE -
             (MEM_EXEC_LOAD_ADDR - MEM_APP_BAND_BASE);
    if (existing) {
        u32 k = (lo - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
        u32 p = pgalloc_alloc_phys(owner, 1);
        CHECK("existing PT", p);
        for (u32 j = 0; j < PTE_COUNT; j++) ((u32 *)P2V(p))[j] = 0;
        a.app_pt_phys[k] = p;
        ((u32 *)P2V(a.pd_phys))[APP_BAND_PDE + k] = p | PAGE_RW; /* USER propagates at commit */
    }
    paging_load_cr3(active ? a.pd_phys : paging_kernel_pd_phys());
    start_watch(lo, lo + size, 0);
    u32 total = need_pt + size / PAGE_SIZE;
    watching = 0;
    for (u32 nth = 1; nth <= total; nth++) {
        start_watch(lo, lo + size, nth);
        u32 out = 0x12345678, pt_before = paging_app_pt_nospc_count, data_before = paging_app_data_nospc_count;
        CHECK("failure NOSPC", map(size, lo, APPMEM_MAP_EXACT, &out) == APPMEM_ENOSPC);
        CHECK("failure output unchanged", out == 0x12345678);
        CHECK("failure no publish reload", !publish_count && !reloads);
        CHECK("failure reason", paging_app_pt_nospc_count == pt_before + (nth <= need_pt) &&
              paging_app_data_nospc_count == data_before + (nth > need_pt));
        CHECK("IF CR3 restored", _irq_enabled() && host_cr3 == (active ? a.pd_phys : roots[0]));
        snapshot(1); no_np(); watching = 0;
    }
    start_watch(lo, lo + size, 0);
    u32 out = 0;
    CHECK("map success", !map(size, lo, APPMEM_MAP_EXACT, &out) && out == lo);
    CHECK("publish reload count", publish_count == 1 && reloads == (u32)active);
    CHECK("owner increment", ledger_owner_pages(owner) == 2 + (existing ? 1 : 0) + total);
    CHECK("success IF CR3", _irq_enabled() && host_cr3 == (active ? a.pd_phys : roots[0]));
    CHECK("master unchanged", equal(pd_images[0], P2V(roots[0]), PAGE_SIZE));
    CHECK("other AS unchanged", equal(pd_images[2], P2V(roots[2]), PAGE_SIZE));
    other_image(roots[0], 0, 1); other_image(roots[2], 1, 1);
    CHECK("other AS metadata unchanged", equal(&b, &saved_b, sizeof(b)));
    for (u32 va = lo; va < end; va += PAGE_SIZE) {
        u32 e = *range_entry(va);
        CHECK("successful PTE", (e & (PAGE_SIZE - 1U)) == (PAGE_RW | PTE_USER));
        u32 k = (va - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
        CHECK("successful PDE", ((u32 *)P2V(a.pd_phys))[APP_BAND_PDE + k] == (a.app_pt_phys[k] | PAGE_RW | PTE_USER));
        CHECK("successful owner", pgalloc_page_owned(e / PAGE_SIZE, owner));
        for (u32 i = 0; i < PAGE_SIZE; i++) CHECK("success data zero", ((u8 *)P2V(e & ~(PAGE_SIZE - 1U)))[i] == 0);
    }
    no_np(); cleanup();
}
static void reject_cases(u32 owner) {
    make_as(owner);
    base = layout.primary_mapped_end; end = base + PAGE_SIZE;
    u32 out = 0x12345678;
    for (u32 variant = 0; variant < 5; variant++) {
        start_watch(base, end, 0);
        if (variant == 0) kctx_irq_depth = 1;
        if (variant == 1) kctx_exc_depth = 1;
        if (variant == 2) host_cr3 = b.pd_phys;
        if (variant == 3) _disable();
        u32 owner_saved = a.owner;
        if (variant == 4) a.owner = b.owner;
        u32 root_before = host_cr3; int if_before = _irq_enabled();
        CHECK("context rejected", map(PAGE_SIZE, base, APPMEM_MAP_EXACT, &out) == APPMEM_EINVAL);
        CHECK("reject IF CR3 unchanged", host_cr3 == root_before && _irq_enabled() == if_before);
        a.owner = owner_saved; kctx_irq_depth = kctx_exc_depth = 0; host_cr3 = roots[0]; _enable();
        CHECK("context no allocation", !calls && out == 0x12345678);
        snapshot(1); watching = 0;
    }
    /* Table absent but PTE present/NP: reject before any allocation or write. */
    u32 pt = pgalloc_alloc_phys(owner, 1), data = pgalloc_alloc_phys(owner, 1);
    u32 k = (base - MEM_APP_BAND_BASE) / MEM_APP_BAND_PDE_SIZE;
    a.app_pt_phys[k] = pt;
    ((u32 *)P2V(a.pd_phys))[APP_BAND_PDE + k] = pt | PAGE_RW;
    for (u32 i = 0; i < PTE_COUNT; i++) ((u32 *)P2V(pt))[i] = 0;
    for (u32 variant = 0; variant < 3; variant++) {
        *range_entry(base) = variant == 2 ? PTE_RW : data | PTE_RW | (variant ? 0 : PTE_PRESENT);
        start_watch(base, end, 0);
        CHECK("nonzero PTE rejected", map(PAGE_SIZE, base, APPMEM_MAP_EXACT, &out) == APPMEM_EINVAL);
        CHECK("nonzero PTE no allocation", !calls && out == 0x12345678);
        snapshot(1); watching = 0;
    }
    *range_entry(base) = 0;
    CHECK("fixture data return", pgalloc_free_n_owner(owner, data / PAGE_SIZE, 1));
    /* Wrong PDE frame/rights and wrong PT owner are rejected before writes. */
    u32 d = ((u32 *)P2V(a.pd_phys))[APP_BAND_PDE + k];
    const u32 invalid[] = {d | PTE_PS, d & ~PTE_PRESENT, d ^ PAGE_SIZE};
    for (u32 i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        ((u32 *)P2V(a.pd_phys))[APP_BAND_PDE + k] = invalid[i];
        CHECK("invalid PDE rejected", map(PAGE_SIZE, base, APPMEM_MAP_EXACT, &out) == APPMEM_EINVAL);
    }
    ((u32 *)P2V(a.pd_phys))[APP_BAND_PDE + k] = d;
    CHECK("transfer fixture", ledger_transfer(pt / PAGE_SIZE, 1, owner, b.owner));
    CHECK("PT owner rejected", map(PAGE_SIZE, base, APPMEM_MAP_EXACT, &out) == APPMEM_EINVAL);
    CHECK("transfer back", ledger_transfer(pt / PAGE_SIZE, 1, b.owner, owner));
    start_watch(base, end, 0);
    CHECK("invalid size no alloc", map(0, base, APPMEM_MAP_EXACT, &out) == APPMEM_EINVAL && !calls);
    snapshot(1); watching = 0;
    for (u32 i = 0; i < APPMEM_EXTENT_MAX; i++)
        table.e[i] = (struct appmem_extent){base + 2 * i * PAGE_SIZE,
            base + (2 * i + 1) * PAGE_SIZE, APPMEM_ANON, 0};
    start_watch(base, end, 0);
    CHECK("FULL before allocation", map(PAGE_SIZE, 0, 0, &out) == APPMEM_EFULL && !calls);
    snapshot(1); watching = 0;
    start_watch(base, end, 0);
    CHECK("ENOVA before allocation", map(PAGE_SIZE, base, APPMEM_MAP_EXACT, &out) == APPMEM_ENOVA && !calls);
    snapshot(1); watching = 0;
    cleanup();
}
static void number(u32 n) {
    char buf[10]; u32 len = 0;
    do { buf[len++] = '0' + n % 10; n /= 10; } while (n);
    for (u32 i = 0; i < len / 2; i++) { char c = buf[i]; buf[i] = buf[len - i - 1]; buf[len - i - 1] = c; }
    say(buf, len);
}
void _start(void) {
    u32 args[6] = {MEM_POOL_BASE, 0x4000000, 3, 0x32, 0xffffffffUL, 0}, result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK("host RAM", result == MEM_POOL_BASE);
    host_map_fixed_paging(); paging_init(17408); host_pool_boot(17408);
    u32 oa, ob;
    CHECK("owner A", ledger_owner_new(LEDGER_KIND_AS, 0, "A", &oa));
    CHECK("owner B", ledger_owner_new(LEDGER_KIND_AS, 0, "B", &ob));
    CHECK("AS B", !paging_addrspace_create_lease(&b, ob));
    roots[0] = paging_kernel_pd_phys(); roots[2] = b.pd_phys;
    layout = (struct appmem_layout){MEM_EXEC_LOAD_ADDR + 1, MEM_EXEC_LOAD_ADDR + 2 * PAGE_SIZE,
        MEM_EXEC_HEAP_BASE + MEM_EXEC_HEAP_MIN, MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE - MEM_GUARD_SIZE};
    /* Boundary-crossing two PTs, then existing/new mixture, on both roots. */
    for (int existing = 0; existing < 2; existing++) for (int active = 0; active < 2; active++)
        case_map(oa, existing, active, 3 * PAGE_SIZE);
    reject_cases(oa);
    CHECK("bad frees zero", !paging_app_bad_free_count && !ledger_bad_free);
    paging_addrspace_destroy(&b);
    CHECK("owner B returned", !ledger_owner_pages(ob));
    say("PASS appmem_map CHECK=", sizeof("PASS appmem_map CHECK=") - 1); number(checks); say("\n", 1);
    die(0);
}
