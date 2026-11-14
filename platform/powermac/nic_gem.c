/* Borrow the service's DMA/device. P1g must exclude PLN1 RX and arrange
 * reset/PHY progress in lan_service; only net_jiffy may call this poll. */
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
static void poll(void)
{
    struct gem *g = acquired ? pm_lan_running_gem(0) : 0;
    if (!g) { packet_head = packet_count = 0; return; }
    if (gem_poll(g, g->io.tb(g->io.ctx), 8u) < 0 ||
        !pm_lan_running_gem(0)) { packet_head = packet_count = 0; return; }
    /* gem_rx itself scans up to eight descriptors (including bad frames).
     * Exactly one invocation preserves the total RX descriptor budget. */
    if (packet_count < GEM_RX_SLOTS) {
        u32 tail = (packet_head + packet_count) % GEM_RX_SLOTS;
        u32 n = 0;
        if (gem_rx(g, packet[tail], GEM_FRAME_BYTES, &n) == GEM_OK) {
            packet_len[tail] = n;
            packet_count++;
        }
    }
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
