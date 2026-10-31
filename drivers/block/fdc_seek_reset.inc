/* FDC のシーク完了待ち、リセット・回復と RECALIBRATE/SEEK。fdc.c から元の順序で取り込む。 */

/* ======================================================================== */
/*  シーク完了待ち (SEEK / RECALIBRATE 共通)                                */
/*                                                                          */
/*  **期限 (start + FDC_SEEK_TIMEOUT_TICKS) までループする。**              */
/*  IRQ 待ちをループの前に 1 回だけ置くと、別ドライブの通知 (例: drv1 の     */
/*  Ready 変化) で IRQ が上がったときに、それを読み捨てた直後の SIS が       */
/*  80h を返して **自ドライブの正常なシークを途中で失敗にする**。           */
/*  なので:                                                                 */
/*    - 別ドライブの通知なら捨てて待ちを続ける                              */
/*    - 80h (pending 無し) も期限内なら待ちを続ける                         */
/*    - 自ドライブの結果が出たらそれを返す                                  */
/*  上限は**時間で**縛る (tick_count は単調に進むので無限ループにならない)。 */
/*                                                                          */
/*  期限を過ぎたら最後にもう一度 SIS を出す — 実機では遅れて上がった INT の  */
/*  エッジを PIC が取りこぼすことがあり、そのとき ST0 の SE が立っていれば   */
/*  シーク自体は終わっている。80h なら本当に終わっていないので              */
/*  FDC_SEEK_PENDING を返す。                                               */
/*                                                                          */
/*  want_cyl >= 0 で PCN を照合する。RECALIBRATE は -1 を渡す。             */
/*  戻り値は fdc_classify_seek_end() の FDC_SEEK_*。                        */
/*  診断用に最後に見た ST0 を s_last_seek_st0 へ残す。                      */
/* ======================================================================== */
/* 完了待ちの SIS が返した結果が「自ドライブのシーク完了」ではないか。
 * 別ドライブの通知と、自ドライブの Ready 変化 (IC=11b で SE 無し) は
 * どちらも読み捨てて次を読む。 */
static int fdc_sis_is_foreign(u8 st0, u8 want_ds)
{
    if ((u8)(st0 & FDC_ST0_DS_MASK) != want_ds) return 1;
    if ((u8)(st0 & FDC_ST0_IC_MASK) == FDC_ST0_IC_RDYCHG
        && (st0 & FDC_ST0_SE) == 0) return 1;
    return 0;
}

