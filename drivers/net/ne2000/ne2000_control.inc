/* 初期化・停止・再初期化。ne2000.c と同じ翻訳単位で使用する。 */
/* ====================================================================== */
/*  初期化 (DP8390D §11)                                                    */
/* ====================================================================== */

static void program_ring(void)
{
    int i;
    u8 rcr;

    wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
    wr(NE2K_P0_DCR, NE2K_DCR_DEFAULT);
    wr(NE2K_P0_RBCR0, 0);
    wr(NE2K_P0_RBCR1, 0);
    wr(NE2K_P0_RCR, NE2K_RCR_MON);
    wr(NE2K_P0_TCR, NE2K_TCR_LOOP_INT);
    wr(NE2K_P0_TPSR, nic.tx_page);
    wr(NE2K_P0_PSTART, nic.rx_start);
    wr(NE2K_P0_PSTOP, nic.rx_stop);
    wr(NE2K_P0_BNRY, nic.rx_start);
    wr(NE2K_P0_ISR, NE2K_ISR_ALL);
    ne2k_imr_sync(0);

    wr(NE2K_CR, NE2K_CR_PAGE1 | NE2K_CR_RD2 | NE2K_CR_STP);
    for (i = 0; i < NE2K_ETH_ADDR_LEN; i++) wr((u8)(NE2K_P1_PAR0 + i), nic.mac[i]);
    for (i = 0; i < NE2K_MAR_BYTES; i++) wr((u8)(NE2K_P1_MAR0 + i), 0);
    wr(NE2K_P1_CURR, (u8)(nic.rx_start + 1));
    nic.rx_next = (u8)(nic.rx_start + 1);

    wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
    nic.state = NE2K_STATE_RUNNING;
    cr_page0_idle();                                    /* STA */
    wr(NE2K_P0_TCR, (nic.cfg.flags & NE2K_CFG_LOOPBACK) ? NE2K_TCR_LOOP_INT : NE2K_TCR_NORMAL);
    rcr = NE2K_RCR_AB;
    if (nic.cfg.flags & NE2K_CFG_PROMISC) rcr |= NE2K_RCR_PRO;
    wr(NE2K_P0_RCR, rcr);
    wr(NE2K_P0_ISR, NE2K_ISR_ALL);
    /* **ここで IMR を戻さない** (往復 6 の R1)。再初期化は busy = 1 のまま
     * 走るので、ここで開けると「busy = 装置マスク済み」という DEFERRED の
     * 契約が破れる。復帰は呼び手の最終出口 (leave / アダプタの出口) が
     * ne2k_imr_sync で行う。IMR はこの関数を抜けた時点で 0。 */
}

static void reset_soft_state(void)
{
    nic.tx_busy = 0;
    nic.bad_hdr_run = 0;
    nic.rxq_head = nic.rxq_tail = nic.rxq_count = 0;
    nic.irq_pending = 0;
    nic.rx_backlog = 0;
}

/* 停止状態からの再構築。統計は保持する。 */
static int bring_up(void)
{
    int rc;

    nic.state = NE2K_STATE_STOPPED;
    rc = hw_reset();
    if (rc) return rc;
    rc = presence_check();
    if (rc) return rc;

    /* probe 中は STA + 内部 loopback + monitor で受信も送信も外に出さない */
    wr(NE2K_P0_RCR, NE2K_RCR_MON);
    wr(NE2K_P0_TCR, NE2K_TCR_LOOP_INT);
    rc = read_prom();
    if (rc) return rc;
    rc = probe_ram();
    if (rc) return rc;

    nic.tx_page  = nic.cfg.ram_page_first;
    nic.rx_start = (u8)(nic.cfg.ram_page_first + NE2K_TX_PAGES);
    nic.rx_stop  = (u8)(nic.cfg.ram_page_first + nic.ram_pages);

    if (nic.cfg.flags & NE2K_CFG_DIAG_RAM) diag_ram_test();
    if (nic.cfg.flags & NE2K_CFG_DIAG_DMA) diag_dma_test();

    reset_soft_state();
    program_ring();
    return NE2K_OK;
}

int ne2k_init(const struct ne2k_config *config)
{
    int rc;

    if (!config) return NE2K_ERR_INVAL;
    if (config->reg_base == 0 || config->data_port == 0) return NE2K_ERR_INVAL;
    if (config->ram_page_first == 0 || config->ram_pages_max < NE2K_RAM_PAGES_16K) return NE2K_ERR_INVAL;

    if (tick_register(PC98_TICK_NE2K, ne2k_timer_tick) != 0) return NE2K_ERR_IO;

    kmemset(&nic, 0, sizeof(nic));
    nic.cfg = *config;

    rc = bring_up();
    if (rc) {
        /* 失敗したら NIC を止めて OS 起動は続けさせる */
        nic.state = NE2K_STATE_FAILED;
        wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
        ne2k_imr_sync(0);
        return rc;
    }
    return NE2K_OK;
}

void ne2k_stop(void)
{
    if (nic.state == NE2K_STATE_OFF) return;
    ne2k_enter();
    ne2k_imr_sync(0);
    nic.irq_on = 0;
    nic.imr_mask = 0;
    wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
    nic.state = NE2K_STATE_STOPPED;
    reset_soft_state();
    nic.busy = 0;
    nic.irq_pending = 0;
}

/* 有限回の再初期化。上限を超えたら FAILED で NIC だけ止める。 */
static void reinit_or_fail(void)
{
    int rc;
    if (nic.tx_reinit_run >= NE2K_TX_REINIT_MAX) {
        nic.state = NE2K_STATE_FAILED;
        wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
        ne2k_imr_sync(0);
        return;
    }
    nic.tx_reinit_run++;
    nic.st.reinit++;
    rc = bring_up();
    if (rc) {
        nic.state = NE2K_STATE_FAILED;
        wr(NE2K_CR, NE2K_CR_PAGE0 | NE2K_CR_RD2 | NE2K_CR_STP);
        ne2k_imr_sync(0);
    }
}

