/* 受信リング・送受信・エラー復旧。ne2000.c と同じ翻訳単位で使用する。 */
/* ====================================================================== */
/*  受信リングの回収 (DP8390D §7)                                           */
/* ====================================================================== */

static u8 read_curr(void)
{
    u8 c;
    cr_page1_idle();
    c = rd(NE2K_P1_CURR);
    cr_page0_idle();
    nic.last_curr = c;
    return c;
}

/* CURR に合わせて再同期する (壊れたヘッダをたどらない) */
static void ring_resync(u8 curr)
{
    nic.st.resync++;
    nic.rx_next = curr;
    wr(NE2K_P0_BNRY, ne2k_ring_bnry_for(nic.rx_start, nic.rx_stop, curr));
}

/* リングから 1 フレーム分をホストキューへ。戻り値: 1 = 進んだ, 0 = 空, -1 = 再初期化要 */
static int rx_one(u8 curr)
{
    u8 raw[NE2K_RX_HDR_LEN];
    struct ne2k_rx_hdr h;
    u16 dlen = 0;
    int chk;
    int fcs_in_count = (nic.cfg.flags & NE2K_CFG_RX_COUNT_NO_FCS) ? 0 : 1;

    if (nic.rx_next == curr) return 0;

    if (dma_read((u16)nic.rx_next << 8, raw, NE2K_RX_HDR_LEN)) return -1;
    ne2k_ring_decode_hdr(raw, &h);
    chk = ne2k_ring_check(nic.rx_start, nic.rx_stop, nic.rx_next, &h, fcs_in_count, &dlen);

    if (chk == NE2K_RING_BAD_NEXT || chk == NE2K_RING_BAD_COUNT) {
        nic.st.bad_header++;
        if (++nic.bad_hdr_run >= NE2K_BAD_HDR_MAX) return -1;
        ring_resync(curr);
        return 0;
    }
    nic.bad_hdr_run = 0;

    if (chk == NE2K_RING_RX_ERROR) {
        nic.st.rx_err++;
    } else if (nic.rxq_count >= NE2K_RXQ_SLOTS) {
        nic.st.rx_dropped++;
    } else {
        u8 *dst = nic.rxq_buf[nic.rxq_tail];
        u16 addr = (u16)(((u16)nic.rx_next << 8) + NE2K_RX_HDR_LEN);
        u16 first = ne2k_ring_first_chunk(nic.rx_stop, addr, dlen);
        if (dma_read(addr, dst, first)) return -1;
        if (first < dlen) {
            if (dma_read((u16)nic.rx_start << 8, dst + first, (u16)(dlen - first))) return -1;
        }
        nic.rxq_len[nic.rxq_tail] = dlen;
        {
            unsigned int f = irq_save();
            nic.rxq_tail = (u8)((nic.rxq_tail + 1) % NE2K_RXQ_SLOTS);
            nic.rxq_count++;
            irq_restore(f);
        }
        nic.st.rx_frames++;
        nic.st.rx_bytes += dlen;
    }

    /* リングで消費したページ数を実測に足す (§2-3、リンク層の credit cost 用) */
    {
        unsigned int size = (unsigned int)(nic.rx_stop - nic.rx_start);
        if (size) nic.st.rx_pages_total +=
            ((unsigned int)h.next + size - nic.rx_next) % size;
    }
    /* データ回収後に next を進め、BNRY はその 1 ページ前 */
    nic.rx_next = h.next;
    wr(NE2K_P0_BNRY, ne2k_ring_bnry_for(nic.rx_start, nic.rx_stop, nic.rx_next));
    return 1;
}

/* budget フレームまで回収。戻り値 -1 = 再初期化要、1 = 予算を使い切り未回収が残る、0 = 空 */
static int rx_drain(unsigned int budget)
{
    while (budget > 0) {
        u8 curr = read_curr();
        int r = rx_one(curr);
        if (r < 0) return -1;
        if (r == 0) return 0;
        budget--;
    }
    return (read_curr() != nic.rx_next) ? 1 : 0;
}

/* ====================================================================== */
/*  OVW 復旧 (DP8390D §7 の手順を状態機械に分けたもの)                       */
/* ====================================================================== */

