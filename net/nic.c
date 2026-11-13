#include "kernel/idle_work.h"
#ifdef TICK_PROBE
#include "kernel/time/tick_probe.h"
#endif
/* Shared NIC lifetime and dispatch; NET_HOSTSVC_DESIGN §2, addenda #1/#2. */
#include "nic.h"
#include "rx_work.h"
#include "kernel_tick.h"
#include "clock_source.h"
#include "kernel/irq/tick_budget.h"
#include "irq.h"
#include "nic_rx.h"
#include "link.h"
#include "io.h"
#include "kprintf.h"
#include "kernel/irq/tick.h"
#include "platform/pc98/tick_order.h"
#include "os32_kapi_shared.h"

static const struct NetAdapter *adapters[NET_NIC_LGY98 + 1];
static const struct NetAdapter *owner;
static const struct NetNicOps *bound;
static u8 local_mac[6], peer_mac[6];
static int fixed_peer, ticking, tick_registered;
#define LINK_RX_IDLE_RESCUE_JIFFIES 2u
static int pending, idle_registered, unbind_due, link_due;
static u32 reset_due, last_idle_jiffy, batch_depth;
static struct rx_work_budget batch_budget;
static void net_rx_idle(void);
#ifdef TICK_PROBE
u32 net_link_deferred, net_link_deferred_max_us;
static u32 link_due_start;
static int link_due_clock_valid;
#endif

int net_adapter_register(enum NetNicId id, const struct NetAdapter *adapter)
{
    if (id != NET_NIC_E82557 && id != NET_NIC_LGY98)
        return OS32_ERR_INVAL;
    if (!adapter || !adapter->acquire || !adapter->release)
        return OS32_ERR_INVAL;
    if (adapters[id])
        return adapters[id] == adapter ? 0 : OS32_ERR_BUSY;
    adapters[id] = adapter;
    return 0;
}

int net_bind(const struct NetNicOps *ops)
{
    unsigned int flags = irq_save();
    int rc = 0;

    if (bound) rc = OS32_ERR_BUSY;
    else if (!ops || !ops->tx || !ops->rx || !ops->mac ||
             !ops->poll || !ops->rx_credit_bytes) rc = OS32_ERR_INVAL;
    else {
        if (!idle_registered) {
            rc = idle_work_register(net_rx_idle);
            if (!rc) idle_registered = 1;
        }
        /* Register once; unbind leaves the resident callback gated by bound. */
        if (!rc && !tick_registered) {
            rc = tick_register(PC98_TICK_LINK, net_jiffy);
            if (rc) {
                kprintf(ATTR_YELLOW, "[nic] tick registration failed: %d\n", rc);
                rc = OS32_ERR_BUSY;
            } else tick_registered = 1;
        }
        if (!rc) {
            ops->mac(local_mac);
            net_rx_reset(NET_RX_SFS);
            net_rx_reset(NET_RX_LINK);
            fixed_peer = 0;
            link_init(local_mac);
            pending = link_due = unbind_due = 0;
            last_idle_jiffy = tick_count;
            bound = ops;
        }
    }
    irq_restore(flags);
    return rc;
}

int net_start(enum NetNicId choice, const u8 peer[6])
{
    const struct NetAdapter *adapter;
    const struct NetNicOps *ops = 0;
    int rc;

    if (choice == NET_NIC_AUTO)
        choice = adapters[NET_NIC_E82557] ? NET_NIC_E82557 : NET_NIC_LGY98;
    if (choice != NET_NIC_E82557 && choice != NET_NIC_LGY98)
        return OS32_ERR_INVAL;
    adapter = adapters[choice];
    if (bound) {
        if (!adapter || owner != adapter) return OS32_ERR_BUSY;
        if (!fixed_peer && peer) {
            if (link_hello_ok)
                for (u32 i = 0; i < sizeof(peer_mac); i++)
                    if (link_peer_mac[i] != peer[i]) return OS32_ERR_BUSY;
            for (u32 i = 0; i < sizeof(peer_mac); i++) peer_mac[i] = peer[i];
            fixed_peer = 1; /* Pin AUTO without restarting the link. */
        }
        if (fixed_peer != (peer != 0)) return OS32_ERR_BUSY;
        if (peer)
            for (u32 i = 0; i < sizeof(peer_mac); i++)
                if (peer_mac[i] != peer[i]) return OS32_ERR_BUSY;
        return 0; /* Same NIC/peer: preserve the handshake and its nonce. */
    }
    if (!adapter) return OS32_ERR_NOSYS;
    rc = adapter->acquire(&ops);
    if (rc) return rc;
    /* Hold IF through publication of the reference and peer selection. */
    unsigned int flags = irq_save();
    rc = net_bind(ops);
    if (!rc) {
        owner = adapter;
        fixed_peer = peer != 0;
        if (peer)
            for (u32 i = 0; i < sizeof(peer_mac); i++) peer_mac[i] = peer[i];
    }
    irq_restore(flags);
    if (rc) adapter->release();
    return rc;
}

void net_unbind(void)
{
    unsigned int flags = irq_save();
    const struct NetAdapter *adapter = owner;
    if (ticking) {
        unbind_due = 1;
        irq_restore(flags);
        return;
    }
    unbind_due = pending = link_due = 0;
    bound = 0; /* Gates the permanently registered tick before releasing ops. */
    owner = 0;
    fixed_peer = 0;
    net_rx_reset(NET_RX_SFS);
    net_rx_reset(NET_RX_LINK);
    irq_restore(flags);
    if (adapter) adapter->release();
}

