/* ------------------------------------------------------------------------ */
/*  アロケータ                                                              */
/*                                                                          */
/*  カーネルヒープを壊さずに実装そのものを試すため、専用の KHeap             */
/*  インスタンスを静的バッファ上に作る (kheap_* をパラメータ化してある       */
/*  おかげでこれができる)。                                                  */
/* ------------------------------------------------------------------------ */
static u8 ksel_heap_buf[2048];

static void test_heap(void)
{
    KHeap h;
    void *a, *b, *c;
    u32 free_all;
    int i;

    kheap_init(&h, ksel_heap_buf, sizeof(ksel_heap_buf), "selftest");

    a = kheap_alloc(&h, 16);
    check(a != (void *)0, "kheap_alloc basic");
    /* SQLite が double を置くので 8 バイト境界が要る */
    check((((u32)a) & 7) == 0, "kheap_alloc 8-byte aligned");

    b = kheap_alloc(&h, 1);
    check(b != (void *)0 && (((u32)b) & 7) == 0, "kheap_alloc(1) aligned");
    check(kheap_block_size(&h, a) >= 16, "kheap_block_size");

    /* 0 バイト要求と、ヒープ全体を超える要求は NULL */
    check(kheap_alloc(&h, 0) == (void *)0, "kheap_alloc(0) = NULL");
    check(kheap_alloc(&h, sizeof(ksel_heap_buf) * 2) == (void *)0,
          "kheap_alloc too big = NULL");
    /* アライン時に 0 へ化ける値 (整数オーバーフロー) も弾けること */
    check(kheap_alloc(&h, 0xFFFFFFFFUL) == (void *)0,
          "kheap_alloc overflow = NULL");

    /* 解放 → 再確保でブロックが再利用される */
    kheap_free(&h, a);
    kheap_free(&h, b);
    c = kheap_alloc(&h, 16);
    check(c == a, "kheap reuse after free");
    kheap_free(&h, c);

    /* 全域が 1 個のフリーブロックに戻っている (前後の結合が効いている)。
     * 結合漏れがあると、この後の「ほぼ全域」確保が失敗する。 */
    free_all = sizeof(ksel_heap_buf) - 64;
    c = kheap_alloc(&h, free_all);
    check(c != (void *)0, "kheap coalesce to one block");
    if (c) kheap_free(&h, c);

    /* 断片化 → 全解放で必ず 1 個に戻る (結合の取りこぼし検出) */
    {
        void *p[16];
        for (i = 0; i < 16; i++) p[i] = kheap_alloc(&h, 32);
        /* 飛び飛びに解放してから残りを解放 */
        for (i = 0; i < 16; i += 2) kheap_free(&h, p[i]);
        for (i = 1; i < 16; i += 2) kheap_free(&h, p[i]);
        c = kheap_alloc(&h, free_all);
        check(c != (void *)0, "kheap coalesce after fragmentation");
        if (c) kheap_free(&h, c);
    }

    /* 不正な解放を検出して弾くこと (弾かないとヒープ管理が壊れる)。
     * 診断が 1 行出るのは想定内。 */
    a = kheap_alloc(&h, 16);
    kheap_free(&h, a);
    kheap_free(&h, a);                      /* 二重解放 */
    kheap_free(&h, (void *)0x1);            /* 範囲外 */
    kheap_free(&h, (u8 *)a + 1);            /* 非アライン */
    c = kheap_alloc(&h, free_all);
    check(c != (void *)0, "kheap survives invalid frees");
    if (c) kheap_free(&h, c);

    /* 極小サイズのヒープ: ヘッダも入らないなら空ヒープとして扱う
     * (size - BLK_HDR_SIZE のアンダーフローで巨大ブロックに化けた形) */
    {
        KHeap tiny;
        kheap_init(&tiny, ksel_heap_buf, 4, "tiny");
        check(kheap_alloc(&tiny, 1) == (void *)0, "kheap tiny = empty");
    }
}