static void ovw_begin(u8 isr)
{
    nic.st.overwrite++;
    nic.ovw_txp_was = (u8)((rd(NE2K_CR) & NE2K_CR_TXP) ? 1 : 0);
    nic.ovw_tx_done = (u8)((isr & (NE2K_ISR_PTX | NE2K_ISR_TXE)) ? 1 : 0);
    wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
    ne2k_imr_sync(0);                           /* 復旧完了まで NIC 割り込みは止める */
    nic.ovw_tick = tick_count;
    nic.ovw_used_delay = 0;
    nic.state = NE2K_STATE_OVW_WAIT;
}

/* 1.6ms 以上経ったら復旧を完了する。IF=0 なら校正済み delay で待つ。 */
static void ovw_continue(void)
{
    int resend;

    if (eflags_if() || nic.in_irq) {
        /* tick は IF=1 の foreground でもタイマ文脈 (tick を数えた直後) でも進む */
        if ((u32)(tick_count - nic.ovw_tick) < NE2K_OVW_WAIT_TICKS) return;
    } else if (!nic.ovw_used_delay) {
        cpu_delay_us(NE2K_OVW_WAIT_US);
        nic.ovw_used_delay = 1;
    }

    wr(NE2K_P0_RBCR0, 0);
    wr(NE2K_P0_RBCR1, 0);
    resend = nic.ovw_txp_was && !nic.ovw_tx_done;
    wr(NE2K_P0_TCR, NE2K_TCR_LOOP_INT);
    nic.state = NE2K_STATE_RUNNING;
    cr_page0_idle();                                    /* STA, loopback 中 */
    if (rx_drain(NE2K_OVW_DRAIN_BUDGET) < 0) {
        reinit_or_fail();
        return;
    }
    wr(NE2K_P0_ISR, NE2K_ISR_OVW);
    wr(NE2K_P0_TCR, (nic.cfg.flags & NE2K_CFG_LOOPBACK) ? NE2K_TCR_LOOP_INT : NE2K_TCR_NORMAL);
    if (resend) {
        /* TPSR / TBCR は残っているので送信だけ再開する */
        wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STA | NE2K_CR_TXP);
        nic.tx_start_tick = tick_count;
    } else {
        nic.tx_busy = 0;
    }
}

/* ====================================================================== */
/*  送信完了・カウンタ・タイムアウト                                          */
/* ====================================================================== */

static void tx_complete(u8 isr)
{
    u8 tsr = rd(NE2K_P0_TSR);
    if ((isr & NE2K_ISR_TXE) || !(tsr & NE2K_TSR_PTX)) {
        nic.st.tx_err++;
    } else {
        nic.st.tx_ok++;
    }
    if (tsr & NE2K_TSR_COL) nic.st.tx_collisions++;
    if (tsr & NE2K_TSR_ABT) nic.st.tx_aborted++;
    if (tsr & NE2K_TSR_FU)  nic.st.tx_underrun++;
    wr(NE2K_P0_ISR, NE2K_ISR_PTX | NE2K_ISR_TXE);
    nic.tx_busy = 0;
    nic.tx_reinit_run = 0;
}

static void read_counters(void)
{
    nic.st.rx_align  += rd(NE2K_P0_CNTR0);
    nic.st.rx_crc    += rd(NE2K_P0_CNTR1);
    nic.st.rx_missed += rd(NE2K_P0_CNTR2);
    wr(NE2K_P0_ISR, NE2K_ISR_CNT);
}

/* 送信タイムアウト: tick は IF=1 の文脈で進む。IF=0 の診断ループでは
 * TXP が消えないことを poll 回数ではなく呼び出し側の期限で扱う。 */
static void tx_check_timeout(void)
{
    if (!nic.tx_busy) return;
    if (!(rd(NE2K_CR) & NE2K_CR_TXP)) {
        /* 完了しているが ISR を見る前だった */
        tx_complete(rd(NE2K_P0_ISR));
        return;
    }
    if (!eflags_if() && !nic.in_irq) return;    /* IF=0 の診断ループでは tick が進まない */
    if ((u32)(tick_count - nic.tx_start_tick) >= NE2K_TX_TIMEOUT_TICKS) {
        nic.st.tx_timeouts++;
        reinit_or_fail();
    }
}