int net_tx(const void *frame, u32 len)
{
    unsigned int flags = irq_save();
    int rc = bound ? bound->tx(frame, len) : OS32_ERR_HOSTDOWN;
    irq_restore(flags);
    if (kctx_irq_depth == 0) idle_work_run();
    return rc;
}

int net_mac(u8 out[6])
{
    unsigned int flags = irq_save();
    int rc = OS32_ERR_HOSTDOWN;
    if (bound) {
        rc = out ? 0 : OS32_ERR_INVAL;
        if (out)
            for (u32 i = 0; i < sizeof(local_mac); i++) out[i] = local_mac[i];
    }
    irq_restore(flags);
    return rc;
}

u32 net_rx_credit_bytes(void)
{
    unsigned int flags = irq_save();
    u32 credit = bound ? bound->rx_credit_bytes() : 0;
    irq_restore(flags);
    return credit;
}

void net_rx_schedule(void)
{
    unsigned int flags = irq_save();
    pending = 1;
    irq_restore(flags);
}

int net_rx_defer_reset(u16 protocol)
{
    if (!ticking) return 0;
    reset_due |= 1u << protocol;
    return 1;
}

void net_rx_batch_begin(void)
{
    if (!batch_depth++) rx_work_begin(&batch_budget);
}
void net_rx_batch_end(void)
{
    if (batch_depth) batch_depth--;
}

/* IF=0, still owning the guard. Collapse IRQ requests to one absolute tick. */
static void rx_finish(void)
{
    if (link_due && bound) {
        link_due = 0;
#ifdef TICK_PROBE
        u32 now, hi;
        if (link_due_clock_valid && !platform_clock_read_us(&now, &hi) &&
            (u32)(now - link_due_start) > net_link_deferred_max_us)
            net_link_deferred_max_us = now - link_due_start;
        tick_probe_net_start();
        tick_probe_net_begin(TP_NET0);
        tick_probe_net_end(TP_NET0);
#endif
        link_tick();
#ifdef TICK_PROBE
        tick_probe_net_finish();
#endif
    }
    ticking = 0;
    u32 resets = reset_due;
    reset_due = 0;
    if (resets & (1u << NET_RX_SFS)) net_rx_reset(NET_RX_SFS);
    if (resets & (1u << NET_RX_LINK)) net_rx_reset(NET_RX_LINK);
}

static int rx_service(int idle)
{
    struct rx_work_budget local;
    struct rx_work_budget *b;
    int count;
    if (!_irq_enabled() || kctx_irq_depth != 0) {
        net_rx_schedule();
        return 0;
    }
    unsigned int flags = irq_save();
    if (!bound || ticking || unbind_due) {
        if (bound && ticking) net_rx_schedule();
        irq_restore(flags);
        return 0;
    }
    ticking = 1;
    if (idle) last_idle_jiffy = tick_count;
    pending = 0;
    irq_restore(flags);
    b = batch_depth ? &batch_budget : &local;
    if (!batch_depth) rx_work_begin(b);
    count = (int)net_rx_demux_budget(bound, local_mac, fixed_peer ? peer_mac : 0, b);
    flags = irq_save();
    rx_finish();
    irq_restore(flags);
    if (unbind_due) net_unbind();
    return count;
}

int net_rx_service(void) { return rx_service(0); }
int net_nic_poll_once(void) { return net_rx_service(); }

static void net_rx_idle(void)
{
    /* Permanent: even without a new need_* mark, resume an exhausted scan. */
    if (unbind_due) net_unbind();
    if (bound) rx_service(1);
}

void net_tick(void)
{
    unsigned int flags = irq_save();
    if (bound && !ticking) {
        ticking = 1;
        bound->poll();
        net_rx_schedule();
        ticking = 0;
    }
    irq_restore(flags);
}

void net_jiffy(void)
{
    unsigned int flags = irq_save();
#ifdef TICK_PROBE
    if (bound && !ticking) {
        tick_probe_net_start();
        tick_probe_net_begin(TP_NET0);
    }
#endif
    net_tick();
    if (bound) {
        if (ticking) {
#ifdef TICK_PROBE
            u32 hi;
            net_link_deferred++;
            if (!link_due)
                link_due_clock_valid = !platform_clock_read_us(&link_due_start, &hi);
#endif
            link_due = 1;
        }
        else {
            ticking = 1;
            if (owner && owner == adapters[NET_NIC_E82557] &&
                (u32)(tick_count - last_idle_jiffy) >= LINK_RX_IDLE_RESCUE_JIFFIES &&
                tick_budget_peek(tick_budget_cnt(IRQ0_SLOT_BUDGET_US / 2),
                                 tick_budget_cnt(NET_TAIL_RX_US))) {
                struct rx_work_budget b;
                pending = 0;
                rx_work_begin(&b);
                net_rx_demux_budget(bound, local_mac, fixed_peer ? peer_mac : 0, &b);
            }
#ifdef TICK_PROBE
            tick_probe_net_end(TP_NET0);
#endif
            link_tick();
#ifdef TICK_PROBE
            tick_probe_net_finish();
#endif
            rx_finish();
        }
    }
    irq_restore(flags);
}
