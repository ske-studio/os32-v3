/* Real NE2K + net_tx + idle_work, fake device; no libc or privileged opcodes. */
#define NE2K_BH_TEST
static void bh_wait_check(void);
#include "ne2000_dma_host.c"
#include "../../kernel/idle_work.c"
#include "../../net/nic.c"

static unsigned int waits;
static void bh_wait_check(void)
{
    if (!_irq_enabled() || kctx_irq_depth) finish(1);
    waits++;
}
static int queued_tx(const void *frame, u32 len) { return ne2k_send(frame, len); }
static const struct NetNicOps bh_ops = { .tx = queued_tx };

static void run_entry(unsigned int entry, int reset_fault)
{
    device_init();
    bound = &bh_ops;
    queue_frames(1);
    dma_fails = 1;
    dma_fail_countdown = NE2K_RDMA_ATTEMPTS;
    reset_fails = reset_fault;
    unsigned int before_waits = waits, before_pios = device_pios;
    unsigned int before_reset = reset_count;
    host_if_mask = 0;
    kctx_irq_depth = entry == 3 ? 0 : 1;
    switch (entry) {
    case 0: CHECK(ne2k_irq_shared(5, 0) == IRQ_HANDLED); break;
    case 1: ne2k_timer_tick(); break;
    case 2: case 3: CHECK(net_tx(buffer, 60) == NE2K_OK); break;
    case 4:
        ne2k_enter();
        nic.irq_pending = 1;
        ne2k_leave();
        break;
    }
    CHECK(!host_if_mask);
    CHECK(waits == before_waits && device_pios == before_pios && reset_count == before_reset);
    CHECK(nic.need_rx || nic.need_tx || nic.need_reinit);
    /* IF=1 alone is insufficient: slot4 depth remains 1 after slot2 returned. */
    host_if_mask = EFLAGS_IF;
    kctx_irq_depth = 1;
    CHECK(net_tx(buffer, 60) == NE2K_OK);
    idle_work_run();
    CHECK(waits == before_waits && device_pios == before_pios);
    kctx_irq_depth = 0;
    idle_work_run();
    CHECK(waits > before_waits && nic.st.rdc_timeout == NE2K_RDMA_ATTEMPTS);
    CHECK(nic.state == (reset_fault ? NE2K_STATE_FAILED : NE2K_STATE_RUNNING));
    /* Clear a persistent reset fault and explicitly reopen the FAILED device. */
    dma_fails = reset_fails = 0;
    struct ne2k_config config = nic.cfg;
    CHECK(ne2k_init(&config) == NE2K_OK);
    CHECK(ne2k_send(buffer, 61) == NE2K_OK);
    idle_work_run();
    CHECK(nic.state == NE2K_STATE_RUNNING && nic.tx_busy && nic.txq_count == 0);
}

static void queue_capacity(void)
{
    device_init();
    unsigned int before = device_pios;
    host_if_mask = 0;
    for (unsigned int i = 0; i < NE2K_TXQ_SLOTS; i++) {
        buffer[0] = (u8)i;
        CHECK(ne2k_send(buffer, 60) == NE2K_OK);
    }
    CHECK(ne2k_send(buffer, 60) == NE2K_ERR_BUSY && device_pios == before);
    host_if_mask = EFLAGS_IF;
    for (unsigned int i = 0; i < NE2K_TXQ_SLOTS; i++) {
        idle_work_run();
        CHECK(nic.tx_busy && ram[(unsigned int)nic.tx_page << 8] == i);
        txp = 0;
        tx_status = NE2K_TSR_PTX;
        regs[0][NE2K_P0_ISR] |= NE2K_ISR_PTX;
        host_if_mask = 0;
        kctx_irq_depth = 1;
        ne2k_irq_shared(5, 0);
        kctx_irq_depth = 0;
        host_if_mask = EFLAGS_IF;
    }
    idle_work_run();
    CHECK(nic.st.tx_ok == NE2K_TXQ_SLOTS && !nic.txq_count && !nic.tx_busy);
}

static void bounded_work(void)
{
    device_init();
    queue_frames(NE2K_BH_ROUNDS * NE2K_IRQ_BUDGET + 1);
    ne2k_poll(4);
    CHECK(!(regs[0][NE2K_P0_ISR] & NE2K_IMR_MASK));
    CHECK(regs[0][NE2K_P0_IMR] == NE2K_IMR_MASK);
    u32 before = nic.st.rx_frames + nic.st.rx_dropped;
    ne2k_bh();
    CHECK(nic.st.rx_frames + nic.st.rx_dropped - before == NE2K_RXQ_SLOTS);
    CHECK(nic.rx_backlog && nic.imr_written == NE2K_IMR_MASK);
    CHECK(!(regs[0][NE2K_P0_ISR] & NE2K_IMR_MASK));
    CHECK(ne2k_irq_shared(5, 0) == IRQ_HANDLED);
}

