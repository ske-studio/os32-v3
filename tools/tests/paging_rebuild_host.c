/* 狭い確保処理の再構築用。既存の有効な公開 API 回帰は変更しない。 */
#define _start bounds_start
#include "paging_bounds_host.c"
#undef _start
#if defined(TEST_SPARSE) || defined(TEST_ATTRS) || defined(TEST_FINAL)
static u32 high_pt[PTE_COUNT] __attribute__((aligned(PAGE_SIZE)));
static int verify_metadata(u32 first, u32 pages, void *backing)
{
    u32 i, addr;
    if ((u32)backing != first * PAGE_SIZE) return 0;
    for (i = 0; i < pages; i++) {
        addr = (first + i) * PAGE_SIZE;
        if (page_tables[addr >> 22][(addr >> PAGE_SHIFT) % PTE_COUNT] !=
            (addr | PAGE_RW)) return 0;
    }
    return 1;
}
void _start(void)
{
    struct physmem model;
    u32 args[6] = {0x400000, 0x1C00000, 3, 0x32, 0xFFFFFFFF, 0};
    u32 result, bytes, high = 0x1400000UL / PAGE_SIZE;
#ifdef TEST_FINAL
    high = PHYSMEM_MAX_PFN - 1;
#endif
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x400000);
    paging_init(16384);
    physmem_bootstrap_legacy(&model, 16384);
    CHECK(physmem_add_trusted(&model, high, high + 1, PHYSMEM_SOURCE_SYNTHETIC));
    bytes = pgalloc_metadata_bytes(&model);
    CHECK(pgalloc_init_model(&model, (void *)MEM_EXEC_LOAD_ADDR, bytes,
                            MEM_EXEC_LOAD_ADDR / PAGE_SIZE, verify_metadata));
    CHECK(pgalloc_reserve_pfn(PGALLOC_BASE / PAGE_SIZE, MEM_EXEC_LOAD_ADDR / PAGE_SIZE));
    CHECK(pgalloc_reserve_pfn((MEM_EXEC_LOAD_ADDR + bytes) / PAGE_SIZE,
                             0x1000000UL / PAGE_SIZE));
    /* This fixture exercises the retained legacy mapped-candidate scanner,
     * not the new MODEL stage (covered by highram_stage_host.c). Install a
     * synthetic eligibility snapshot explicitly; no production reset/bypass. */
    online = 1;
    model_mode = 0;
    CHECK(pgalloc_free_pages() == 1);
    CHECK(high * PAGE_SIZE > PGALLOC_BASE + pgalloc_total_pages() * PAGE_SIZE);
    page_tables[high / PTE_COUNT] = high_pt;
    page_directory[high / PTE_COUNT] = (u32)high_pt | PAGE_RW;
    high_pt[high % PTE_COUNT] = high * PAGE_SIZE | PAGE_RW;
