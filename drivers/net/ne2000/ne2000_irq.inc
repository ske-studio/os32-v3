/* poll・共有 IRQ・タイマ補助。ne2000.c と同じ翻訳単位で使用する。 */
void ne2k_poll(unsigned int budget)
{
    if (nic.state == NE2K_STATE_OFF || nic.state == NE2K_STATE_STOPPED ||
        nic.state == NE2K_STATE_FAILED) return;
    unsigned int f = irq_save();
    (void)budget;
    if (nic.busy) nic.irq_pending = 1;
    else {
        ne2k_enter();
        mark_work();
        ne2k_leave();
    }
    irq_restore(f);
}

/* ======================================================================== */
/*  ne2k_irq_shared — 共有 IRQ の口 (票 TASK_HAL_WIRING §1-1「LGY-98 のアダプタ」)
 *
 *  kernel/irq/irq.c の表から呼ばれる。線は他の装置と共有され得るので、
 *  **自分の要因が無ければ必ず IRQ_NONE を返す** (受けたふりをすると
 *  相方の要因が残ったまま線が上がり続ける)。
 *
 *    IRQ_NONE     : 自分の要因ではない
 *    IRQ_HANDLED  : 要因を受け付け、IMR は開いている
 *    IRQ_DEFERRED : 自分の要因で、装置はマスク中。
 *                   協調処理 ne2k_bh が回収する。RUNNING 中は ACK 後に
 *                   IMR を開け、送信完了が次の bh を起こせるようにする。
 * ======================================================================== */
int ne2k_irq_shared(unsigned int irq, void *arg)
{
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

    ne2k_enter();
    mark_work();
    ne2k_leave();

    /* **判定は imr_written (実 IMR)**。imr_mask は「戻したい値」なので、
     * OVW 復旧待ちや FAILED で 0 を書いた直後でも非 0 のまま = 誤判定になる。 */
    return nic.imr_written == 0 ? IRQ_DEFERRED : IRQ_HANDLED;
}

void ne2k_irq_enable(void)
{
    if (nic.state == NE2K_STATE_OFF) return;
    nic.imr_mask = NE2K_IMR_MASK;
    nic.irq_on = 1;
    /* 既着分は印を立てて ACK し、leave が IMR を戻す。 */
    ne2k_poll(NE2K_IRQ_BUDGET);
}

/* 100Hz watchdog catches lost edges by checking CURR, without draining here.
 * Statistics count the frames subsequently drained by that cooperative pass. */
void ne2k_timer_tick(void)
{
    if (nic.state != NE2K_STATE_RUNNING && nic.state != NE2K_STATE_OVW_WAIT) return;
    ne2k_poll(NE2K_WATCHDOG_BUDGET);
    if (nic.need_rx & (NE2K_ISR_PRX | NE2K_ISR_RXE) || nic.rx_backlog)
        nic.need_rx |= NE2K_WORK_WATCHDOG;
}
