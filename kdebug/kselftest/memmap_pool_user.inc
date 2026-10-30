static void test_memmap_pool_user(void)
{
    u32 tramp = exec_tramp_page_addr();
    int clean, poked;

    if (!selftest_dma_available()) return;

    /* 変異の前。ここが 0 でなければ test_memmap が既に報告している。 */
    clean = paging_memmap_selftest(tramp);
    check(clean == 0, "mmu:map ok b4");
    if (clean != 0) return;   /* 既に壊れている。変異しても意味が無い */

    if (paging_poke_user_bit(MEM_DMA_POOL_BASE, 1) != 0) {
        check(0, "mmu:PTE poke failed");
        return;
    }
    poked = paging_memmap_selftest(tramp);
    /* **必ず戻す** — 落ちたかどうかを見る前に戻しておく。 */
    (void)paging_poke_user_bit(MEM_DMA_POOL_BASE, 0);

    check(poked > 0, "mmu:USER bit caught");

    /* 戻したので、もう一度通ること。TLB は poke の中で無効化している。 */
    check(paging_memmap_selftest(tramp) == 0,
          "mmu:map ok after");
}
