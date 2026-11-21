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

#define GEM_FRAME_BYTES 1514u
static u32 rx_budget;
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
    struct gem *g = acquired ? pm_lan_running_gem(0) : 0;
    if (!g) return OS32_ERR_HOSTDOWN;
    if (!rx_budget) return OS32_ERR_AGAIN;
    return result(gem_rx_some(g, frame, cap, len, &rx_budget));
}

static void mac(u8 *out) { (void)pm_lan_running_gem(out); }
/* Only whole groups of four have been kicked back to the device. */
static u32 free_slots(struct gem *g, u32 done)
{
    u32 pending = (done - g->rx_head) & (GEM_RING_COUNT - 1);
    u32 published = GEM_RX_PUBLISHED - g->rx_head % 4;
    return pending < published ? published - pending : 0;
}

#ifdef KBOOT_GEM_RX_DIAG
/* Field           shift   width
 * poll serial         0      16
 * free DMA slots     34       5
 * rx_head            19       5
 * RX_COMPLETION      24       5
 * last RX_KICK       29       5
 */
extern volatile u32 gem_rx_last_kick;
volatile unsigned long long gem_rx_diag;
static void observe_poll(struct gem *g)
{
    static unsigned short serial;
    u32 done = g ? g->io.read32(g->io.ctx, GEM_RX_COMPLETION) : 0;
    u32 head = g ? g->rx_head : 0;
    gem_rx_diag = ++serial | ((unsigned long long)(g ? free_slots(g, done) : 0) << 34) | ((head & 31u) << 19) |
        ((done & 31u) << 24) | ((unsigned long long)(gem_rx_last_kick & 31u) << 29);
}
#endif
static void poll(void)
{
    struct gem *g = acquired ? pm_lan_running_gem(0) : 0;
    if (!g || gem_poll(g, g->io.tb(g->io.ctx), 8u) < 0 ||
        !pm_lan_running_gem(0)) {
        rx_budget = 0;
#ifdef KBOOT_GEM_RX_DIAG
        observe_poll(0);
#endif
        return;
    }
    rx_budget = GEM_RX_SCAN_BUDGET;
#ifdef KBOOT_GEM_RX_DIAG
    observe_poll(g);
#endif
}
static u32 credit(void)
{
    struct gem *g = acquired ? pm_lan_running_gem(0) : 0;
    return g ? free_slots(g, g->io.read32(g->io.ctx, GEM_RX_COMPLETION)) * GEM_FRAME_BYTES : 0;
}
static const struct NetNicOps ops = { tx, rx, mac, poll, credit };
static int acquire(const struct NetNicOps **out)
{
    if (!out) return OS32_ERR_INVAL;
    if (!pm_lan_running_gem(0)) return OS32_ERR_NOSYS;
    if (acquired) return OS32_ERR_BUSY;
    acquired = 1; rx_budget = 0; *out = &ops;
    return 0;
}
static void release(void) { acquired = 0; rx_budget = 0; }
const struct NetAdapter net_gem_adapter = { acquire, release };
