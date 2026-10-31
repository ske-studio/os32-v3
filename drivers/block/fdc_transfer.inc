/* FDC の単発読み書き・トラック内まとめ読みと失敗診断。fdc.c から元の順序で取り込む。 */

#include "fdc_command.inc"

/* ======================================================================== */
/*  最終失敗の 1 行 ([V4])                                                  */
/*                                                                          */
/*  リトライごとには出さない — 画面が流れて元の失敗が見えなくなる。        */
/*  リザルトフェーズまで届いていない (seek / cmd で落ちた) ときは           */
/*  results[] がまだ空なので、**失敗時点の SEEK / RECALIBRATE の ST0** を出す。 */
/*  NR (媒体もドライブも無い) なら印を付ける — いちばん多い失敗なので       */
/*  「なぜ読めなかったか」が 1 行で分かるようにする。                      */
/* ======================================================================== */
static void fdc_report_fail(const char *op, int drv, int cyl, int head,
                            int sect, const char *phase, int have_results,
                            const u8 *results, u8 seek_st0)
{
    u8 st0, st1, st2;
    const char *mark;

    if (!have_results) {
        st0 = seek_st0;
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

#include "fdc_sector_transfer.inc"

/* ======================================================================== */
/*  セクタ読み込み (ジオメトリ指定版)                                        */
/* ======================================================================== */
int fdc_read_sector_geom(int drv, int cyl, int head, int sect,
                         const struct fdc_geom *g, void *buf)
{
    return fdc_transfer_sector_geom(drv, cyl, head, sect, g, buf, 0, 0);
}

/* ======================================================================== */
/*  セクタ書き込み (ジオメトリ指定版)                                        */
/* ======================================================================== */
int fdc_write_sector_geom(int drv, int cyl, int head, int sect,
                          const struct fdc_geom *g, const void *buf)
{
    return fdc_transfer_sector_geom(drv, cyl, head, sect, g, 0, buf, 1);
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
    u8 failed_seek_st0;

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
    /* 回復の RECALIBRATE が最後の ST0 を上書きする前に、原因を残す。 */
    failed_seek_st0 = s_last_seek_st0;
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
                        have_results, results, failed_seek_st0);
    }
    return -1;
}

