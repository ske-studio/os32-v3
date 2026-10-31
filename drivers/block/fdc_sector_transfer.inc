/* 単発 read/write の転送骨格。バッファは方向ごとに型を保って渡す。 */
static int fdc_transfer_sector_geom(int drv, int cyl, int head, int sect,
                                     const struct fdc_geom *g, void *read_buf,
                                     const void *write_buf, int write)
{
    u8 results[FDC_RESULT_BYTES];
    int n, retry;
    u32 phys = fdc_dma_phys();
    u16 bps = g->bps;
    const char *phase = "seek";
    int have_results = 0;   /* results[] が埋まったか (診断の出し分け) */
    int write_protected = 0;
    int dma_armed = 0;      /* DMA ch2 をアンマスクしたまま抜けていないか */

    kmemset(results, 0, sizeof(results));
    if (write) {
        /* **書く前に世代を進める** — 先読みが古いセクタを当てないようにする。
         * dev.c / KAPI もここを通る。失敗しても途中まで書いたかもしれないので
         * 世代は進めたままにし、入力のコピーも再試行の前に 1 回だけ行う。 */
        fdc_bump_gen(drv);
        s_stats.writes++;
        kmemcpy(dma_buffer, (const u8 *)write_buf, (u32)bps);
    } else {
        s_stats.single_reads++;
    }

    for (retry = 0; retry < FDC_RW_RETRIES; retry++) {
        int seek_rc;

        /* 0. 次を出す前に回復する。最後の失敗の後には呼ばない。
         * リセットと recalibrate を挟まないと、INT 線が上がったまま同じ形で
         * 失敗し続ける。fdc_recover は先頭で ch2 をマスクする。
         * 診断は最後の試行だけを映すので results[] も消す。 */
        if (retry > 0) {
            if (!write) s_stats.single_retries++;
            (void)fdc_recover(drv);
            dma_armed = 0;
            have_results = 0;
            kmemset(results, 0, sizeof(results));
        }

        /* 1. シーク。NR は回復で直らないのでリトライも回復もしない。
         * HDD 起動時の空の /fd0 への試行も 1 回で打ち切る。 */
        phase = "seek";
        seek_rc = fdc_seek(drv, cyl, head);
        if (seek_rc == FDC_RC_NOT_READY) break;
        if (seek_rc != 0) continue;

        /* 2. DMA セットアップ。read は FDC→メモリ、write はメモリ→FDC。
         * 負なら ch2 は閉じたままなのでコマンドを出さない。
         * 成功なら ch2 が開くので、途中で抜けても必ず閉じてから戻る。 */
        if (dma_setup(phys, bps, write) != 0) { phase = "dma"; break; }
        dma_armed = 1;
        phase = "cmd";

        /* 3. Read/Write Data コマンド送信 */
        if (fdc_send_sector_command(write ? FDC_CMD_WRITE_DATA : FDC_CMD_READ_DATA,
                                     drv, cyl, head, sect, g) != 0) continue;

        /* 4. IRQ 待ち (データ転送完了) */
        phase = "irq";
        if (fdc_wait_irq(FDC_RW_TIMEOUT_TICKS) != 0) continue;

        /* 5. リザルト読み出し (7 バイト) */
        phase = "result";
        n = fdc_read_results(results, FDC_RESULT_BYTES);
        have_results = (n > 0);
        if (n < FDC_RESULT_BYTES) continue;

        /* 6. ST0 の bit6-7 が 00 なら成功。read は成功時だけコピーする。 */
        if ((results[0] & FDC_ST0_IC_MASK) == FDC_ST0_IC_NORMAL) {
            if (!write) kmemcpy((u8 *)read_buf, dma_buffer, (u32)bps);
            return 0;
        }

        /* WRITE の NW を NR より先に見る。読み終えたので DMA を閉じて抜ける。 */
        if (write && (results[1] & FDC_ST1_NW)) {
            dma_chan_mask(FDC_DMA_CHANNEL);
            dma_armed = 0;
            write_protected = 1;
            break;
        }

        /* NR は両方向とも回復より前に打ち切る。リザルトを読み終えているので
         * DMA を閉じるだけでよい (リセットは要らない)。 */
        if ((results[0] & FDC_ST0_NR) != 0) {
            dma_chan_mask(FDC_DMA_CHANNEL);
            dma_armed = 0;
            phase = "nr";
            break;
        }
    }

    /* 実行フェーズで落ちたまま戻らない。DMA ch2 を閉じる。
     * 開いたままだと、次の WRITE の準備後に遅れた旧 READ の DMA が
     * dma_buffer を上書きする。送信途中の失敗も phase でなく dma_armed で拾う。 */
    if (dma_armed) fdc_abort_transfer();
    fdc_forget_cyl(drv);

    fdc_report_fail(write ? "write" : "read", drv, cyl, head, sect, phase,
                    have_results, results, s_last_seek_st0);
    return write_protected ? FDC_RC_WRITE_PROTECTED : -1;
}
