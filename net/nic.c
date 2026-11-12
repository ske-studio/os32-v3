#ifdef TICK_PROBE
#include "kernel/time/tick_probe.h"
#endif
/* Shared NIC lifetime and dispatch; NET_HOSTSVC_DESIGN §2, addenda #1/#2. */
#include "nic.h"
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
static u32 received;

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
        /* Register once; unbind leaves the resident callback gated by bound. */
        if (!tick_registered) {
            rc = tick_register(PC98_TICK_LINK, net_tick);
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

/* demux owns the scan budget. Count frames received, including discarded ones,
 * without changing A3's void interface or draining the adapter a second time. */
static int counted_rx(void *out, u32 cap, u32 *len)
{
    int rc = bound->rx(out, cap, len);
    if (!rc) received++;
    return rc;
}

/* Caller holds IF=0; net_tick uses this while its reentry guard is set. */
static int poll_once_locked(void)
{
    int count = 0;
    if (bound) {
        struct NetNicOps ops = *bound;
        received = 0;
        ops.rx = counted_rx;
        bound->poll();
        net_rx_demux(&ops, local_mac, fixed_peer ? peer_mac : 0);
        count = (int)received;
    }
    return count;
}

int net_nic_poll_once(void)
{
    unsigned int flags = irq_save();
    int count = ticking ? 0 : poll_once_locked();
    irq_restore(flags);
    return count;
}

void net_tick(void)
{
    unsigned int flags = irq_save();
    if (bound && !ticking) {
        ticking = 1;
#ifdef TICK_PROBE
        tick_probe_net_start();
        tick_probe_net_begin(TP_NET0);
#endif
        poll_once_locked();
#ifdef TICK_PROBE
        tick_probe_net_end(TP_NET0);
#endif
        link_tick();
#ifdef TICK_PROBE
        tick_probe_net_finish();
#endif
        ticking = 0;
    }
    irq_restore(flags);
}
