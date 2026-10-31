/* FDC の単発読み書き・トラック内まとめ読みと失敗診断。fdc.c から元の順序で取り込む。 */

/* ======================================================================== */
/*  最終失敗の 1 行 ([V4])                                                  */
/*                                                                          */
/*  リトライごとには出さない — 画面が流れて元の失敗が見えなくなる。        */
/*  リザルトフェーズまで届いていない (seek / cmd で落ちた) ときは           */
/*  results[] がまだ空なので、**最後の SEEK / RECALIBRATE の ST0** を出す。 */
/*  NR (媒体もドライブも無い) なら印を付ける — いちばん多い失敗なので       */
/*  「なぜ読めなかったか」が 1 行で分かるようにする。                      */
/* ======================================================================== */
static void fdc_report_fail(const char *op, int drv, int cyl, int head,
                            int sect, const char *phase, int have_results,
                            const u8 *results)
{
    u8 st0, st1, st2;
    const char *mark;

    if (!have_results) {
        st0 = s_last_seek_st0;
        st1 = 0;
        st2 = 0;
    } else {
        st0 = results[0];
        st1 = results[1];
        st2 = results[FDC_RESULT_ST2];
    }
    mark = (st0 & FDC_ST0_NR) ? " (NR)" : "";

    kprintf(KLOG_ATTR,
            "[fdc] %s fail drv=%d chs=%d/%d/%d phase=%s st0=%02x st1=%02x st2=%02x%s\n",
            op, drv, cyl, head, sect, phase, st0, st1, st2, mark);
}

/* ======================================================================== */
/*  セクタ読み込み (ジオメトリ指定版)                                        */
/* ======================================================================== */
int fdc_read_sector_geom(int drv, int cyl, int head, int sect,
                         const struct fdc_geom *g, void *buf)
{
    u8 results[FDC_RESULT_BYTES];
    int n, retry;
    u32 phys = fdc_dma_phys();
    u16 bps = g->bps;
    const char *phase = "seek";
    int have_results = 0;   /* results[] が埋まったか (診断の出し分け) */
    int dma_armed = 0;      /* DMA ch2 をアンマスクしたまま抜けていないか */

    kmemset(results, 0, sizeof(results));
    s_stats.single_reads++;

    for (retry = 0; retry < FDC_RW_RETRIES; retry++) {
        int seek_rc;

        /* 0. 前の試行が失敗している。**次を出す前に回復する** —
         *    リセットと recalibrate を挟まないと、上がりっぱなしの INT 線の
         *    まま同じ形で失敗し続ける。最後の失敗の後には呼ばない。
         *    fdc_recover は先頭で ch2 をマスクするので dma_armed も下りる。
         *    診断は**最後の試行だけ**を映すので results[] も消す。 */
        if (retry > 0) {
            s_stats.single_retries++;
            (void)fdc_recover(drv);
            dma_armed = 0;
            have_results = 0;
            kmemset(results, 0, sizeof(results));
        }

        /* 1. シーク */
        phase = "seek";
        seek_rc = fdc_seek(drv, cyl, head);
        /* NR = 媒体もドライブも無い。**リトライも回復もしない** —
         * 回復で直るものではないし、HDD 起動時の /fd0 サブマウント試行が
         * 空のドライブに対して毎回 3 回踏むと起動が数秒伸びる。 */
        if (seek_rc == FDC_RC_NOT_READY) break;
        if (seek_rc != 0) continue;

        /* 2. DMAセットアップ (FDC→メモリ = read)。
         *    ここで ch2 がアンマスクされる。以後どこで抜けても
         *    **閉じてからでないと戻れない** (下の fdc_abort_transfer)。
         *    **負が返ったら ch2 は閉じたままなのでコマンドを出さない** —
         *    出すと実行フェーズで固まってタイムアウトを待つだけになる。 */
        if (dma_setup(phys, bps, 0) != 0) { phase = "dma"; break; }
        dma_armed = 1;
        phase = "cmd";

        /* 3. Read Data コマンド送信 */
        fdc_irq_fired = 0;
        if (fdc_send_byte(FDC_OPT_MF | FDC_CMD_READ_DATA) != 0) continue;
        if (fdc_send_byte((u8)((head << FDC_HEAD_SHIFT) | drv)) != 0) continue;
        if (fdc_send_byte((u8)cyl) != 0) continue;      /* C */
        if (fdc_send_byte((u8)head) != 0) continue;     /* H */
        if (fdc_send_byte((u8)sect) != 0) continue;     /* R (1始まり) */
        if (fdc_send_byte(g->sec_n) != 0) continue;     /* N */
        if (fdc_send_byte((u8)sect) != 0) continue;     /* EOT (最終セクタ) */
        if (fdc_send_byte(g->gap3) != 0) continue;      /* GPL */
        if (fdc_send_byte(FDC_DTL_UNUSED) != 0) continue;     /* DTL */

        /* 4. IRQ待ち (データ転送完了) */
        phase = "irq";
        if (fdc_wait_irq(FDC_RW_TIMEOUT_TICKS) != 0) continue;

        /* 5. リザルト読み出し (7バイト) */
        phase = "result";
        n = fdc_read_results(results, FDC_RESULT_BYTES);
        have_results = (n > 0);
        if (n < FDC_RESULT_BYTES) continue;

        /* 6. エラーチェック: ST0のbit6-7が00なら成功 */
        if ((results[0] & FDC_ST0_IC_MASK) == FDC_ST0_IC_NORMAL) {
            /* DMAバッファからユーザーバッファにコピー */
            kmemcpy((u8 *)buf, dma_buffer, (u32)bps);
            return 0;
        }

        /* NR (媒体無し) は**回復 (リセット・RECALIBRATE) より前に**打ち切る
         * (ラリー 2 の Codex)。リザルトを読み終えたコマンドは実行フェーズを
         * 抜けているので、DMA を閉じるだけでよい (リセットは要らない)。 */
        if ((results[0] & FDC_ST0_NR) != 0) {
            dma_chan_mask(FDC_DMA_CHANNEL);
            dma_armed = 0;
            phase = "nr";
            break;
        }
    }

    /* 実行フェーズで落ちたまま戻らない。**DMA ch2 を閉じる** —
     * 開いたままだと、次の WRITE が dma_buffer を用意した後に遅れて来た
     * 旧 READ の DMA がそれを上書きし、古い内容が書かれてしまう。
     * 判断は phase の文字列ではなく dma_armed で行う — コマンド送信が
     * 途中で失敗した場合も ch2 は既に開いている。 */
    if (dma_armed) fdc_abort_transfer();
    fdc_forget_cyl(drv);

    fdc_report_fail("read", drv, cyl, head, sect, phase, have_results, results);
    return -1;
}

