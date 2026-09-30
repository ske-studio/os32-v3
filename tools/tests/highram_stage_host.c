#include "types.h"
static u32 host_cr3;
#ifdef TEST_IRQ_OFF
#define HOST_IF 2U
#else
#define HOST_IF 0x202U
#endif
static unsigned int host_if = HOST_IF;
static unsigned int host_irq_save(void)
{
    unsigned int f = host_if;
    host_if &= ~0x200U;
    return f;
}
static void host_irq_restore(unsigned int f) { host_if = f; }
#include "paging_host_source.c"
#include "pgalloc_host_source.c"
#include "sys_host_source.c"
__asm__(".globl __sqlite_start\n.set __sqlite_start, 0x200000\n"
        ".globl __sqlite_end\n.set __sqlite_end, 0x240000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n");
static void die(int code)
{
    if (!code && host_if != HOST_IF) code = 2;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static void report(const char *s, u32 n)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
#define CHECK(x) do { if (!(x)) { report("FAIL " #x "\n", sizeof("FAIL " #x "\n") - 1); die(1); } } while (0)
#ifndef PGALLOC_BOOTSTRAP
struct pgalloc_layout {
    void *metadata;
    u32 capacity, metadata_first, workspace_first, workspace_end;
};
#endif
extern int sys_memory_bootstrap_model(struct physmem *, const struct pgalloc_layout *,
                                     int (*)(u32, u32, void *)) __attribute__((weak));
extern int sys_memory_stage_online(void) __attribute__((weak));
extern int paging_verify_identity(u32, u32, void *) __attribute__((weak));
void _start(void)
{
    u32 args[6] = {0x800000, 0x800000, 3, 0x32, 0xffffffffUL, 0};
    u32 result, p, i;
    struct physmem m;
    struct pgalloc_layout l;
    static u32 low[4096];
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x800000);
    paging_init(16384);
    CHECK(sys_memory_stage_online != 0);
    CHECK(sys_memory_bootstrap_model != 0 && paging_verify_identity != 0);
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(physmem_add_trusted(&m, 4096, TEST_END, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_exclude(&m, 5000, 5002, PHYSMEM_MMIO));
    CHECK(physmem_add_trusted(&m, 1048575, 1048576, PHYSMEM_SOURCE_SYNTHETIC));
    l.capacity = pgalloc_metadata_bytes(&m);
    l.metadata_first = 4096 - l.capacity / PAGE_SIZE;
    l.metadata = (void *)(l.metadata_first * PAGE_SIZE);
    l.workspace_end = l.metadata_first;
    l.workspace_first = l.workspace_end - 16;
#ifdef TEST_OOM
    l.workspace_first = l.workspace_end - 1;
#endif
#ifdef TEST_LATE_OOM
    l.workspace_first = l.workspace_end - 8;
#endif
#ifdef TEST_MODEL_WS_ALIAS
    {
        struct physmem *alias = (struct physmem *)(l.workspace_first * PAGE_SIZE);
        *alias = m;
        CHECK(!sys_memory_bootstrap_model(alias, &l, paging_verify_identity));
        CHECK(!initialized && !sys_frozen_end);
        die(0);
    }
#endif
#ifdef TEST_ATOMIC
    {
        struct physmem before = m;
        struct pgalloc_layout bad;
        u32 j, saved, free_before = pgalloc_free_pages();
        unsigned char *a = (unsigned char *)&m, *b = (unsigned char *)&before;
        u32 *storage = (u32 *)l.metadata;
        for (j = 0; j < l.capacity / sizeof(u32); j++) storage[j] = 0xa55aa55aUL;
        for (i = 0; i < 7; i++) {
            bad = l;
            saved = page_tables[l.workspace_first / PTE_COUNT][l.workspace_first % PTE_COUNT];
            if (i == 0) bad.capacity--;
            if (i == 1) bad.workspace_first = bad.workspace_end;
            if (i == 2) bad.workspace_end++;
            if (i == 3) bad.workspace_first = MEM_EXEC_LOAD_ADDR / PAGE_SIZE;
            if (i == 4) page_tables[l.workspace_first / PTE_COUNT][l.workspace_first % PTE_COUNT] &= ~PTE_PRESENT;
            if (i == 5) page_tables[l.workspace_first / PTE_COUNT][l.workspace_first % PTE_COUNT] |= PTE_USER;
            if (i == 6) page_tables[l.workspace_first / PTE_COUNT][l.workspace_first % PTE_COUNT] |= PTE_PCD;
            CHECK(!sys_memory_bootstrap_model(&m, &bad, paging_verify_identity));
            CHECK(!pgalloc_model_state() && !initialized && !sys_frozen_end);
            CHECK(pgalloc_free_pages() == free_before);
            for (j = 0; j < sizeof(m); j++) CHECK(a[j] == b[j]);
            for (j = 0; j < l.capacity / sizeof(u32); j++) CHECK(storage[j] == 0xa55aa55aUL);
            page_tables[l.workspace_first / PTE_COUNT][l.workspace_first % PTE_COUNT] = saved;
        }
    }
#endif
    for (i = 0; i < 4096; i++) low[i] = page_tables[i / PTE_COUNT][i % PTE_COUNT];
#ifdef TEST_LAYOUT_ALIAS
    *(struct pgalloc_layout *)l.metadata = l;
    CHECK(sys_memory_bootstrap_model(&m, (struct pgalloc_layout *)l.metadata, paging_verify_identity));
#else
    CHECK(sys_memory_bootstrap_model(&m, &l, paging_verify_identity));
#endif
    CHECK(sys_usable_mem_end() == l.workspace_first * PAGE_SIZE);
    CHECK(!pgalloc_alloc_page());
    p = 99;
    CHECK(!pgalloc_alloc_n_pfn(1, 4096, TEST_END, &p) && p == 99);
    CHECK(!pgalloc_alloc_n_range(1, l.workspace_first * PAGE_SIZE, l.workspace_end * PAGE_SIZE));
#if defined(TEST_NONMASTER) || defined(TEST_LIVE)
    /* Simulate the CR3/live-AS state that the stage must reject, even though
     * bootstrap intentionally prevents creating a new AS through alloc. */
#ifdef TEST_NONMASTER
    host_cr3 = 0x123000;
#else
    live_addrspaces = 1;
#endif
    CHECK(!sys_memory_stage_online());
    CHECK(!page_tables[4][0] && !page_tables[8] && !page_tables[1023]);
    CHECK(pgalloc_model_state() == PGALLOC_BOOTSTRAP && !pgalloc_alloc_page());
    for (i = 0; i < 4096; i++) CHECK(low[i] == page_tables[i / PTE_COUNT][i % PTE_COUNT]);
    die(0);
#endif
#ifdef TEST_UNMAPPED_WS
    for (i = l.workspace_first; i < l.workspace_end; i++)
        page_tables[i / PTE_COUNT][i % PTE_COUNT] &= ~PTE_PRESENT;
#endif
#if defined(TEST_OOM) || defined(TEST_LATE_OOM) || defined(TEST_UNMAPPED_WS)
    CHECK(!sys_memory_stage_online());
    CHECK(pgalloc_model_state() == PGALLOC_BOOTSTRAP);
    CHECK(!pgalloc_alloc_page() && !pgalloc_alloc_n_pfn(1, 4096, TEST_END, &p));
    CHECK(!page_tables[1023]);
#ifndef TEST_LATE_OOM
    for (i = 8; i < TEST_END / PTE_COUNT; i++) CHECK(!page_tables[i] && !page_directory[i]);
    for (i = 0; i < LEGACY_WORDS; i++) CHECK(!workspace_used[i]);
#else
    CHECK(paging_is_present((TEST_END - 1) * PAGE_SIZE));
#endif
    die(0);
#endif
#ifdef TEST_BAD_HIGH_PDE
    page_directory[4] |= PTE_PCD;
    CHECK(!sys_memory_stage_online());
    CHECK(pgalloc_model_state() == PGALLOC_BOOTSTRAP);
    CHECK(!pgalloc_alloc_page());
    die(0);
#endif
    CHECK(sys_memory_stage_online());
    for (i = 0; i < 4096; i++) CHECK(low[i] == page_tables[i / PTE_COUNT][i % PTE_COUNT]);
#ifdef TEST_RESERVE_TOP
    /* PEGC 8bpp backbuffer (H2) must still be reservable on the model path.
     * The model must carve below the frozen exec ceiling, since
     * metadata/workspace sit above it (the hotdeploy window is gone). */
    {
        u32 ceiling = sys_usable_mem_end();
        u32 need = ((u32)MEM_GFX_BB8_SIZE + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        u32 base = sys_reserve_top((u32)MEM_GFX_BB8_SIZE);
        CHECK(base != 0);
        CHECK(base == ceiling - need);
        CHECK(sys_usable_mem_end() == base);
        /* 予約の分だけ上限が下がる。窓は無いので workspace_first からちょうど need。 */
        CHECK(base == l.workspace_first * PAGE_SIZE - need);
        /* Idempotent for the same size, and the reserved pages never allocate. */
        CHECK(sys_reserve_top((u32)MEM_GFX_BB8_SIZE) == base);
        CHECK(!sys_reserve_top((u32)MEM_GFX_BB8_SIZE + PAGE_SIZE));
        CHECK(!pgalloc_alloc_n_range(1, base, base + need));
        die(0);
    }
#endif
    CHECK(paging_is_present(4096 * PAGE_SIZE));
    CHECK(!paging_is_present(5000 * PAGE_SIZE));
    CHECK(!paging_is_present(TEST_END * PAGE_SIZE));
    CHECK(paging_is_present(0xffffffffUL));
    CHECK(pgalloc_model_state() == PGALLOC_ONLINE);
    CHECK(!pgalloc_alloc_n_pfn(1, l.workspace_first, 4096, &p));
    CHECK(!pgalloc_free_n_pfn(l.workspace_first, 1));
    CHECK(!pgalloc_free_n_pfn(l.metadata_first, 1));
    for (i = 8; i < TEST_END / PTE_COUNT; i++) {
        CHECK((u32)page_tables[i] >= l.workspace_first * PAGE_SIZE);
        CHECK((u32)page_tables[i] < l.workspace_end * PAGE_SIZE);
    }
    CHECK((u32)page_tables[1023] >= l.workspace_first * PAGE_SIZE);
    CHECK((u32)page_tables[1023] < l.workspace_end * PAGE_SIZE);
    CHECK(pgalloc_alloc_n_pfn(1, 1048575, 1048576, &p) && p == 1048575);
    CHECK(pgalloc_free_n_pfn(p, 1));
    CHECK(pgalloc_reserve_pfn(PGALLOC_BASE / PAGE_SIZE, 4096));
    CHECK(pgalloc_alloc_page() == 4096 * PAGE_SIZE);
    CHECK(!sys_memory_stage_online());
    die(0);
}
