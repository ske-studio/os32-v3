/* ------------------------------------------------------------------------ */
/*  8237 の共通部 (票 TASK_HAL_WIRING §1-2)                                  */
/*                                                                          */
/*  **悪い引数でハードウェアに触らないこと**だけを見る。良い引数の転送は     */
/*  FDC が起動のたびに踏んでいる (W2)。ここで出せない out が 1 つでも        */
/*  出ると、他チャネル (CS4231) の設定を壊す。                              */
/* ------------------------------------------------------------------------ */
static void test_dma8237(void)
{
    int done = -1, tc = -1;

    check(dma8237_ready() != 0, "dma:init b4 fdc");
    check(dma_above_1mb_state() != DMA_A20_UNKNOWN,
          "dma:0439h recorded");

    /* ch は 0〜3。範囲外は表も引かない。 */
    check(dma_chan_setup(DMA_CHAN_COUNT, MEM_DMA_POOL_BASE, 512,
                         DMA_DIR_TO_MEM, DMA_MODE_SINGLE) < 0,
          "dma:bad ch refused");
    /* 0 バイトは積めない (カウントに -1 を積むので 65536 になる)。 */
    check(dma_chan_setup(2, MEM_DMA_POOL_BASE, 0, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) < 0,
          "dma:0 bytes refused");
    /* 64KB バンクまたぎ ([HW2])。プールの次の境界の手前から。 */
    check(dma_chan_setup(3, (MEM_DMA_POOL_BASE & ~(DMA_BANK_SIZE - 1)) +
                         DMA_BANK_SIZE - 0x1000UL, 0x4000UL,
                         DMA_DIR_TO_MEM, DMA_MODE_SINGLE) < 0,
          "dma:64KB cross refused");
    /* 16MB 以上 */
    check(dma_chan_setup(3, 0x1000000UL, 512, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) < 0,
          "dma:>=16MB refused");

    /* 引数が通っても **マスクしていなければ積まない**。ch3 は誰も使って
     * いないので、ここで触っても他の装置に当たらない。 */
    check(dma_chan_setup(3, MEM_DMA_POOL_BASE, 512, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) < 0,
          "dma:unmasked refused");

    /* マスクしてから積むと通り、done / tc_event が落ちている。
     * **積んだだけでマスクは外れない**ので、ch3 は閉じたまま。 */
    dma_chan_mask(3);
    check(dma_chan_setup(3, MEM_DMA_POOL_BASE, 512, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) == 0,
          "dma:masked setup ok");
    check(dma_chan_state(3, &done, &tc) == 0, "dma:state readable");
    check(done == 0 && tc == 0, "dma:setup cleared TC");
    dma_chan_mask(3);   /* 念のため閉じたままにしておく */
}
