#ifndef POWERMAC_NIC_GEM_H
#define POWERMAC_NIC_GEM_H
#include "net/nic.h"
/* Platform-exclusive registration slot; PowerMac has no LGY98.
 * P1g registers this adapter here; AUTO already falls back to this slot. */
#define PM_NET_NIC_GEM NET_NIC_LGY98
extern const struct NetAdapter net_gem_adapter;
#if KBOOT_LINK && KBOOT_SH
struct pm_gem_trace {
    unsigned long long rx_packets, rx_drops;
    u32 adapter, link_queue, tx_pending, ready;
};
void pm_gem_trace_snapshot(struct pm_gem_trace *out);
#endif
#endif
