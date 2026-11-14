/* Borrow the service's DMA/device. P1g must exclude PLN1 RX and arrange
 * reset/PHY progress in lan_service; only net_jiffy may call this poll. */
#include "nic_gem.h"
#include "lan_boot.h"
#include "drivers/net/gem/gem.h"
#include "os32/os32_kapi_shared.h"

static u8 packet[1514];
static u32 packet_len;
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
    return g ? result(gem_tx(g, frame, len)) : OS32_ERR_HOSTDOWN;
}

static int rx(void *frame, u32 cap, u32 *len)
{
    if (!frame || !len) return OS32_ERR_INVAL;
    if (!acquired || !pm_lan_running_gem(0)) return OS32_ERR_HOSTDOWN;
    if (!packet_len) return OS32_ERR_AGAIN;
    *len = packet_len;
    if (cap < packet_len) return OS32_ERR_NOSPC;
    for (u32 i = 0; i < packet_len; i++) ((u8 *)frame)[i] = packet[i];
    packet_len = 0;
    return 0;
}

static void mac(u8 *out) { (void)pm_lan_running_gem(out); }
static void poll(void)
{
    struct gem *g = acquired ? pm_lan_running_gem(0) : 0;
    if (!g) { packet_len = 0; return; }
    if (gem_poll(g, g->io.tb(g->io.ctx), 8u) < 0 ||
        !pm_lan_running_gem(0)) { packet_len = 0; return; }
    /* gem_rx itself scans up to eight descriptors (including bad frames).
     * Exactly one invocation preserves the total RX descriptor budget. */
    if (!packet_len) {
        u32 n = 0;
        if (gem_rx(g, packet, sizeof(packet), &n) == GEM_OK) packet_len = n;
    }
}
static u32 credit(void)
{
    return acquired && pm_lan_running_gem(0) && !packet_len ? sizeof(packet) : 0;
}
static const struct NetNicOps ops = { tx, rx, mac, poll, credit };
static int acquire(const struct NetNicOps **out)
{
    if (!out) return OS32_ERR_INVAL;
    if (!pm_lan_running_gem(0)) return OS32_ERR_NOSYS;
    if (acquired) return OS32_ERR_BUSY;
    acquired = 1; packet_len = 0; *out = &ops;
    return 0;
}
static void release(void) { acquired = 0; packet_len = 0; }
const struct NetAdapter net_gem_adapter = { acquire, release };
