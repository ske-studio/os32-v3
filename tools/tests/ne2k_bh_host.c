/* Real NE2K + net_tx + idle_work, fake device; no libc or privileged opcodes. */
#define NE2K_BH_TEST
static void bh_wait_check(void);
static unsigned int bh_now, bh_pio_cost, bh_demux_cost, bh_requests;
#define io_request bh_request

#include "ne2000_dma_host.c"
#undef io_request
void io_request(u32 id);
void bh_request(u32 id) { if (id == IO_SVC_NE2K_BH) bh_requests++; io_request(id); }
static void _idle(void) {}
#include "../../kernel/idle_work.c"
#define pending nic_pending
#include "../../net/nic.c"
#undef pending
#include "../../net/rx_work.c"
#include "rx_io_clock.h"
int platform_clock_read_us(u32 *lo, u32 *hi) { *lo = bh_now; *hi = 0; return 0; }

static unsigned int waits;
static void bh_wait_check(void)
{
    if (!_irq_enabled() || kctx_irq_depth) finish(1);
    waits++;
}
static int queued_tx(const void *frame, u32 len) { return ne2k_send(frame, len); }
static void bh_poll(void) {} /* Driver BH is exercised directly by this fixture. */
static const struct NetNicOps bh_ops = { .tx = queued_tx, .poll = bh_poll };

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
    CHECK(nic.state == NE2K_STATE_RUNNING);
    CHECK(nic.tx_busy);
    CHECK(nic.txq_count == 0);
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
    /* Exact FIFO progress uses a full provider slice; dispatcher fairness may
     * supply only one unit (completion and the next PIO are separate units). */
    for (unsigned int i = 0; i < NE2K_TXQ_SLOTS; i++) {
        ne2k_bh();
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
    ne2k_bh();
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

static void same_bh_demux(void);
static void tier_work(void);
void _start(void)
{
    test_rx_clock_install();
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
    same_bh_demux();
    tier_work();
    finish(0);
}

u32 tick_budget_cnt(u32 us) { return us; }
int tick_budget_take(u32 share, u32 tail) { (void)share; (void)tail; return 1; }
static u32 demux_frames;
u32 net_rx_demux_budget(const struct NetNicOps *ops, const u8 local[6],
                         const u8 peer[6], struct rx_work_budget *b)
{
    u8 frame[1514]; u32 len, count = 0;
    (void)local; (void)peer;
    if (!ops->rx) return 0;
    CHECK(_irq_enabled() && !kctx_irq_depth);
    while (rx_work_take(b)) {
        if (ops->rx(frame, sizeof(frame), &len)) return count;
        CHECK(len == 60 && frame[0] == 0x5a);
        count++; demux_frames++;
        bh_now += bh_demux_cost;
    }
    net_rx_schedule();
    return count;
}
static int bh_recv(void *out, u32 cap, u32 *len)
{
    unsigned int n = 0;
    int rc = ne2k_recv(out, cap, &n);
    *len = n;
    return rc;
}
static const struct NetNicOps receive_ops = { .tx = queued_tx, .rx = bh_recv, .poll = bh_poll };
static void same_bh_demux(void)
{
    device_init();
    bound = &receive_ops;
    demux_frames = 0;
    queue_frames(9);
    ne2k_poll(4);
    ne2k_bh();
    CHECK(demux_frames > 0 && demux_frames <= NET_RX_SCAN_MAX);
    CHECK(nic.rxq_count && nic.st.rx_frames == 9);
    /* No new driver marks: its early return must not strand software RX. */
    nic.need_rx = nic.need_tx = nic.need_reinit = nic.rx_backlog = 0;
    CHECK(io_register(&rx_desc) == 0);
    io_request(IO_SVC_NET_RX);
    idle_work_run();
    CHECK(demux_frames == 9 && !nic.rxq_count);
    bound = &bh_ops;
}
void net_rx_reset(u16 protocol) { (void)protocol; }
void link_tick(void) {}

int tick_budget_peek(u32 share, u32 tail) { (void)share; (void)tail; return 1; }

u32 tick_clock_ms(void) { return tick_count * 10u; }

/* Each entry's requests are counted independently of later dispatcher work. */
static void request_origins(void)
{
    device_init();
    bound = &bh_ops;
    bh_requests = 0;
    nic.busy = 1;
    CHECK(ne2k_irq_shared(5, 0) == IRQ_DEFERRED);
    CHECK(bh_requests == 1);
    bh_requests = 0;
    ne2k_poll(4);
    CHECK(bh_requests == 1);
    nic.busy = 0;
    nic.irq_pending = 0;
    bh_requests = 0;
    ne2k_timer_tick();
    CHECK(bh_requests == 1);
    bh_requests = 0;
    CHECK(ne2k_send(buffer, 60) == 0);
    CHECK(bh_requests == 1);
    bh_requests = 0;
    host_if_mask = 0;
    CHECK(!wait_allowed());
    host_if_mask = EFLAGS_IF;
    CHECK(bh_requests == 1);
    nic.need_reinit = nic.need_tx = nic.txq_count = 0;
    queue_frames(1);
    bh_requests = 0;
    CHECK(ne2k_irq_shared(5, 0) == IRQ_HANDLED);
    CHECK(bh_requests == 3); /* IRQ, mark, leave mark */
    bh_requests = 0;
    mark_work();
    CHECK(bh_requests == 1);
    struct ne2k_config config = nic.cfg;
    bh_requests = 0;
    CHECK(ne2k_init(&config) == 0);
    CHECK(bh_requests == 1);
    struct io_slice s;
    CHECK(io_slice_begin(&s, 120, 0) == 0);
    bh_requests = 0;
    reinit_budget(&s);
    CHECK(bh_requests == 1 && nic.need_reinit);
    nic.need_reinit = 0;
    CHECK(ne2k_send(buffer, 60) == 0);
    dma_fails = 1; dma_fail_countdown = NE2K_RDMA_ATTEMPTS;
    bh_requests = 0;
    tx_dequeue();
    CHECK(bh_requests == 1 && nic.need_reinit);
    device_init();
    queue_frames(1); ne2k_poll(4);
    CHECK(io_slice_begin(&s, 120, 1) == 0);
    bh_requests = 0;
    CHECK(service(4, &s));
    CHECK(bh_requests == 1 && nic.rx_backlog);
    CHECK(io_slice_begin(&s, 120, ~0u) == 0);
    CHECK(service(4, &s) == 0 && nic.rxq_count == 1);
    nic.rx_backlog = 1;
    bh_requests = 0;
    unsigned int len;
    CHECK(ne2k_recv(buffer, sizeof(buffer), &len) == 0);
    CHECK(bh_requests == 1);
    queue_frames(1);
    bh_requests = 0;
    ovw_begin(NE2K_ISR_OVW);
    CHECK(bh_requests == 1);
    tick_count += NE2K_OVW_WAIT_TICKS;
    CHECK(io_slice_begin(&s, 120, 0) == 0);
    bh_requests = 0;
    ovw_continue(&s);
    CHECK(bh_requests == 1 && nic.state == NE2K_STATE_OVW_WAIT);
}

static int provider_slice(u32 us, u32 units)
{
    struct io_slice slice;
    CHECK(io_slice_begin(&slice, us, units) == 0);
    return ne2k_desc.step(0, &slice);
}

static void tier_work(void)
{
    request_origins();
    /* Registration uses the real io_policy.c, not a permissive stub. */
    CHECK(io_unregister(IO_SVC_NE2K_BH) == 0);
    CHECK(io_register(&ne2k_desc) == 0);
    device_init(); bound = &receive_ops;
    bh_now = 0; bh_pio_cost = 30; demux_frames = 0;
    queue_frames(9); ne2k_poll(4);
    CHECK(provider_slice(500, ~0u) == IO_MORE);
    CHECK(nic.st.rx_frames == 2 && demux_frames == 1 && bh_now == 120);
    /* Time is shared by PIO and every enqueue-triggered demux call. */
    bh_pio_cost = 20; bh_demux_cost = 20; bh_now = 0;
    device_init(); bh_now = 0; queue_frames(9); ne2k_poll(4); demux_frames = 0;
    CHECK(provider_slice(500, ~0u) == IO_MORE);
    CHECK(nic.st.rx_frames == 2 && demux_frames == 2 && bh_now == 120);
    /* Remaining caller time is narrower than the 120us provider cap. */
    bh_demux_cost = 0; bh_pio_cost = 10; bh_now = 0;
    device_init(); bh_now = 0; queue_frames(9); ne2k_poll(4);
    CHECK(provider_slice(40, ~0u) == IO_MORE);
    CHECK(nic.st.rx_frames == 2 && bh_now == 40);
    /* Exhausted units cannot start RX, TX, counters, or recovery. */
    bh_pio_cost = 0; bh_now = 0;
    device_init(); bound = &bh_ops;
    queue_frames(1); ne2k_poll(4);
    CHECK(ne2k_send(buffer, 60) == 0);
    unsigned int pios = device_pios, resets = reset_count;
    CHECK(provider_slice(120, 0) == IO_MORE);
    CHECK(device_pios == pios && !nic.st.rx_frames && !tx_starts);
    nic.need_reinit = 1;
    CHECK(provider_slice(120, 0) == IO_MORE);
    CHECK(reset_count == resets && nic.need_reinit);
    CHECK(provider_slice(120, 1) == IO_MORE);
    CHECK(reset_count > resets && !nic.need_reinit);
    device_init();
    nic.need_tx = nic.tx_busy = 1;
    txp = 0; tx_status = NE2K_TSR_PTX;
    CHECK(provider_slice(120, 0) == IO_MORE);
    CHECK(nic.tx_busy && !nic.st.tx_ok);
    device_init();
    ovw_begin(NE2K_ISR_OVW);
    tick_count += NE2K_OVW_WAIT_TICKS;
    CHECK(provider_slice(120, 0) == IO_MORE);
    CHECK(nic.state == NE2K_STATE_OVW_WAIT && regs[0][NE2K_P0_TCR] == NE2K_TCR_NORMAL);
    /* Fresh dispatcher registration removes earlier pending requests. Start
     * work after the first empty pass solely through the provider's MORE. */
    CHECK(io_unregister(IO_SVC_NE2K_BH) == 0);
    CHECK(io_register(&ne2k_desc) == 0);
    device_init();
    idle_work_run();
    queue_frames(1); /* lost IRQ: no explicit request after the empty pass */
    nic.need_rx = NE2K_ISR_PRX;
    for (unsigned int i = 0; i < 2; i++) idle_work_run();
    CHECK(nic.st.rx_frames == 1);
    /* Reentry, a preexisting batch, and terminal states leave the slice alone. */
    struct io_slice slice;
    CHECK(io_slice_begin(&slice, 120, 2) == 0);
    nic.busy = 1;
    CHECK(ne2k_desc.step(0, &slice) == IO_MORE && !slice.units);
    nic.busy = 0;
    net_rx_batch_begin();
    nic.need_tx = 1;
    CHECK(ne2k_desc.step(0, &slice) == IO_MORE && !slice.units);
    net_rx_batch_end();
    ne2k_stop();
    CHECK(ne2k_desc.step(0, &slice) == IO_DONE);
    /* OVW drain yields in loopback, retaining the old TX completion state. */
    device_init(); bound = &bh_ops;
    queue_frames(3); regs[0][NE2K_P0_ISR] |= NE2K_ISR_OVW;
    ne2k_poll(4); ne2k_bh();
    tick_count += NE2K_OVW_WAIT_TICKS;
    bh_pio_cost = 30; bh_now = 0;
    CHECK(provider_slice(120, ~0u) == IO_MORE);
    CHECK(nic.state == NE2K_STATE_OVW_WAIT && nic.st.rx_frames == 2);
    bh_pio_cost = 0; bh_now = 0;
    CHECK(provider_slice(120, ~0u) == IO_MORE);
    CHECK(nic.state == NE2K_STATE_RUNNING && nic.st.rx_frames == 3);
}