static void full_queue_backpressure(void)
{
    device_init();
    queue_frames(NE2K_RXQ_SLOTS + 2);
    u8 page = nic.rx_next;
    for (unsigned int i = 0; i < NE2K_RXQ_SLOTS + 2; i++) {
        ram[((unsigned int)page << 8) + NE2K_RX_HDR_LEN] = (u8)i;
        page = ne2k_ring_advance(nic.rx_start, nic.rx_stop, page, 1);
    }
    ne2k_poll(4);
    ne2k_bh();
    u8 retained = nic.rx_next;
    u32 dropped = nic.st.rx_dropped;
    CHECK(nic.rxq_count == NE2K_RXQ_SLOTS && nic.rx_backlog);
    unsigned int before_pios = device_pios;
    CHECK(rx_one(regs[1][NE2K_P1_CURR]) == 2 && device_pios == before_pios);
    instant_tx = 1;
    CHECK(ne2k_send(buffer, 60) == NE2K_OK);
    CHECK(ne2k_send(buffer, 60) == NE2K_OK);
    unsigned int starts = tx_starts;
    ne2k_bh();
    CHECK(nic.rx_next == retained && nic.st.rx_dropped == dropped && nic.rx_backlog);
    CHECK(tx_starts == starts + 1 && nic.txq_count == 1);
    instant_tx = 0;
    unsigned int len;
    for (unsigned int i = 0; i < NE2K_RXQ_SLOTS; i++) {
        CHECK(ne2k_recv(buffer, sizeof(buffer), &len) == NE2K_OK);
        CHECK(buffer[0] == i);
    }
    ne2k_bh();
    CHECK(!nic.rx_backlog && nic.rx_next == regs[1][NE2K_P1_CURR]);
    for (unsigned int i = NE2K_RXQ_SLOTS; i < NE2K_RXQ_SLOTS + 2; i++) {
        CHECK(ne2k_recv(buffer, sizeof(buffer), &len) == NE2K_OK);
        CHECK(buffer[0] == i);
    }
}

static void pending_causes(void)
{
    device_init();
    sticky_cnt = 1;
    regs[0][NE2K_P0_ISR] |= NE2K_ISR_CNT;
    ne2k_poll(4);
    CHECK(regs[0][NE2K_P0_ISR] & NE2K_ISR_CNT);
    CHECK(nic.need_rx & NE2K_ISR_CNT);
    CHECK(nic.imr_written == (NE2K_IMR_MASK & ~NE2K_ISR_CNT));
    ne2k_bh();
    CHECK(!sticky_cnt && !(regs[0][NE2K_P0_ISR] & NE2K_ISR_CNT));
    CHECK(nic.imr_written == NE2K_IMR_MASK);
    regs[0][NE2K_P0_ISR] |= NE2K_ISR_OVW;
    ne2k_poll(4);
    CHECK(nic.imr_written == (NE2K_IMR_MASK & ~NE2K_ISR_OVW));
    ne2k_bh();
    CHECK(nic.state == NE2K_STATE_OVW_WAIT && nic.imr_written == 0);
}

static void ovw_recovery_sends(void)
{
    device_init();
    CHECK(ne2k_send(buffer, 60) == NE2K_OK);
    regs[0][NE2K_P0_ISR] |= NE2K_ISR_OVW;
    ne2k_poll(4);
    ne2k_bh();
    CHECK(nic.state == NE2K_STATE_OVW_WAIT);
    unsigned int starts = tx_starts;
    tick_count += NE2K_OVW_WAIT_TICKS;
    ne2k_bh();
    CHECK(nic.state == NE2K_STATE_RUNNING && tx_starts == starts + 1);
    CHECK(nic.txq_count == 0 && nic.tx_busy);
}

static void transmit_rounds(void)
{
    device_init();
    instant_tx = 1;
    for (unsigned int i = 0; i < NE2K_TXQ_SLOTS; i++)
        CHECK(ne2k_send(buffer, 60) == NE2K_OK);
    unsigned int starts = tx_starts;
    ne2k_bh();
    CHECK(tx_starts - starts == NE2K_TXQ_SLOTS && nic.txq_count == 0);
    instant_tx = 0;
    device_init();
    for (unsigned int i = 0; i < NE2K_TXQ_SLOTS; i++)
        CHECK(ne2k_send(buffer, 60) == NE2K_OK);
    starts = tx_starts;
    unsigned int old_waits = waits, old_resets = reset_count;
    ne2k_bh();
    CHECK(tx_starts - starts == 1 && nic.txq_count == NE2K_TXQ_SLOTS - 1);
    /* The only wait is the successful DMA of the first frame. */
    CHECK(waits == old_waits + 1 && reset_count == old_resets);
    old_waits = waits;
    ne2k_bh();
    CHECK(waits == old_waits && reset_count == old_resets && tx_starts - starts == 1);
}

void _start(void)
{
    for (unsigned int entry = 0; entry < 5; entry++)
        for (int reset_fault = 0; reset_fault < 2; reset_fault++)
            run_entry(entry, reset_fault);
    device_init();
    queue_frames(1);
    ne2k_poll(4);
    kctx_irq_depth = 1;
    unsigned int before = waits;
    ne2k_bh();
    CHECK(waits == before && nic.need_reinit);
    kctx_irq_depth = 0;
    idle_work_run();
    CHECK(nic.state == NE2K_STATE_RUNNING);
    queue_capacity();
    bounded_work();
    full_queue_backpressure();
    transmit_rounds();
    pending_causes();
    ovw_recovery_sends();
    finish(0);
}