static int fdc_wait_seek_end(int drv, u8 *st0, u8 *cyl, int want_cyl)
{
    u32 start = tick_count;
    u8 want_ds = (u8)(drv & FDC_ST0_DS_MASK);
    int i;
    int more = 0;   /* 前の周で 80h を見ずに打ち切った = まだ pending が有り得る */

    *st0 = 0;
    *cyl = 0;

    for (;;) {
        u32 elapsed = tick_count - start;

        if (elapsed >= FDC_SEEK_TIMEOUT_TICKS) break;   /* 期限切れ */
        /* 残り時間だけ待つ。来なければ期限切れとして下の救済へ。
         * **前の周が 80h を見ないまま件数の上限で止まっていたら、エッジを
         * 待たずに読み続ける** — INT 線は上がったままで次のエッジは来ない
         * (ラリー 2 の Fable)。µPD765A が溜める通知はドライブごとに 1 件
         * (4 件) なので実際には 1 周で尽きるが、上限を件数に頼らず時間
         * (FDC_SEEK_TIMEOUT_TICKS) で縛る。 */
        if (!more) {
            if (fdc_wait_irq(FDC_SEEK_TIMEOUT_TICKS - elapsed) != 0) break;
        }
        more = 0;

        /* **1 本のエッジで積まれている分を全部読む** (2026-09-24)。
         * µPD765A の INT 線は pending が尽きるまで上がったままで、エッジ
         * トリガの PIC には次のエッジが来ない。別ドライブの通知や Ready
         * 変化を 1 件読んだだけで待ちに戻ると、自分の完了が FIFO に
         * 残ったまま期限 (1.5 秒) まで空待ちし、救済の SIS で拾うことに
         * なる — NP21/W でフォントの読み込み中に CPU がここに居続けた
         * 形。80h (pending 無し) が出たら次のエッジを待つ。 */
        for (i = 0; i < FDC_SIS_DRAIN_MAX; i++) {
            if (fdc_sense_interrupt(st0, cyl) != 0) return FDC_SEEK_FAIL;
            s_last_seek_st0 = *st0;
            if (fdc_sis_result_bytes(*st0) < FDC_SIS_LEN_NORMAL) break;
            if (fdc_sis_is_foreign(*st0, want_ds)) {
                s_stats.sis_foreign++;
                continue;
            }
            return fdc_classify_seek_end(*st0, *cyl, want_cyl);
        }
        if (i == FDC_SIS_DRAIN_MAX) more = 1;
    }

    /* 期限切れ。エッジの取りこぼしだけなら救う。 */
    s_stats.seek_timeout++;
    for (i = 0; i < FDC_SIS_DRAIN_MAX; i++) {
        if (fdc_sense_interrupt(st0, cyl) != 0) return FDC_SEEK_FAIL;
        s_last_seek_st0 = *st0;
        if (fdc_sis_result_bytes(*st0) < FDC_SIS_LEN_NORMAL) {
            return FDC_SEEK_PENDING;
        }
        if (fdc_sis_is_foreign(*st0, want_ds)) {
            s_stats.sis_foreign++;
            continue;
        }
        return fdc_classify_seek_end(*st0, *cyl, want_cyl);
    }
    return FDC_SEEK_FAIL;
}

/* ======================================================================== */
/*  FDCリセット                                                             */
/* ======================================================================== */
static int fdc_reset(void)
{
    /* リセットの後は覚えているシリンダを信じない。 */
    fdc_forget_all();

    /* FDCをリセット */
    outp(FDC_CTRL, CTRL_RST);
    fdc_delay();
    fdc_delay();
    fdc_delay();
    fdc_delay();

    /* リセット解除 + Forced Ready + DMA有効 + モーターON。
     * FRY はリセット解除と同時に立てる — RDY が下りたままだと、
     * この直後の SPECIFY / RECALIBRATE が全部 NR で返る。 */
    fdc_irq_fired = 0;
    outp(FDC_CTRL, CTRL_FRY | CTRL_MTON | CTRL_DMAE);

    /* リセット完了IRQ待ち。来ない機種・エミュレータがあるので、
     * タイムアウトしても止めずに SIS の排水へ進む。 */
    (void)fdc_wait_irq(FDC_RESET_TIMEOUT_TICKS);

    /* リセット後は 4 ドライブ分の完了通知が溜まる。ST0 = 80h で尽きる。 */
    (void)fdc_drain_interrupts();

    /* Specifyコマンド: SRT=8ms, HLT=10ms, HUT=max, DMA有効 (値は fdc.h)。 */
    if (fdc_send_byte(FDC_CMD_SPECIFY) != 0) return -1;
    if (fdc_send_byte(FDC_SPECIFY_SRT_HUT) != 0) return -1;
    if (fdc_send_byte(FDC_SPECIFY_HLT_DMA) != 0) return -1;

    return 0;
}

