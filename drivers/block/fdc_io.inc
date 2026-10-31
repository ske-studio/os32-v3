/* FDC のコマンド・リザルト送受信、IRQ/SIS 待ちと DMA 設定。fdc.c から元の順序で取り込む。 */

/* ======================================================================== */
/*  内部ユーティリティ                                                      */
/* ======================================================================== */

/* 短い遅延 (I/Oポート読み出しで数µs) */
static void fdc_delay(void)
{
    io_wait();
    io_wait();
}

/* ======================================================================== */
/*  FDCコマンド送受信                                                       */
/* ======================================================================== */

/* MSRのRQM=1かつDIO=0 (CPU→FDC方向)を待ってからコマンドバイトを送信 */
static int fdc_send_byte(u8 val)
{
    int timeout;
    for (timeout = 0; timeout < FDC_TIMEOUT_LOOP; timeout++) {
        u8 msr = (u8)inp(FDC_MSR);
        if ((msr & (MSR_RQM | MSR_DIO)) == MSR_RQM) {
            outp(FDC_FIFO, val);
            return 0; /* 成功 */
        }
        fdc_delay();
    }
    return -1; /* タイムアウト */
}

/* MSRのRQM=1かつDIO=1 (FDC→CPU方向)を待ってからリザルトバイトを読み出し */
static int fdc_read_byte(void)
{
    int timeout;
    for (timeout = 0; timeout < FDC_TIMEOUT_LOOP; timeout++) {
        u8 msr = (u8)inp(FDC_MSR);
        if ((msr & (MSR_RQM | MSR_DIO)) == (MSR_RQM | MSR_DIO)) {
            return (int)inp(FDC_FIFO);
        }
        fdc_delay();
    }
    return -1; /* タイムアウト */
}

/* リザルトフェーズ: 最大10バイトのリザルトを読み出し。
 * **1 バイトごとに空回りの上限を掛ける** — CB=1 のまま RQM/DIO が揃わない
 * FDC に当たると、縛りが無ければ i-- のリトライで永久に回る。
 * 戻り値は実際に読めたバイト数 (諦めた時点の i)。 */
static int fdc_read_results(u8 *st, int max)
{
    int i, spin;

    spin = 0;
    for (i = 0; i < max; i++) {
        u8 msr = (u8)inp(FDC_MSR);
        /* CMD BSY=0 なら終了 */
        if ((msr & MSR_BUSY) == 0) break;
        /* RQM=1, DIO=1 なら読み出し */
        if ((msr & (MSR_RQM | MSR_DIO)) == (MSR_RQM | MSR_DIO)) {
            st[i] = (u8)inp(FDC_FIFO);
            spin = 0;       /* 上限はバイトごとに数え直す */
        } else {
            fdc_delay();
            if (++spin >= FDC_MSR_SETTLE_LOOP) break;  /* 出てこない */
            i--; /* リトライ */
        }
    }
    return i;
}

/* ======================================================================== */
/*  IRQ11 待ち                                                              */
/* ======================================================================== */
static int fdc_wait_irq(u32 timeout_ticks)
{
    u32 start = tick_count;
    while (!fdc_irq_fired) {
        if ((tick_count - start) > timeout_ticks) {
            return -1; /* タイムアウト */
        }
    }
    fdc_irq_fired = 0;
    return 0;
}

/* ======================================================================== */
/*  リザルトフェーズがまだ続いているか                                      */
/*                                                                          */
/*  CB が立ったまま FDC→CPU 方向に RQM が立てば、次のリザルトバイトがある。  */
/*  CB が落ちていればコマンドは終わっていて、もうバイトは来ない。           */
/*  直前のバイトを読んだ直後は RQM が落ちているので少しだけ待つ。           */
/* ======================================================================== */
static int fdc_result_pending(void)
{
    int i;

    for (i = 0; i < FDC_MSR_SETTLE_LOOP; i++) {
        u8 msr = (u8)inp(FDC_MSR);
        if ((msr & MSR_BUSY) == 0) return 0;   /* コマンド終了 */
        if ((msr & (MSR_RQM | MSR_DIO)) == (MSR_RQM | MSR_DIO)) return 1;
        fdc_delay();
    }
    return 0;
}

