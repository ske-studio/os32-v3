/* poll・共有 IRQ・タイマ補助。ne2000.c と同じ翻訳単位で使用する。 */
void ne2k_poll(unsigned int budget)
{
    if (nic.state == NE2K_STATE_OFF || nic.state == NE2K_STATE_STOPPED ||
        nic.state == NE2K_STATE_FAILED) return;
    ne2k_enter();
    service(budget);
    ne2k_leave();
}

/* ======================================================================== */
/*  ne2k_irq_shared — 共有 IRQ の口 (票 TASK_HAL_WIRING §1-1「LGY-98 のアダプタ」)
 *
 *  kernel/irq/irq.c の表から呼ばれる。線は他の装置と共有され得るので、
 *  **自分の要因が無ければ必ず IRQ_NONE を返す** (受けたふりをすると
 *  相方の要因が残ったまま線が上がり続ける)。
 *
 *    IRQ_NONE     : 自分の要因ではない
 *    IRQ_HANDLED  : 要因を落とし、残件も無い
 *    IRQ_DEFERRED : 自分の要因だが残件がある。**装置はマスクしてある** —
 *                   回収は ne2k_timer_tick (M4 の受信ウォッチドッグ。
 *                   契約が要求する「定期回収」そのもの) が行い、
 *                   回収し終えたら ne2k_imr_sync でマスクを外す。
 * ======================================================================== */
int ne2k_irq_shared(unsigned int irq, void *arg)
{
    int pass;

    (void)irq;
    (void)arg;

    nic.st.irq_count++;
    if (nic.busy) {
        /* busy 中の入口は NIC レジスタを変更しない (foreground のページ切替・
         * remote DMA と衝突する)。leave() が irq_pending を見て処理する。 */
        nic.irq_pending = 1;
        nic.st.irq_deferred++;
        return IRQ_DEFERRED;
    }
    /* 装置が止まっている状態 (OFF / STOPPED / FAILED) では IMR = 0 で、
     * この線を上げているのは自分ではない。**票からの逸脱** (票は (b) の
     * ISR 検査だけを書いている): ここを通さないと FAILED の NIC が
     * IRQ_DEFERRED を返し続け、tick 回収を持たない状態 (ne2k_timer_tick は
     * 非 RUNNING/OVW_WAIT で即戻る) と組み合わさって残件が永久に残る。 */
    if (nic.state != NE2K_STATE_RUNNING && nic.state != NE2K_STATE_OVW_WAIT) {
        return IRQ_NONE;
    }
    /* (b) ISR に要因が無く、リングにも未回収が無ければ自分ではない。 */
    if (!(rd(NE2K_P0_ISR) & NE2K_IMR_MASK) && !nic.rx_backlog) return IRQ_NONE;

    nic.busy = 1;
    nic.in_irq = 1;
    for (pass = 0; pass < NE2K_IRQ_RECHECK; pass++) {
        service(NE2K_IRQ_BUDGET);
        cr_page0_idle();
        if (nic.rx_backlog || nic.state != NE2K_STATE_RUNNING) break;
        /* ACK 後に到着した分。NP21/W は 2ms 以内の再アサートを抑えるので、
         * ここで見ないと次の IRQ まで取り残す (実機でも安全側)。 */
        if (!(rd(NE2K_P0_ISR) & NE2K_IMR_MASK) && read_curr() == nic.rx_next) break;
        nic.st.irq_rechecks++;
    }
    /* 予算超過なら IMR をマスクしたまま返し、tick / poll が続きを回収して戻す。
     * **shadow と実 IMR は必ず一緒に動く** (1-6 / 往復 7 B3)。 */
    ne2k_imr_sync((nic.irq_on && !nic.rx_backlog &&
                   nic.state == NE2K_STATE_RUNNING) ? nic.imr_mask : 0);
    nic.in_irq = 0;
    nic.busy = 0;

    /* **判定は imr_written (実 IMR)**。imr_mask は「戻したい値」なので、
     * OVW 復旧待ちや FAILED で 0 を書いた直後でも非 0 のまま = 誤判定になる。 */
    return (nic.rx_backlog || nic.imr_written == 0) ? IRQ_DEFERRED : IRQ_HANDLED;
}

void ne2k_irq_enable(void)
{
    if (nic.state == NE2K_STATE_OFF) return;
    nic.imr_mask = NE2K_IMR_MASK;
    nic.irq_on = 1;
    /* 有効化前に届いていた分を回収してから IMR を入れる (poll の leave が行う) */
    ne2k_poll(NE2K_IRQ_BUDGET);
}

/* 100Hz の受信ウォッチドッグ (M4)。NIC IRQ を取りこぼしても (NP21/W の 2ms
 * 再アサート抑制やエッジ喪失、リング飽和後の停止) ここで CURR を見て回収するので、
 * 「フレームがリングに残ったまま二度と IRQ が来ない」wedge を防ぐ。健全時は CURR ==
 * rx_next で 1 回の CURR 読みだけで戻る (安価)。溢れそのものはリンク層の Credit で
 * 防ぐ ([LINK_PLAN.md])。ここは「溢れても・取りこぼしても壊れない」安全網。 */
void ne2k_timer_tick(void)
{
    u32 before;
    if (nic.busy) return;                       /* foreground 操作中は延期 (再入しない) */
    if (nic.state != NE2K_STATE_RUNNING && nic.state != NE2K_STATE_OVW_WAIT) return;
    nic.in_irq = 1;
    before = nic.st.rx_frames + nic.st.rx_dropped;
    ne2k_poll(nic.rx_backlog ? NE2K_IRQ_BUDGET : NE2K_WATCHDOG_BUDGET);
    /* この tick で回収できたフレーム = IRQ が取りこぼしていた分 (健全時は 0)。 */
    nic.st.watchdog_frames += (nic.st.rx_frames + nic.st.rx_dropped) - before;
    if (nic.st.rx_frames + nic.st.rx_dropped != before) nic.st.watchdog_hits++;
    nic.in_irq = 0;
}