/* ======================================================================== */
/*  実行フェーズごと打ち切る (転送の後始末)                                 */
/*                                                                          */
/*  **DMA ch2 を先にマスクするのが肝**。READ が実行フェーズ中にタイム       */
/*  アウトしたまま -1 を返すと、ch2 はアンマスクのまま残る。次に WRITE が    */
/*  dma_buffer へ書くべき中身を写した後で、遅れて来た旧 READ の DMA が       */
/*  **そこを上書きし得る**。WRITE は最初の SEEK が失敗しても fdc_recover で  */
/*  先へ進むので、気づかないまま**旧データをディスクに書いて成功を返す**。  */
/*                                                                          */
/*  IRQ は待たない — 最短で戻す。積み残しは SIS の排水で回収する。          */
/* ======================================================================== */
static void fdc_abort_transfer(void)
{
    /* 1. まず DMA を止める。以後 dma_setup が「マスク→設定→アンマスク」
     *    するので、ここではマスクしたままにしておいてよい。 */
    dma_chan_mask(FDC_DMA_CHANNEL);

    /* 2. FDC をリセットして実行フェーズを畳む。覚えているシリンダも捨てる。 */
    fdc_forget_all();
    outp(FDC_CTRL, CTRL_RST);
    fdc_delay();
    fdc_delay();
    fdc_delay();
    fdc_delay();
    fdc_irq_fired = 0;
    outp(FDC_CTRL, CTRL_FRY | CTRL_MTON | CTRL_DMAE);

    /* 3. Specify を入れ直す。µPD765A の資料では SRT/HUT/HLT は RESET で
     *    保持されるが、既知の値へ戻しておくほうが状態を追いやすい。 */
    (void)fdc_send_byte(FDC_CMD_SPECIFY);
    (void)fdc_send_byte(FDC_SPECIFY_SRT_HUT);
    (void)fdc_send_byte(FDC_SPECIFY_HLT_DMA);

    /* 4. 積み残しの割り込みを回収する (IRQ 待ちはしない)。 */
    (void)fdc_drain_interrupts();
}

/* ======================================================================== */
/*  Recalibrate (ヘッドをシリンダ0に移動)                                   */
/* ======================================================================== */
/* 成功条件は「SE が立ち、EC が立っていない」こと。PCN は照合しない。
 * 戻り値: 0 = 成功 / -1 = コマンド送信失敗 / -2 = 未完了 (ST0 = 80h)
 *         -3 = EC が取れなかった / -4 = その他の失敗
 *         FDC_RC_NOT_READY (-5) = NR (媒体もドライブも無い。回復しない)
 * 最後に見た ST0 を *out_st0 に返す (診断用。NULL 可)。 */
/* ヘッドの整定待ち (SEEK / RECALIBRATE の後、READ / WRITE の前)。
 * **RECALIBRATE の後にも要る** (2026-09-24 ラリー 2、Codex と Fable):
 * RECALIBRATE が通ると 0 を覚えるので、続く C=0 の読みは SEEK を省く。
 * 整定を SEEK にだけ任せていると、回復 (リセット + RECALIBRATE) の直後の
 * C=0 の READ が整定前に出る。 */
#define FDC_HEAD_SETTLE_TICKS  2    /* 約 15ms を tick (10ms) に切り上げ */
static void fdc_head_settle(void)
{
    u32 start = tick_count;
    while ((tick_count - start) < FDC_HEAD_SETTLE_TICKS) { /* 何もしない */ }
}

static int fdc_recalibrate_st0(int drv, u8 *out_st0)
{
    u8 st0, cyl;
    int rc, attempt;

    st0 = 0;
    rc = FDC_SEEK_FAIL;

    /* ヘッドが動く。通るまでは知らないことにする。 */
    fdc_note_drive(drv);
    fdc_forget_cyl(drv);

    for (attempt = 0; attempt < FDC_RECAL_ATTEMPTS; attempt++) {
        /* 前のコマンドの取りこぼしを片付けてから出す。 */
        (void)fdc_drain_interrupts();

        fdc_irq_fired = 0;
        s_stats.recal_issued++;
        if (fdc_send_byte(FDC_CMD_RECALIBRATE) != 0) {
            if (out_st0) *out_st0 = st0;
            return -1;
        }
        if (fdc_send_byte((u8)drv) != 0) {
            if (out_st0) *out_st0 = st0;
            return -1;
        }

        rc = fdc_wait_seek_end(drv, &st0, &cyl, -1);
        if (rc == FDC_SEEK_OK) {
            if (out_st0) *out_st0 = st0;
            /* 整定してから 0 を覚える (上の fdc_head_settle の注記)。 */
            fdc_head_settle();
            fdc_note_cyl(drv, 0);
            return 0;
        }
        if (rc != FDC_SEEK_RETRY_EC) break;
        /* EC = 77 ステップでトラック 0 に届かなかった。80 シリンダ媒体で
         * ヘッドが 77 より奥に居ると起きる。もう一度出せば残りを踏む。 */
    }

    if (out_st0) *out_st0 = st0;
    if (rc == FDC_SEEK_NOT_READY) return FDC_RC_NOT_READY;
    if (rc == FDC_SEEK_PENDING)  return FDC_SEEK_ERR_PENDING;
    if (rc == FDC_SEEK_RETRY_EC) return FDC_SEEK_ERR_RETRY;
    return FDC_SEEK_ERR_IO;
}

