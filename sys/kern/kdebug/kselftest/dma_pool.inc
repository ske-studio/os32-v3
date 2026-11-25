/* ------------------------------------------------------------------------ */
/*  DMA プールの配り方 (票 TASK_HAL_WIRING §1-3、TASK_T1_LEDGER §3-7 / R4)   */
/*                                                                          */
/*  表の算数はホスト試験 (tools/tests/test_dma_pool.py) が見る。ここが       */
/*  見るのは **実物の池**: 最悪の並び (R4) が入ること、組が va = P2V(pa)    */
/*  で 16MB 未満・64KB 非またぎであること、失敗時に *out を書かないこと、   */
/*  枯渇と解放が起動のたびに一度踏まれること。**池の番地は焼かない**        */
/*  (T3 で池が 64KB 整列の番地へ移っても同じ試験が通る)。                   */
/*                                                                          */
/*  **後片付けまでが試験。** 途中で return すると池が埋まったまま残り、      */
/*  82557 の probe が取れなくなる。                                          */
/* ------------------------------------------------------------------------ */
#define DMAP_NIC_BYTES   (16UL * 1024UL)  /* 82557 の CB/RFD ≒16KB (R4) */
#define DMAP_HOLE_BYTES  (8UL * 1024UL)   /* 最悪の並びの前置き */

static int dmap_buf_ok(const struct dma_buf *b, u32 size)
{
    return b->size == size && b->va == P2V(b->pa) &&
           !dma_crosses_64k(b->pa, size) &&
           b->pa < DMA_PHYS_LIMIT && size <= DMA_PHYS_LIMIT - b->pa;
}

static void test_dma_pool(void)
{
    struct dma_buf h1, h2, ring, nic, a, b, c, d, x;
    int pre, got_h1, got_h2, got_r, got_n;
    int fit = 1, apart = 1;
    u32 bad_before = dma_pool_bad_free();
    u32 leak_before = dma_pool_leaked();

    check(dma_pool_free_pages() == DMA_POOL_PAGES, "dmap:starts empty");

    /* R4 の最悪の並び: PCM リング 16KB + 82557 ≒16KB。前置きは
     * 0 = 空 / 1 = 先頭に 8KB 使用中 / 2 = 先頭に 8KB の穴 (次の 8KB は
     * 使用中)。どの前置きでも 2 本とも入り、重ならず、64KB をまたがず
     * 16MB 未満であること。 */
    for (pre = 0; pre < 3; pre++) {
        got_h1 = got_h2 = 0;
        if (pre >= 1) {
            got_h1 = (dma_alloc(DMAP_HOLE_BYTES, 0, DMA_PHYS_LIMIT, &h1) == 0);
            if (!got_h1) fit = 0;
        }
        if (pre == 2) {
            got_h2 = (dma_alloc(DMAP_HOLE_BYTES, 0, DMA_PHYS_LIMIT, &h2) == 0);
            if (!got_h2) fit = 0;
            if (got_h1) {
                if (dma_free(&h1) == 0) got_h1 = 0;
                else fit = 0;
            }
        }
        got_r = (dma_alloc(PCM_RING_BYTES, PCM_POOL_ALIGN, DMA_PHYS_LIMIT,
                           &ring) == 0);
        got_n = (dma_alloc(DMAP_NIC_BYTES, 0, DMA_PHYS_LIMIT, &nic) == 0);
        if (!got_r || !got_n) fit = 0;
        if (got_r && !dmap_buf_ok(&ring, PCM_RING_BYTES)) fit = 0;
        if (got_n && !dmap_buf_ok(&nic, DMAP_NIC_BYTES)) fit = 0;
        if (got_r && got_n && ring.pa < nic.pa + DMAP_NIC_BYTES &&
            nic.pa < ring.pa + PCM_RING_BYTES)
            apart = 0;
        if (got_r) (void)dma_free(&ring);
        if (got_n) (void)dma_free(&nic);
        if (got_h1) (void)dma_free(&h1);
        if (got_h2) (void)dma_free(&h2);
    }
    check(fit, "dmap:R4 worst fits");
    check(apart, "dmap:R4 disjoint");
    check(dma_pool_free_pages() == DMA_POOL_PAGES, "dmap:R4 all back");

    /* 失敗時 *out は不変 (内部の 0 を外へ漏らさない)。 */
    x.pa = 0x5A5A5000UL;
    x.va = (void *)0;
    x.size = 0x77;
    check(dma_alloc(33 * 1024, 0, DMA_PHYS_LIMIT, &x) < 0 &&
          x.pa == 0x5A5A5000UL && x.size == 0x77, "dmap:fail keeps out");
    /* limit が池の先頭なら、どの候補も上限を越える。 */
    check(dma_alloc(4096, 0, (u32)MEM_DMA_POOL_BASE, &x) < 0 &&
          x.pa == 0x5A5A5000UL && x.size == 0x77, "dmap:limit refused");
    check(dma_alloc(0, 0, DMA_PHYS_LIMIT, &x) < 0, "dmap:0 bytes refused");

    /* 真ん中を返すと、そこに 8KB が入る。 */
    check(dma_alloc(16 * 1024, 0, DMA_PHYS_LIMIT, &a) == 0 &&
          dma_alloc(16 * 1024, 0, DMA_PHYS_LIMIT, &b) == 0 &&
          dma_alloc(16 * 1024, 0, DMA_PHYS_LIMIT, &c) == 0,
          "dmap:3x16KB fit");
    check(dma_free(&b) == 0, "dmap:free middle");
    check(dma_alloc(8 * 1024, 0, DMA_PHYS_LIMIT, &d) == 0 && d.pa == b.pa,
          "dmap:8KB reuses");

    /* 途中ポインタの解放は数える (装置がまだ書いているかもしれない)。 */
    x = a;
    x.pa += 4096;
    check(dma_free(&x) < 0, "dmap:mid ptr refused");
    check(dma_pool_bad_free() == bad_before + 1, "dmap:bad free cnt");

    /* LEAKED は二度と配らない。 */
    check(dma_mark_leaked(&c) == 0, "dmap:mark_leaked ok");
    check(dma_pool_leaked() == leak_before + 1, "dmap:leak cnt");
    check(dma_free(&c) < 0, "dmap:leaked no free");

    /* 後片付け。LEAKED の c は**戻せない**ので、その 16KB は使えないまま。
     * 起動ごとの自己診断で池を削るわけにはいかないので、
     * **プールを作り直す**。そのため kernel.c は `pci_bind_all` を
     * **この自己診断より後**で呼ぶ (先に呼ぶと driver の span が消える)。 */
    (void)dma_free(&a);
    (void)dma_free(&d);
    dma_pool_init();
    check(dma_pool_free_pages() == DMA_POOL_PAGES,
          "dmap:empty again");
}
