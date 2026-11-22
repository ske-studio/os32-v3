/* Borrow the service's DMA/device. P1g must exclude PLN1 RX and arrange
 * reset/PHY progress in lan_service; poll runs from net_jiffy or from the
 * deferred rx_finish, always inside link_platform_lock (AC-NET-2). */
#include "nic_gem.h"
#include "lan_boot.h"
#if KBOOT_LINK
#include "rh_observe.h"
#include "net/link.h"
#endif
#include "drivers/net/gem/gem.h"
#include "os32/os32_kapi_shared.h"

/* link subtracts twelve 256-byte pages before advertising WINDOW. A single
 * frame can never cover that reserve. Four slots match the shared RX queues;
 * credit describes actual free storage, without reading DMA in the query. */
#define GEM_RX_SLOTS 4u
#define GEM_FRAME_BYTES 1514u
static u8 packet[GEM_RX_SLOTS][GEM_FRAME_BYTES];
static u32 packet_len[GEM_RX_SLOTS], packet_head, packet_count;
static int acquired;

static int result(int rc)
{
    switch (rc) {
    case GEM_OK: return 0;
    case GEM_AGAIN: case GEM_EBUSY: return OS32_ERR_AGAIN;
    case GEM_EINVAL: return OS32_ERR_INVAL;
    case GEM_ERANGE: return OS32_ERR_NOSPC;
    case GEM_ENODEV: return OS32_ERR_HOSTDOWN;
    case GEM_ETIME: return OS32_ERR_TIMEOUT;
    default: return OS32_ERR_IO;
    }
}

static int tx(const void *frame, u32 len)
{
    struct gem *g = acquired ? pm_lan_running_gem(0) : 0;
    int rc = g ? result(gem_tx(g, frame, len)) : OS32_ERR_HOSTDOWN;
#if KBOOT_LINK
    const u8 *p = frame;
    if (!rc && len >= LINK_ETH_HDR_LEN + LINK_HDR_LEN &&
        p[LINK_ETH_HDR_LEN + LINK_OFF_OP] == LINK_OP_HELLO)
        pm_rh_stage(PM_RH_LINK_HELLO_SENT);
#endif
    return rc;
}

static int rx(void *frame, u32 cap, u32 *len)
{
    if (!frame || !len) return OS32_ERR_INVAL;
    if (!acquired || !pm_lan_running_gem(0)) return OS32_ERR_HOSTDOWN;
    if (!packet_count) return OS32_ERR_AGAIN;
    *len = packet_len[packet_head];
    if (cap < *len) return OS32_ERR_NOSPC;
    for (u32 i = 0; i < *len; i++) ((u8 *)frame)[i] = packet[packet_head][i];
    packet_head = (packet_head + 1) % GEM_RX_SLOTS;
    packet_count--;
    return 0;
}

static void mac(u8 *out) { (void)pm_lan_running_gem(out); }
#ifdef KBOOT_GEM_RX_DIAG
/* Field           shift   width
 * poll serial         0      16
 * packet_count       16       3
 * rx_head            19       7
 * RX_COMPLETION      26       7
 * last RX_KICK       33       7
 */
extern volatile u32 gem_rx_last_kick;
volatile unsigned long long gem_rx_diag;
static void observe_poll(struct gem *g)
{
    static unsigned short serial;
    u32 done = g ? g->io.read32(g->io.ctx, GEM_RX_COMPLETION) : 0;
    u32 head = g ? g->rx_head : 0;
    gem_rx_diag = ++serial | (packet_count << 16) | ((head & 127u) << 19) |
        ((unsigned long long)(done & 127u) << 26) | ((unsigned long long)(gem_rx_last_kick & 127u) << 33);
}
#endif
static void poll(void)
{
    struct gem *g = acquired ? pm_lan_running_gem(0) : 0;
    if (!g || gem_poll(g, g->io.tb(g->io.ctx), 8u) < 0 ||
        !pm_lan_running_gem(0)) {
        packet_head = packet_count = 0;
#ifdef KBOOT_GEM_RX_DIAG
        observe_poll(0);
#endif
        return;
    }
    /* All receive calls share eight descriptor inspections, including bad
     * frames and OWN, while filling the available packet slots. */
    u32 budget = GEM_RX_SCAN_BUDGET;
    while (packet_count < GEM_RX_SLOTS && budget) {
        u32 tail = (packet_head + packet_count) % GEM_RX_SLOTS, n = 0;
        if (gem_rx_some(g, packet[tail], GEM_FRAME_BYTES, &n, &budget) != GEM_OK) break;
        packet_len[tail] = n;
        packet_count++;
    }
#ifdef KBOOT_GEM_RX_DIAG
    observe_poll(g);
#endif
}
static u32 credit(void)
{
    return acquired && pm_lan_running_gem(0) ?
        (GEM_RX_SLOTS - packet_count) * GEM_FRAME_BYTES : 0;
}
static const struct NetNicOps ops = { tx, rx, mac, poll, credit };
static int acquire(const struct NetNicOps **out)
{
    if (!out) return OS32_ERR_INVAL;
    if (!pm_lan_running_gem(0)) return OS32_ERR_NOSYS;
    if (acquired) return OS32_ERR_BUSY;
    acquired = 1; packet_head = packet_count = 0; *out = &ops;
    return 0;
}
static void release(void) { acquired = 0; packet_head = packet_count = 0; }
const struct NetAdapter net_gem_adapter = { acquire, release };