/* ======================================================================== */
/*  Sense Interrupt Status (SIS)                                            */
/*                                                                          */
/*  ST0 を 1 バイト読み、**pending が無いときの invalid 応答 (ST0 = 80h)     */
/*  では PCN を読まずに戻る**。µPD765A の invalid command のリザルトは       */
/*  1 バイトで、2 バイト目を待つと来ないバイトを FDC_TIMEOUT_LOOP 回        */
/*  空転してから諦めることになる。排水ループは pending が尽きるまで回すので */
/*  **毎回 1 度は必ずこの空振りを踏む**。                                   */
/*  NP21/W も同じ 1 バイト応答を返す (src/io/fdc.c の FDC_SenceintStatus は  */
/*  pending 無しで fdc.buf[0] = FDCRLT_IC1 (0x80) / fdc.bufcnt = 1)。        */
/*                                                                          */
/*  戻り値: 0 = ST0 を読めた (pending の有無は *st0 で見る) / -1 = 失敗      */
/* ======================================================================== */
static int fdc_sense_interrupt(u8 *st0, u8 *cyl)
{
    int r0, r1;

    *st0 = 0;
    *cyl = 0;
    if (fdc_send_byte(FDC_CMD_SENSE_INTERRUPT) != 0) return -1;

    r0 = fdc_read_byte();
    if (r0 < 0) return -1;
    *st0 = (u8)r0;

    /* Ready 線の変化 (IC=11b) = 媒体が抜き差しされたかもしれない。
     * そのドライブの世代を進めて先読みを捨てさせ、覚えたシリンダも
     * 捨てる。どこの SIS (排水 / シーク完了待ち / 救済) で見ても同じ。 */
    if ((u8)(*st0 & FDC_ST0_IC_MASK) == FDC_ST0_IC_RDYCHG) {
        int d = (int)(*st0 & FDC_ST0_DS_MASK);
        s_stats.ready_change++;
        fdc_bump_gen(d);
        s_known_cyl[d] = FDC_CYL_UNKNOWN;
    }

    /* 1 バイト応答なら PCN は来ない。MSR でも裏を取る — ST0 の読み方を
     * 間違えても、CB が落ちていれば読みに行かない。 */
    if (fdc_sis_result_bytes(*st0) < FDC_SIS_LEN_NORMAL) return 0;
    if (!fdc_result_pending()) return 0;

    r1 = fdc_read_byte();
    if (r1 < 0) return -1;
    *cyl = (u8)r1;
    return 0;
}

/* ======================================================================== */
/*  未回収の割り込みを排水する                                              */
/*                                                                          */
/*  SEEK / RECALIBRATE を出す前に必ず呼ぶ。遅れて来た seek-end 割り込みを    */
/*  SIS で読み出さないまま次のコマンドを出すと、µPD765A の INT 線が上がり    */
/*  っぱなしになり、**エッジトリガの PIC に次のエッジが来ない**。以後の      */
/*  fdc_wait_irq が全部タイムアウトし、3 回リトライしても読めず f_mount が   */
/*  落ちる — 実機の root panic はこの連鎖だった。                           */
/*                                                                          */
/*  回数は FDC_SIS_DRAIN_MAX で縛る (壊れた FDC で無限ループにしない)。      */
/*  戻り値: 排水した件数。                                                  */
/* ======================================================================== */
static int fdc_drain_interrupts(void)
{
    u8 st0, cyl;
    int i;

    for (i = 0; i < FDC_SIS_DRAIN_MAX; i++) {
        if (fdc_sense_interrupt(&st0, &cyl) != 0) break;
        /* ST0 = 80h = もう pending は無い。 */
        if (fdc_sis_result_bytes(st0) < FDC_SIS_LEN_NORMAL) break;
    }
    fdc_irq_fired = 0;
    return i;
}

/* ======================================================================== */
/*  DMAセットアップ (µPD8237A チャネル2)                                    */
/* ======================================================================== */
/*  ポートを直に叩くのはやめ、drivers/dma/dma8237.c の共通部に渡す。           */
/*  **3 段でなければならない** (票 §1-2): マスク → 積む → 成功したときだけ  */
/*  アンマスク。setup だけに置き換えると、共通部が「積んだらマスクしたまま  */
/*  返す」契約なので、マスクされたままコマンドを出してタイムアウトする。    */
/*                                                                          */
/*  戻り 0 = ch2 が開いた / 負 = 積めなかった (**ch2 は閉じたまま**)。      */
/*  負のときは呼び手が FDC コマンドを出してはいけない。                     */
static int dma_setup(u32 phys_addr, u16 byte_count, int is_write)
{
    int rc;

    dma_chan_mask(FDC_DMA_CHANNEL);
    rc = dma_chan_setup(FDC_DMA_CHANNEL, phys_addr, (u32)byte_count,
                        is_write ? DMA_DIR_FROM_MEM : DMA_DIR_TO_MEM,
                        DMA_MODE_SINGLE);
    if (rc != 0) return rc;
    dma_chan_unmask(FDC_DMA_CHANNEL);
    return 0;
}

