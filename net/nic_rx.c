/* Shared bounded receive owner; NET_HOSTSVC_DESIGN §2 and addendum #2. */
#include "nic_rx.h"
#include "link.h"
#include "io.h"
#include "kstring.h"
#include "os32_kapi_shared.h"

/* Design §2: each protocol owns four complete Ethernet frames without FCS. */
#define RX_SLOTS 4u
#define RX_FRAME_BYTES (ETH_FRAME_MAX - ETH_FCS_LEN)
struct RxQueue {
    u8 frames[RX_SLOTS][RX_FRAME_BYTES];
    u32 lengths[RX_SLOTS], head, count;
};
static struct RxQueue sfs_queue, link_queue;
static u8 rx_frame[RX_FRAME_BYTES];
/* Overflow counters remain cumulative across protocol queue resets. */
u32 net_rx_sfs_dropped, net_rx_link_dropped;

static struct RxQueue *rx_queue(u16 protocol)
{
    if (protocol == NET_RX_SFS) return &sfs_queue;
    if (protocol == NET_RX_LINK) return &link_queue;
    return 0;
}

static int mac_equal(const u8 *a, const u8 *b)
{
    for (u32 i = 0; i < ETH_ALEN; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

void net_rx_demux(const struct NetNicOps *ops, const u8 local[6], const u8 peer[6])
{
    static const u8 broadcast[ETH_ALEN] = {255, 255, 255, 255, 255, 255};
    for (u32 scan = 0; scan < NET_RX_SCAN_MAX; scan++) {
        u32 len = 0;
        if (ops->rx(rx_frame, sizeof(rx_frame), &len)) break;
        if (len < ETH_HLEN || len > sizeof(rx_frame)) continue;
        if (rx_frame[ETH_TYPE_OFF] != (OS32_ETHERTYPE >> 8) ||
            rx_frame[ETH_TYPE_OFF + 1] != (OS32_ETHERTYPE & 255)) continue;
        if (peer && !mac_equal(rx_frame + ETH_ALEN, peer)) continue;
        int own = mac_equal(rx_frame, local);
        if (!own && !mac_equal(rx_frame, broadcast)) continue;
        const u8 *p = rx_frame + ETH_HLEN;
        u32 n = len - ETH_HLEN;
        int sfs = n >= 3 && p[0] == 0x05 && p[1] == 0x53 && p[2] == 0x46;
        if (!own && (sfs || n == 0 || p[0] != LINK_OP_HELLO)) continue;
        struct RxQueue *q = sfs ? &sfs_queue : &link_queue;
        if (q->count == RX_SLOTS) {
            if (sfs) net_rx_sfs_dropped++;
            else net_rx_link_dropped++;
            continue;
        }
        u32 tail = (q->head + q->count) % RX_SLOTS;
        kmemcpy(q->frames[tail], rx_frame, len);
        q->lengths[tail] = len;
        q->count++;
    }
}

u32 net_rx_queued(u16 protocol)
{
    struct RxQueue *q = rx_queue(protocol);
    return q ? q->count : 0;
}

int net_rx_take(u16 protocol, void *out, u32 cap, u32 *len)
{
    struct RxQueue *q = rx_queue(protocol);
    if (!q || !out || !len) return OS32_ERR_INVAL;
    unsigned int flags = irq_save();
    int rc = OS32_ERR_AGAIN;
    if (!q->count) net_nic_poll_once();
    if (q->count) {
        *len = q->lengths[q->head];
        rc = OS32_ERR_NOSPC;
        if (cap >= *len) {
            kmemcpy(out, q->frames[q->head], *len);
            q->head = (q->head + 1) % RX_SLOTS;
            q->count--;
            rc = 0;
        }
    }
    irq_restore(flags);
    return rc;
}

void net_rx_reset(u16 protocol)
{
    unsigned int flags = irq_save();
    struct RxQueue *q = rx_queue(protocol);
    if (q) q->head = q->count = 0;
    irq_restore(flags);
}