/* ======================================================================== */
/*  セクタ書き込み (ジオメトリ指定版)                                        */
/* ======================================================================== */
int fdc_write_sector_geom(int drv, int cyl, int head, int sect,
                          const struct fdc_geom *g, const void *buf)
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

    /* **書く前に世代を進める** — 先読み (drivers/block/fdc_track.c) がこの
     * セクタの古い中身を持っていても当てさせない。dev.c の fd0/fd1 や
     * KAPI の dev_blk_write はここを直に呼ぶので、diskio.c の破棄だけ
     * では足りない (レビューの指摘)。失敗しても進めたままでよい
     * (途中まで書かれたかもしれない)。 */
    fdc_bump_gen(drv);
    s_stats.writes++;

    /* ユーザーバッファからDMAバッファにコピー */
    kmemcpy(dma_buffer, (const u8 *)buf, (u32)bps);

    for (retry = 0; retry < FDC_RW_RETRIES; retry++) {
        int seek_rc;

        /* 0. 前の試行が失敗している。次を出す前に回復する (read 側と同じ)。 */
        if (retry > 0) {
            (void)fdc_recover(drv);
            dma_armed = 0;
            have_results = 0;
            kmemset(results, 0, sizeof(results));
        }

        /* 1. シーク */
        phase = "seek";
        seek_rc = fdc_seek(drv, cyl, head);
        if (seek_rc == FDC_RC_NOT_READY) break;   /* read 側と同じ理由 */
        if (seek_rc != 0) continue;

        /* 2. DMAセットアップ (メモリ→FDC = write)。read 側と同じ (ch2 が開く)。
         *    負なら ch2 は閉じたままなのでコマンドを出さない (read 側と同じ)。 */
        if (dma_setup(phys, bps, 1) != 0) { phase = "dma"; break; }
        dma_armed = 1;
        phase = "cmd";

        /* 3. Write Data コマンド送信 */
        fdc_irq_fired = 0;
        if (fdc_send_byte(FDC_OPT_MF | FDC_CMD_WRITE_DATA) != 0) continue;
        if (fdc_send_byte((u8)((head << FDC_HEAD_SHIFT) | drv)) != 0) continue;
        if (fdc_send_byte((u8)cyl) != 0) continue;      /* C */
        if (fdc_send_byte((u8)head) != 0) continue;     /* H */
        if (fdc_send_byte((u8)sect) != 0) continue;     /* R */
        if (fdc_send_byte(g->sec_n) != 0) continue;     /* N */
        if (fdc_send_byte((u8)sect) != 0) continue;     /* EOT */
        if (fdc_send_byte(g->gap3) != 0) continue;      /* GPL */
        if (fdc_send_byte(FDC_DTL_UNUSED) != 0) continue;     /* DTL */

        /* 4. IRQ待ち */
        phase = "irq";
        if (fdc_wait_irq(FDC_RW_TIMEOUT_TICKS) != 0) continue;

        /* 5. リザルト読み出し */
        phase = "result";
        n = fdc_read_results(results, FDC_RESULT_BYTES);
        have_results = (n > 0);
        if (n < FDC_RESULT_BYTES) continue;

        /* 6. エラーチェック */
        if ((results[0] & FDC_ST0_IC_MASK) == FDC_ST0_IC_NORMAL) {
            return 0;
        }

        if (results[1] & FDC_ST1_NW) {
            /* リザルトは読み終えた。NR と同じく DMA を閉じて抜ける。 */
            dma_chan_mask(FDC_DMA_CHANNEL);
            dma_armed = 0;
            write_protected = 1;
            break;
        }

        /* NR (媒体無し) は回復より前に打ち切る (read 側と同じ)。リザルトを
         * 読み終えているので DMA を閉じるだけでよい (リセットは要らない)。 */
        if ((results[0] & FDC_ST0_NR) != 0) {
            dma_chan_mask(FDC_DMA_CHANNEL);
            dma_armed = 0;
            phase = "nr";
            break;
        }
    }

    if (dma_armed) fdc_abort_transfer();
    fdc_forget_cyl(drv);

    fdc_report_fail("write", drv, cyl, head, sect, phase, have_results, results);
    return write_protected ? FDC_RC_WRITE_PROTECTED : -1;
}

