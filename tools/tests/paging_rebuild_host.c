/* 狭い確保処理の再構築用。既存の有効な公開 API 回帰は変更しない。
 * T1a (TASK_T1_LEDGER §4-1) で reserve_table の legacy 分岐 (恒等 RW ページを
 * 1 枚ずつ探す) を撤去したので、それを突いていた sparse / attrs / final の
 * 3 本も撤去した。master の PT は workspace からだけ来る — workspace の PTE
 * 検査は memory_boot の metadata_pte / workspace_pte が見る。 */
#define _start bounds_start
#include "paging_bounds_host.c"
#undef _start
void _start(void)
{
    u32 args[6] = {0x400000, 0xC00000, 3, 0x32, 0xFFFFFFFF, 0};
    u32 result, before, c;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x400000);
    host_map_fixed_paging();
    paging_init(16384);
    host_pool_boot_ws(16384, HOST_WS_FIRST, HOST_WS_END);
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