#ifdef TEST_ATTRS
    {
        u32 bad_pde[] = {PTE_PCD, PTE_PWT, PTE_PS, PTE_PRESENT, PTE_RW, PAGE_SIZE};
        u32 bad_pte[] = {PTE_PRESENT, PTE_RW, PTE_USER, PTE_PCD, PTE_PWT, PAGE_SIZE};
        u32 i, j, saved_pde = page_directory[high / PTE_COUNT];
        u32 saved_pte = high_pt[high % PTE_COUNT], before_calls;
        u32 *backing = (u32 *)(high * PAGE_SIZE);
        /* Isolate this candidate: permanently reserved low RAM is not a
         * backing mapping in this fixture, so no unrelated dispatch occurs. */
        page_directory[2] = page_directory[3] = 0;
        for (j = 0; j < PTE_COUNT; j++) backing[j] = 0xA55AA55AUL;
        for (i = 0; i < sizeof(bad_pde) / sizeof(bad_pde[0]); i++) {
            page_directory[high / PTE_COUNT] = saved_pde ^ bad_pde[i];
            before_calls = calls;
            CHECK(prepare_tables(0x2000000UL / PAGE_SIZE, 1) == -1);
            CHECK(calls == before_calls && used == 0 && pgalloc_free_pages() == 1);
            CHECK(!page_tables[8] && !page_directory[8]);
            for (j = 0; j < PTE_COUNT; j++) CHECK(backing[j] == 0xA55AA55AUL);
        }
        page_directory[high / PTE_COUNT] = saved_pde;
        for (i = 0; i < sizeof(bad_pte) / sizeof(bad_pte[0]); i++) {
            high_pt[high % PTE_COUNT] = saved_pte ^ bad_pte[i];
            before_calls = calls;
            CHECK(prepare_tables(0x2000000UL / PAGE_SIZE, 1) == -1);
            CHECK(calls == before_calls && used == 0 && pgalloc_free_pages() == 1);
            CHECK(!page_tables[8] && !page_directory[8]);
            for (j = 0; j < PTE_COUNT; j++) CHECK(backing[j] == 0xA55AA55AUL);
        }
        high_pt[high % PTE_COUNT] = saved_pte;
        SAY("PASS: invalid PDE/PTE backing rejected before allocation and writes");
    }
#endif
#ifdef TEST_FINAL
    /* Linux i386 cannot map this page: only mapped mock PT metadata is read.
     * The actual PFN allocator reserves it; never dereference its address. */
    CHECK(pgalloc_limit_pfn() == PHYSMEM_MAX_PFN);
    CHECK(reserve_table() == (u32 *)0xFFFFF000UL);
    CHECK(used == 1 && pgalloc_free_pages() == 0);
    CHECK(reserve_table() == 0);
    CHECK(pgalloc_free_n_pfn(high, 1));
    CHECK(used == 0 && pgalloc_free_pages() == 1);
    SAY("PASS: final PFN exact allocation without wrapped byte end or dereference");
#else
    CHECK(prepare_tables(0x2000000UL / PAGE_SIZE, 1) == 0);
    CHECK(page_tables[8] == (u32 *)(high * PAGE_SIZE));
    CHECK(pgalloc_free_pages() == 0 && used == 1);
    SAY("PASS: sparse permanent reservations, sole safe free PT above count and 16MiB");
#endif
    die(0);
}
#else
void _start(void)
{
    u32 args[6] = {0x400000, 0xC00000, 3, 0x32, 0xFFFFFFFF, 0};
    u32 result, before, c;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x400000);
    paging_init(16384);
    pgalloc_init(16384);
    before = used;
    c = calls;
#ifdef TEST_NONMASTER
    host_cr3 = 0x123000;
    CHECK(prepare_tables(0x2000000 >> PAGE_SHIFT, 1) == -1);
    CHECK(calls == c && used == before);
    CHECK(!page_tables[8] && !page_directory[8]);
    CHECK(host_cr3 == 0x123000 && live_addrspaces == 0);
    SAY("PASS: rebuild nonmaster rejection before allocation");
#else
    limit = used + 1;
    CHECK(prepare_tables(0x23FF000 >> PAGE_SHIFT, 2) == -1);
    CHECK(calls > c + 1);
    CHECK(used == before);
    CHECK(!page_tables[8] && !page_tables[9]);
    CHECK(!page_directory[8] && !page_directory[9]);
    limit = 16;
    CHECK(paging_map_phys(0x23FF000, 0x12345000, 2, PAGE_RW | PTE_PCD) == 0);
    CHECK(used == before + 2);
    CHECK(page_tables[8][1023] == (0x12345000 | PAGE_RW | PTE_PCD));
    CHECK(page_tables[9][0] == (0x12346000 | PAGE_RW | PTE_PCD));
    CHECK(!page_tables[8][1022] && !page_tables[9][1]);
    SAY("PASS: rebuild multi-PT rollback and successful retry");
#endif
    die(0);
}
#endif