/* ======================================================================== */
/*  同じトラックの複数セクタを 1 回の READ DATA で読む (2026-09-24)         */
/*                                                                          */
/*  EOT = sect + count - 1、DMA の転送長 = count × bps。µPD765A は R が     */
/*  EOT に達するか DMA の TC が来るまで次のセクタへ進む。長さを EOT と揃え  */
/*  てあるので、**最後のセクタの最後のバイトで TC と EOT が同時に来て正常   */
/*  終了する** — 単発 (R = EOT、長さ = 1 セクタ) と同じ組み立てで、実機で   */
/*  通っている形をセクタ数だけ伸ばしたもの。NP21/W も同じ (src/io/fdc.c の  */
/*  FDC_ReadData は FDCEVENT_NEXTDATA で `R++ == eot` まで readsector を     */
/*  繰り返し、fdc_dmafunc の DMAEXT_END で tc を立てる)。                    */
/*                                                                          */
/*  **MT (マルチトラック) は使わない。** Bible 2-9 の INT 1Bh READ DATA は   */
/*  「MT を指定し開始ヘッドが 0 なら同じシリンダのヘッド 1 も読める」とし、  */
/*  NP21/W も MT で H を 0→1 に進める。使えば 1 コマンドで 1 シリンダ       */
/*  (2HD 16KB) 読めるが、今回は入れない:                                    */
/*   - 受け皿が 1 トラック分 (9KB) で足りる (票の指示: 転送長 1 トラック以下)*/
/*   - 同資料は書き込みの MT を禁じている (µPD765 が正しく動かない)。読みで */
/*     実機の挙動を確かめた記録がまだ無い                                   */
/*   - 得はヘッド 1 の先頭セクタの待ち (最悪 1 回転) をシリンダごとに 1 回  */
/*     省くことだけで、1 セクタ 1 回転の今の損に比べて小さい                */
/*  実機で速度を測ってから別の段で入れる (票 TASK_FDC_REALHW の v3 の節)。  */
/*                                                                          */
/*  **リトライしない**。失敗したら DMA を閉じ、覚えているシリンダを捨てて   */
/*  -1 を返す。呼び手 (drivers/block/fdc_track.c) がその範囲を                    */
/*  fdc_read_sector_geom で 1 セクタずつ読み直す (そちらは従来どおり        */
/*  リトライ・回復・NR の早期終了を持つ)。                                  */
/* ======================================================================== */
int fdc_read_sectors(int drv, int cyl, int head, int sect, int count,
                     const struct fdc_geom *g, void *buf)
{
    u8 results[FDC_RESULT_BYTES];
    int n, eot;
    u32 bytes, timeout;
    u32 phys = fdc_dma_phys();
    const char *phase = "seek";
    int have_results = 0;
    int dma_armed = 0;
    int seek_failed = 0;

    if (g == 0 || count < 1 || sect < 1) return FDC_MULTI_ERR_RANGE;
    eot = sect + count - 1;
    if (eot > (int)g->spt) return FDC_MULTI_ERR_RANGE;          /* トラックをまたぐ (MT 無し) */
    bytes = (u32)count * (u32)g->bps;
    if (bytes > FDC_DMA_BUF_SIZE) return FDC_MULTI_ERR_RANGE;    /* 受け皿に入らない */

    kmemset(results, 0, sizeof(results));
    timeout = fdc_rw_timeout_ticks((int)g->spt, count, FDC_ROT_TICKS_WORST,
                                   FDC_FIND_ROTATIONS, FDC_HEAD_LOAD_TICKS,
                                   FDC_TIMEOUT_MARGIN, FDC_RW_TIMEOUT_TICKS);

    /* 1. シーク (同じシリンダなら省かれる)。NR (媒体無し) は失敗に
     *    数えない — 1 セクタずつの読みも NR で即座に終わる。 */
    {
        int seek_rc = fdc_seek(drv, cyl, head);
        if (seek_rc == FDC_RC_NOT_READY) {
            fdc_forget_cyl(drv);
            s_stats.multi_nr++;
            return FDC_MULTI_ERR_NOT_READY;
        }
        if (seek_rc != 0) { seek_failed = 1; goto fail; }
    }

    /* 2. DMA (FDC→メモリ)。長さはちょうど count セクタ = EOT と揃える。
     *    負なら ch2 は閉じたままなのでコマンドを出さない。 */
    if (dma_setup(phys, (u16)bytes, 0) != 0) { phase = "dma"; goto fail; }
    dma_armed = 1;
    phase = "cmd";

    /* 3. READ DATA (MT = 0) */
    fdc_irq_fired = 0;
    if (fdc_send_byte(FDC_OPT_MF | FDC_CMD_READ_DATA) != 0) goto fail;
    if (fdc_send_byte((u8)((head << FDC_HEAD_SHIFT) | drv)) != 0) goto fail;
    if (fdc_send_byte((u8)cyl) != 0) goto fail;      /* C */
    if (fdc_send_byte((u8)head) != 0) goto fail;     /* H */
    if (fdc_send_byte((u8)sect) != 0) goto fail;     /* R (1始まり) */
    if (fdc_send_byte(g->sec_n) != 0) goto fail;     /* N */
    if (fdc_send_byte((u8)eot) != 0) goto fail;      /* EOT */
    if (fdc_send_byte(g->gap3) != 0) goto fail;      /* GPL */
    if (fdc_send_byte(FDC_DTL_UNUSED) != 0) goto fail;  /* DTL */

    /* 4. 完了の IRQ。上限は機構の最悪値 (fdc.h の FDC_ROT_TICKS_WORST)。 */
    phase = "irq";
    if (fdc_wait_irq(timeout) != 0) goto fail;

    /* 5. リザルト */
    phase = "result";
    n = fdc_read_results(results, FDC_RESULT_BYTES);
    have_results = (n > 0);
    if (n < FDC_RESULT_BYTES) goto fail;
    /* リザルトを読み終えた = 実行フェーズは終わっている。DMA を閉じる。 */
    dma_chan_mask(FDC_DMA_CHANNEL);
    dma_armed = 0;
    if ((results[0] & FDC_ST0_IC_MASK) != FDC_ST0_IC_NORMAL) {
        /* NR は回復より前に打ち切る (単発と同じ。ラリー 2 の Codex)。 */
        if ((results[0] & FDC_ST0_NR) != 0) {
            fdc_forget_cyl(drv);
            s_stats.multi_nr++;
            return FDC_MULTI_ERR_NOT_READY;
        }
        goto fail;
    }

    kmemcpy((u8 *)buf, dma_buffer, bytes);
    s_stats.multi_ok++;
    return 0;

fail:
    /* 実行フェーズの途中なら DMA を閉じて FDC を畳み、**RECALIBRATE まで
     * 済ませる** (単発のリトライの間の fdc_recover と同じ形)。リセットの
     * 後の µPD765A の PCN はヘッドの実際の位置と合っている保証が無く、
     * そのまま次の単発が SEEK すると別のシリンダへ出て WC で 1 回ぶん
     * 無駄にする。覚えている値は fdc_abort_transfer が全部捨て、
     * RECALIBRATE が通れば 0 を覚え直す (ヘッドは実際に 0 に居る)。
     * DMA を積む前に落ちた (シークの失敗 / DMA を積めない) ときは
     * リセットを通らないので、ここで捨てる — 次の単発がシークし直す。 */
    if (dma_armed) {
        fdc_abort_transfer();
        (void)fdc_recalibrate(drv);
    } else if (seek_failed) {
        /* シークの失敗 (-2 未完了 / -4 異常) も回復を通す (ラリー 2 の
         * Fable)。INT 線やヘッドの位置が分からないまま単発へ渡さない。 */
        (void)fdc_recover(drv);
    } else {
        fdc_forget_cyl(drv);
    }
    s_stats.multi_fail++;
    if (s_stats.multi_fail <= FDC_MULTI_FAIL_REPORT_MAX) {
        kprintf(KLOG_ATTR, "[fdc] multi-read n=%d falls back to single\n", count);
        fdc_report_fail("read-multi", drv, cyl, head, sect, phase,
                        have_results, results);
    }
    return -1;
}

