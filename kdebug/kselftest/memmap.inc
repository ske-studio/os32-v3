/* ------------------------------------------------------------------------ */
/*  地図と実物の照合 (票 TASK_KSTACK_USER §4 の 3)                           */
/*                                                                          */
/*  PDE 0 の PTE 1024 本を include/memmap.h から導いた期待値と比べる。       */
/*  exec_init の **後** に呼ぶ — KAPI 踏み台ページ (RO+USER) が張られるのが  */
/*  exec_init なので、前に呼ぶとそれを食い違いとして数えてしまう。           */
/*                                                                          */
/*  逆転した範囲を撥ねた回数も同時に見る。paging_init の                     */
/*  paging_set_not_present(MEM_SHM_RESV_START, MEM_SHM_RESV_END) は          */
/*  start > end なので空振りしているが、呼び側が戻り値を見ていないため       */
/*  今まで誰も気づかなかった (票 §3)。                                       */
/* ------------------------------------------------------------------------ */
static void test_memmap(void)
{
    int bad = paging_memmap_selftest(exec_tramp_page_addr());
    u32 i, n;
    if (paging_v86_session_open()) return;

    /* The floating SHM address belongs to the kernel's __bss_end. */
    check(kdi_kaudit_last_rc(KAUDIT_POST_EXEC_SHM) == 0,
          "paging: SHM boot USER RW WB");

    check(paging_range_reject_count == 0,
          "paging: no reversed (start > end) range was rejected");
    if (paging_range_reject_count != 0) {
        kprintf(0xC1, "[memmap] reversed ranges rejected: %d\n",
                (int)paging_range_reject_count);
    }

    if (bad <= 0) {
        check(bad == 0, "memmap: low memory matches include/memmap.h");
        return;
    }

    /* 食い違いは **件数ぶん** kselftest_fail に積む。1 件にまとめると
     * 「1 か所ずれている」のか「帯ごと食い違っている」のかが見えない。 */
    n = paging_memmap_bad_count;
    if (n > MM_BAD_MAX) n = MM_BAD_MAX;
    for (i = 0; i < n; i++) {
        kprintf(0xC1, "[memmap] %x-%x want=%d seen=%d\n",
                paging_memmap_bad[i * 3 + 0], paging_memmap_bad[i * 3 + 1],
                (int)(paging_memmap_bad[i * 3 + 2] >> 4),
                (int)(paging_memmap_bad[i * 3 + 2] & 0xF));
        check(0, "memmap: band above differs from the map");
    }
}
