/* T3b2: actual fixture + allocator + paging + USER walk. Only machine
 * boundaries and allocation/map failures are supplied by this harness. */
#include <stdarg.h>
#include "types.h"
#include "paging.h"
#include "pgalloc.h"
#include "kprintf.h"
static u32 host_cr3;
static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static void say(const char *s)
{
    u32 n = 0;
    while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(x) do { if (!(x)) { say("FAIL: " #x "\n"); die(1); } } while (0)
static int same(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static u32 operation, fail_at;
static int bad_mapping, bad_pattern;
static int fail_operation(void) { return ++operation == fail_at; }
#define pgalloc_alloc_phys actual_alloc_phys
#define pgalloc_alloc_n_owner actual_alloc_n_owner
#define ledger_owner_new actual_owner_new
#include "pgalloc_host_source.c"
#undef pgalloc_alloc_phys
#undef pgalloc_alloc_n_owner
#undef ledger_owner_new
u32 pgalloc_alloc_phys(u32 owner, int n)
{
    return fail_operation() ? 0 : actual_alloc_phys(owner, n);
}
int pgalloc_alloc_n_owner(u32 owner, int n, u32 first, u32 end, u32 dir, u32 *pfn)
{
    return !fail_operation() && actual_alloc_n_owner(owner, n, first, end, dir, pfn);
}
int ledger_owner_new(u32 kind, u32 id, const char *name, u32 *owner)
{
    return !fail_operation() && actual_owner_new(kind, id, name, owner);
}
#define paging_addrspace_map_user actual_map_user
#include "paging_host_source.c"
#undef paging_addrspace_map_user
int paging_addrspace_map_user(struct addrspace *as, u32 va, u32 pa, u32 flags)
{
    if (fail_operation()) return -1;
    if (va == MEM_EXEC_LOAD_ADDR + PAGE_SIZE) {
        if (bad_mapping) pa = as->pd_phys;
        else if (bad_pattern) ((u8 *)P2V(pa))[PAGE_SIZE / 2] ^= 1;
    }
    return actual_map_user(as, va, pa, flags);
}
#include "access_walk_host_source.c"
/* The fixture only maps private owned RAM; these other walk branches are
 * deliberately unavailable, not a replacement for the USER translator. */
int shlib_read_page(u32 va, u32 pa) { (void)va; (void)pa; return 0; }
u32 exec_tramp_page_addr(void) { return 0; }
void exec_addrspace_abort(struct addrspace *as) { (void)as; CHECK(0); }
static u32 ksel_fail, entries, skips, page_logs, cleanups, results, last_entered;
static void check(int cond, const char *name)
{
    (void)name;
    ksel_fail += !cond;
}
void __cdecl kprintf(u8 attr, const char *fmt, ...)
{
    (void)attr;
    va_list ap;
    va_start(ap, fmt);
    if (same(fmt, "[h4] %s entry va=%08X bytes=%u pages=%u pattern=%s\n")) {
        (void)va_arg(ap, const char *);
        CHECK(va_arg(ap, u32) == MEM_EXEC_LOAD_ADDR + PAGE_SIZE - 17);
        CHECK(va_arg(ap, u32) == PAGE_SIZE + 34);
        CHECK(va_arg(ap, u32) == 3);
        CHECK(same(va_arg(ap, const char *), bad_pattern ? "FAIL" : "ok"));
        entries++;
    } else if (same(fmt, "[h4] %s SKIP (not run): %s allocation failed\n")) {
        skips++;
    } else if (same(fmt, "[h4] %s page=%u va=%08X pfn=%08X user_rw=%u range=%u\n")) {
        int high = same(va_arg(ap, const char *), "high");
        u32 page = va_arg(ap, u32), va = va_arg(ap, u32), pfn = va_arg(ap, u32);
        CHECK(page < 3 && va == MEM_EXEC_LOAD_ADDR + page * PAGE_SIZE);
        int mapped = va_arg(ap, int);
        CHECK(mapped || pfn == 0);
        int range = va_arg(ap, int);
        CHECK(range == (high ? pfn > 32 * MEM_1MB / PAGE_SIZE :
                              pfn < 32 * MEM_1MB / PAGE_SIZE));
        page_logs++;
    } else if (same(fmt, "[h4] %s cleanup owner=%u pages=%u retired=%u\n")) {
        (void)va_arg(ap, const char *);
        CHECK(va_arg(ap, u32) > LEDGER_OWNER_FIXED_LAST);
        CHECK(va_arg(ap, u32) == 0);
        CHECK(va_arg(ap, int) == 1);
        cleanups++;
    } else if (same(fmt, "[h4] %s result=%s entered=%u free_restored=%u\n")) {
        (void)va_arg(ap, const char *);
        const char *result = va_arg(ap, const char *);
        last_entered = va_arg(ap, int);
        CHECK(last_entered == (same(result, "PASS") || bad_pattern));
        CHECK(va_arg(ap, int) == 1);
        results++;
    }
    va_end(ap);
}
#include "h4_backing_source.c"

#define HOST_RAM_END (64UL * MEM_1MB)
static u32 storage[(PGALLOC_META_BYTES(HOST_RAM_END / PAGE_SIZE) + 3) / 4];
static void boot_pool(void)
{
    u32 args[6] = {MEM_1MB, HOST_RAM_END - MEM_1MB, 3, 0x32, ~0U, 0}, mapped;
    __asm__ volatile("int $0x80" : "=a"(mapped) : "a"(90), "b"(args) : "memory");
    CHECK(mapped == MEM_1MB);
    host_map_fixed_paging();
    paging_init(HOST_RAM_END / 1024);
    struct physmem model;
    physmem_bootstrap_legacy(&model, 16 * 1024);
    CHECK(physmem_exclude(&model, MEM_SYSTEM_SPACE_BASE / PAGE_SIZE,
                         MEM_SYSTEM_SPACE_END / PAGE_SIZE, PHYSMEM_RESERVED));
    CHECK(physmem_add_trusted(&model, MEM_HIGH_RAM_BASE / PAGE_SIZE,
                              HOST_RAM_END / PAGE_SIZE, PHYSMEM_SOURCE_SYNTHETIC));
    workspace_first = MEM_SYSTEM_SPACE_BASE / PAGE_SIZE - 16;
    workspace_end = MEM_SYSTEM_SPACE_BASE / PAGE_SIZE;
    CHECK(physmem_reserve_ram(&model, workspace_first, workspace_end));
    init_core(&model, storage, HOST_RAM_END / PAGE_SIZE);
    model_mode = 1;
    CHECK(pgalloc_stage_online());
}
static void reset_observation(u32 failure)
{
    operation = ksel_fail = entries = skips = page_logs = cleanups = results = 0;
    last_entered = 0;
    fail_at = failure;
}
static void recovered(u32 before, u32 root, u32 flags)
{
    CHECK(pgalloc_free_pages() == before && host_cr3 == root && host_arch_if == flags);
    for (u32 o = LEDGER_OWNER_FIXED_LAST + 1; o < LEDGER_MAX_OWNERS; o++)
        CHECK(!ledger_owner_pages(o) && !ledger_owners[o].kind);
    CHECK(ledger_selfcheck("h4-host"));
    CHECK(!ledger_bad_free && !ledger_retire_refused);
}
static void classification_and_patterns(void)
{
    struct addrspace as;
    u32 owner, low, pfns[H4_BUFFER_PAGES], high[H4_BUFFER_PAGES], freed;
    reset_observation(0);
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 0, "control", &owner));
    CHECK(!paging_addrspace_create(&as, owner));
    low = pgalloc_alloc_phys(owner, 1);
    CHECK(low && low < H4_PHYS_BOUNDARY);
    for (u32 i = 0; i < H4_BUFFER_PAGES; i++) {
        /* Nonadjacent, reversed PFNs: VA order, not physical order, defines
         * the known page pattern. Include exactly 32MiB as a negative below. */
        high[i] = H4_PHYS_BOUNDARY / PAGE_SIZE + 7 - 2 * i;
        u32 pfn;
        CHECK(pgalloc_alloc_n_owner(owner, 1, high[i], high[i] + 1,
                                   LEDGER_BOTTOM_UP, &pfn) && pfn == high[i]);
        CHECK(!paging_addrspace_map_user(&as, MEM_EXEC_LOAD_ADDR + i * PAGE_SIZE,
                                        high[i] * PAGE_SIZE, PAGE_RW | PTE_USER));
        for (u32 j = 0; j < PAGE_SIZE; j++)
            ((u8 *)P2V(high[i] * PAGE_SIZE))[j] = h4_pattern(i, j);
    }
    CHECK(h4_collect_pfns(&as, 1, pfns));
    for (u32 i = 0; i < H4_BUFFER_PAGES; i++) CHECK(pfns[i] == high[i]);
    CHECK(h4_buffer_entry(&as, MEM_EXEC_LOAD_ADDR + H4_BUFFER_OFFSET, H4_BUFFER_BYTES));
    /* Corrupt each of the three covered portions, including the final byte. */
    for (u32 i = 0; i < H4_BUFFER_PAGES; i++) {
        u32 offset = i == 0 ? H4_BUFFER_OFFSET : i == 1 ? PAGE_SIZE / 2 : 16;
        u8 *p = P2V(high[i] * PAGE_SIZE + offset);
        *p ^= 1;
        CHECK(!h4_buffer_entry(&as, MEM_EXEC_LOAD_ADDR + H4_BUFFER_OFFSET, H4_BUFFER_BYTES));
        *p ^= 1;
        /* One low page anywhere must invalidate the whole high buffer. */
        CHECK(!paging_addrspace_map_user(&as, MEM_EXEC_LOAD_ADDR + i * PAGE_SIZE,
                                        low, PAGE_RW | PTE_USER));
        CHECK(!h4_collect_pfns(&as, 1, pfns));
        CHECK(!paging_addrspace_map_user(&as, MEM_EXEC_LOAD_ADDR + i * PAGE_SIZE,
                                        high[i] * PAGE_SIZE, PAGE_RW | PTE_USER));
    }
    u32 boundary;
    CHECK(pgalloc_alloc_n_owner(owner, 1, H4_PHYS_BOUNDARY / PAGE_SIZE,
                               H4_PHYS_BOUNDARY / PAGE_SIZE + 1, LEDGER_BOTTOM_UP, &boundary));
    CHECK(!paging_addrspace_map_user(&as, MEM_EXEC_LOAD_ADDR + PAGE_SIZE,
                                    boundary * PAGE_SIZE, PAGE_RW | PTE_USER));
    CHECK(!h4_collect_pfns(&as, 1, pfns));
    CHECK(!paging_addrspace_map_user(&as, MEM_EXEC_LOAD_ADDR + PAGE_SIZE,
                                    high[1] * PAGE_SIZE, PAGE_RO | PTE_USER));
    CHECK(!h4_collect_pfns(&as, 1, pfns));
    /* The production collector must reject missing RW, not just range. */
    u32 *pt = P2V(as.app_pt_phys[0]);
    u32 idx = ((MEM_EXEC_LOAD_ADDR + PAGE_SIZE) >> PAGE_SHIFT) % PTE_COUNT;
    u32 pa;
    unsigned int flags = irq_save();
    CHECK(!as_access_page(&as, MEM_EXEC_LOAD_ADDR + PAGE_SIZE, 1, &pa));
    irq_restore(flags);
    pt[idx] = 0;
    CHECK(!h4_collect_pfns(&as, 1, pfns));
    paging_addrspace_destroy(&as);
    CHECK(ledger_reclaim_owner(owner, &freed) && !ledger_owner_pages(owner));
    CHECK(ledger_owner_retire(owner));
}
void _start(void)
{
    boot_pool();
    u32 before = pgalloc_free_pages(), root = host_cr3;
    for (u32 f = 0; f < 2; f++) {
        host_arch_if = f ? 0x202 : 2;
        reset_observation(0);
        test_h4_backing();
        CHECK(!ksel_fail && entries == 2 && !skips && page_logs == 6 && cleanups == 2 && results == 2);
        recovered(before, root, f ? 0x202 : 2);
        for (int high = 0; high < 2; high++) {
            reset_observation(0);
            CHECK(h4_backing_case(high) == 1 && !ksel_fail);
            u32 operations = operation;
            for (u32 fail = 1; fail <= operations; fail++) {
                reset_observation(fail);
                CHECK(h4_backing_case(high) == 0 && !ksel_fail);
                CHECK(!entries && skips == 1 && !page_logs && results == 1);
                CHECK(cleanups == (fail != 1));
                recovered(before, root, f ? 0x202 : 2);
            }
            for (int fault = 0; fault < 2; fault++) {
                reset_observation(0);
                bad_mapping = !fault;
                bad_pattern = fault;
                CHECK(h4_backing_case(high) == -1 && ksel_fail == 1);
                CHECK(entries == (u32)fault && last_entered == (u32)fault);
                CHECK(!skips && page_logs == 3 && cleanups == 1 && results == 1);
                recovered(before, root, f ? 0x202 : 2);
                bad_mapping = bad_pattern = 0;
            }
        }
    }
    classification_and_patterns();
    recovered(before, root, host_arch_if);
    /* A real empty high interval, without injection: retain all high RAM
     * under a different owner, leaving low RAM available for AS tables. */
    u32 holder, pfn, freed;
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 0, "held", &holder));
    while (pgalloc_alloc_n_owner(holder, 1, H4_PHYS_BOUNDARY / PAGE_SIZE + 1,
                                pgalloc_limit_pfn(), LEDGER_BOTTOM_UP, &pfn)) {}
    reset_observation(0);
    CHECK(h4_backing_case(1) == 0 && skips == 1 && !entries && !ksel_fail);
    CHECK(ledger_reclaim_owner(holder, &freed) && ledger_owner_retire(holder));
    recovered(before, root, host_arch_if);
    say("PASS: h4 real high/low USER PFNs, strict boundary, mixed pages, patterns, all allocation/map failures, owner recovery\n");
    die(0);
}
