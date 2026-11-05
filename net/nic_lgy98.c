/* LGY-98 owns its lifetime and IRQ; the shared NIC borrows its operations. */
#include "net/nic.h"
#include "drivers/net/ne2000/ne2000.h"
#include "drivers/net/ne2000/ne2000_regs.h"
#include "net/link.h"
#include "os32/os32_kapi_shared.h"

static int lgy_result(int rc)
{
    switch (rc) {
    case NE2K_OK: return 0;
    case NE2K_ERR_BUSY:
    case NE2K_ERR_AGAIN: return OS32_ERR_AGAIN;
    case NE2K_ERR_STOPPED: return OS32_ERR_HOSTDOWN;
    case NE2K_ERR_INVAL: return OS32_ERR_INVAL;
    case NE2K_ERR_NOSPACE: return OS32_ERR_NOSPC;
    case NE2K_ERR_TIMEOUT: return OS32_ERR_TIMEOUT;
    default: return OS32_ERR_IO;
    }
}

static int lgy_tx(const void *frame, u32 len)
{
    return lgy_result(ne2k_send(frame, len));
}

static int lgy_rx(void *frame, u32 cap, u32 *len)
{
    unsigned int length;
    int rc = ne2k_recv(frame, cap, len ? &length : 0);
    if (len && (rc == NE2K_OK || rc == NE2K_ERR_NOSPACE)) *len = length;
    return lgy_result(rc);
}

static void lgy_mac(u8 *mac) { ne2k_get_mac(mac); }
/* IRQ and the existing timer already service the device. */
static void lgy_poll(void) { }
static u32 lgy_credit(void)
{
    u32 ring = ne2k_rx_ring_free_pages();
    u32 queue = ne2k_rx_queue_free() * LINK_MAXFRAME_PAGES;
    return (ring < queue ? ring : queue) * NE2K_PAGE_SIZE;
}

static const struct NetNicOps lgy_ops = {
    lgy_tx, lgy_rx, lgy_mac, lgy_poll, lgy_credit
};

static int lgy_acquire(const struct NetNicOps **out)
{
    int state = ne2k_state();
    if (state != NE2K_STATE_RUNNING && state != NE2K_STATE_OVW_WAIT)
        return OS32_ERR_NOSYS;
    *out = &lgy_ops;
    return 0;
}

static void lgy_release(void) { }
const struct NetAdapter net_lgy98_adapter = { lgy_acquire, lgy_release };