/* NIC を触る共通の 1 サイクル。呼び出し側が busy を握っていること。 */
static void service(unsigned int budget)
{
    u8 isr;

    if (nic.state == NE2K_STATE_OVW_WAIT) {
        ovw_continue();
        return;
    }
    if (nic.state != NE2K_STATE_RUNNING) return;

    isr = rd(NE2K_P0_ISR);
    if (isr & NE2K_ISR_OVW) {
        ovw_begin(isr);
        return;
    }
    if (isr & (NE2K_ISR_PTX | NE2K_ISR_TXE)) tx_complete(isr);
    if (isr & (NE2K_ISR_PRX | NE2K_ISR_RXE)) {
        if (isr & NE2K_ISR_RXE) nic.st.rx_err++;
        wr(NE2K_P0_ISR, NE2K_ISR_PRX | NE2K_ISR_RXE);
    }
    if (isr & NE2K_ISR_CNT) read_counters();
    if (isr & NE2K_ISR_RDC) wr(NE2K_P0_ISR, NE2K_ISR_RDC);

    /* リングは ISR に関係なく CURR で判定する (ACK 前後の到着を取りこぼさない) */
    {
        int r = rx_drain(budget);
        if (r < 0) {
            reinit_or_fail();
            return;
        }
        if (r == 1 && !nic.rx_backlog) nic.st.rx_backlog_events++;
        nic.rx_backlog = (u8)(r == 1);
    }
    tx_check_timeout();
}

/* ====================================================================== */
/*  公開 API                                                               */
/* ====================================================================== */

int ne2k_send(const void *frame, unsigned int length)
{
    u16 padded;
    int rc;

    if (!frame) return NE2K_ERR_INVAL;
    if (length < NE2K_ETH_HDR_LEN || length > NE2K_ETH_MAX_LEN) return NE2K_ERR_INVAL;
    if (nic.state == NE2K_STATE_OVW_WAIT) return NE2K_ERR_BUSY;
    if (nic.state != NE2K_STATE_RUNNING) return NE2K_ERR_STOPPED;

    ne2k_enter();
    if (nic.tx_busy) {
        service(0);                             /* 完了済みなら回収 */
        if (nic.tx_busy || nic.state != NE2K_STATE_RUNNING) {
            ne2k_leave();
            return (nic.state == NE2K_STATE_RUNNING) ? NE2K_ERR_BUSY : NE2K_ERR_STOPPED;
        }
    }

    /* 呼び出し元長 (length)、padding 後 (padded)、DMA の偶数丸めは dma_write 内で別 */
    kmemcpy(nic.tx_buf, frame, length);
    padded = (u16)length;
    if (padded < NE2K_ETH_MIN_LEN) {
        kmemset(nic.tx_buf + padded, 0, NE2K_ETH_MIN_LEN - padded);
        padded = NE2K_ETH_MIN_LEN;
    }
    nic.tx_buf[padded] = 0;                     /* 奇数長の最終 word 上位 */

    rc = dma_write((u16)nic.tx_page << 8, nic.tx_buf, padded);
    if (rc) {
        ne2k_leave();
        return NE2K_ERR_IO;
    }
    wr(NE2K_P0_TPSR, nic.tx_page);
    wr(NE2K_P0_TBCR0, (u8)(padded & 0xFF));
    wr(NE2K_P0_TBCR1, (u8)(padded >> 8));
    wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STA | NE2K_CR_TXP);
    nic.tx_busy = 1;
    nic.tx_start_tick = tick_count;
    nic.st.tx_accepted++;
    nic.st.tx_bytes += length;
    ne2k_leave();
    return NE2K_OK;
}

int ne2k_recv(void *frame, unsigned int capacity, unsigned int *length)
{
    u16 len;
    unsigned int f;

    if (!frame || !length) return NE2K_ERR_INVAL;
    if (nic.state == NE2K_STATE_OFF) return NE2K_ERR_STOPPED;
    if (nic.rxq_count == 0) return NE2K_ERR_AGAIN;

    len = nic.rxq_len[nic.rxq_head];
    *length = len;
    if (capacity < len) return NE2K_ERR_NOSPACE;
    kmemcpy(frame, nic.rxq_buf[nic.rxq_head], len);
    f = irq_save();
    nic.rxq_head = (u8)((nic.rxq_head + 1) % NE2K_RXQ_SLOTS);
    nic.rxq_count--;
    irq_restore(f);
    return NE2K_OK;
}

