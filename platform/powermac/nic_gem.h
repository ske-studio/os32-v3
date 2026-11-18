#ifndef POWERMAC_NIC_GEM_H
#define POWERMAC_NIC_GEM_H
#include "net/nic.h"
/* Platform-exclusive registration slot; PowerMac has no LGY98.
 * P1g registers this adapter here; AUTO already falls back to this slot. */
#define PM_NET_NIC_GEM NET_NIC_LGY98
extern const struct NetAdapter net_gem_adapter;
#if KBOOT_LINK && KBOOT_SH
#include "drivers/net/gem/gem.h"

struct pm_gem_trace {
    unsigned long long rx_packets, rx_drops;
    struct gem_rx_counts rx;
    u16 mac_delta[7];
    u32 mac_generation;
    u32 adapter, link_queue, tx_pending, ready;
};
const struct gem *pm_lan_gem_trace_source(void);
void pm_gem_trace_snapshot(struct pm_gem_trace *out);
#endif
#endif