static int fdc_recalibrate(int drv)
{
    return fdc_recalibrate_st0(drv, (u8 *)0);
}

/* ======================================================================== */
/*  リトライのあいだの回復                                                  */
/*                                                                          */
/*  DMA マスク → FDC リセット (0x94 bit7) → Specify → SIS 排水 →           */
/*  recalibrate。排水と recalibrate は fdc_recalibrate_st0() の中で行う。   */
/*  1 回目の転送が失敗した時点で INT 線が上がりっぱなしになっている可能性   */
/*  があり、そのまま 2 回目を出しても必ず同じ形で失敗する。                 */
/*                                                                          */
/*  **先頭で DMA ch2 をマスクする** — 直前の試行が実行フェーズで落ちて      */
/*  いると ch2 がアンマスクのまま残っている。これで、最終失敗が seek        */
/*  フェーズ (dma_setup を通らない) で起きた場合も ch2 は必ず閉じている。   */
/* ======================================================================== */
static int fdc_recover(int drv)
{
    int rc;

    dma_chan_mask(FDC_DMA_CHANNEL);
    rc = fdc_reset();
    if (rc != 0) return rc;
    return fdc_recalibrate(drv);
}

/* ======================================================================== */
/*  Seek (指定シリンダに移動)                                               */
/* ======================================================================== */
static int fdc_seek(int drv, int cyl, int head)
{
    u8 st0, result_cyl;
    int rc;

    fdc_note_drive(drv);

    /* 未回収の割り込みを片付けてから出す (上の fdc_drain_interrupts の注記)。
     * シークを省くときも片付ける — 続く READ / WRITE DATA の完了も IRQ の
     * エッジで待つので、INT 線が上がったままだと同じ形で失敗する。
     * 排水は pending 無しなら SIS 1 回 (1 バイト応答) で終わる。 */
    (void)fdc_drain_interrupts();

    /* 同じシリンダに居ると分かっていれば SEEK も整定待ちも出さない。
     * ヘッド (表裏) の切り替えは READ / WRITE DATA の HD で選ぶので、
     * シリンダが同じなら SEEK は要らない。 */
    if (drv >= 0 && drv < FDC_MAX_DRIVES && s_known_cyl[drv] == cyl) {
        s_stats.seek_skipped++;
        return 0;
    }
    fdc_forget_cyl(drv);

    fdc_irq_fired = 0;
    s_stats.seek_issued++;
    if (fdc_send_byte(FDC_CMD_SEEK) != 0) return -1;
    if (fdc_send_byte((u8)((head << FDC_HEAD_SHIFT) | drv)) != 0) return -1;
    if (fdc_send_byte((u8)cyl) != 0) return -1;

    /* 完了待ち。PCN が要求シリンダと一致することまで見る。 */
    rc = fdc_wait_seek_end(drv, &st0, &result_cyl, cyl);
    /* NR = 媒体もドライブも無い。**リトライも回復もしない** ので分けて返す。 */
    if (rc == FDC_SEEK_NOT_READY) return FDC_RC_NOT_READY;
    if (rc == FDC_SEEK_PENDING) return FDC_SEEK_ERR_PENDING;
    if (rc != FDC_SEEK_OK) return FDC_SEEK_ERR_IO;

    /* ヘッド安定待ち: 約15ms */
    fdc_head_settle();

    fdc_note_cyl(drv, cyl);
    return 0;
}

