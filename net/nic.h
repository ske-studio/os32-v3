/* Shared NIC contract: NET_HOSTSVC_DESIGN §2. */
#ifndef OS32_NET_NIC_H
#define OS32_NET_NIC_H

#include "types.h"

/* 0/negative; empty rx returns AGAIN. Adapters own IRQ registration/lifetime.
 * rx preserves IF and only scans/copies/recycles a bounded number of frames:
 * no wait/yield. poll must not repeat the E82557 recovery tick.
 * Credit is in bytes, including for LGY's page-based hardware. */
struct NetNicOps {
    int (*tx)(const void *, u32);
    int (*rx)(void *, u32, u32 *);
    void (*mac)(u8 *);
    void (*poll)(void);
    u32 (*rx_credit_bytes)(void);
};

/* Bind once at boot. Shared users must not drain the NIC directly. */
int net_bind(const struct NetNicOps *ops);
/* Poll/demux once with IF saved/cli. Unbound: 0; otherwise frames received. */
int net_nic_poll_once(void);
/* Separate fixed SFS/link queues (4 x 1514B each). Save IF/cli; if this queue
 * is empty poll/demux once; take a frame; restore IF. Never drive link here.
 * SFS reset discards only its own queue. */
int net_rx_take(u16 protocol, void *out, u32 cap, u32 *len);

/* net/nic.h: NetNicOps は段0のまま。id: AUTO=0, E82557=1, LGY98=2 */
enum NetNicId { NET_NIC_AUTO, NET_NIC_E82557, NET_NIC_LGY98 };
struct NetAdapter { int (*acquire)(const struct NetNicOps **out); void (*release)(void); };
int net_adapter_register(enum NetNicId id, const struct NetAdapter *adapter);
int net_start(enum NetNicId choice, const u8 peer[6]); void net_unbind(void);
int net_tx(const void *frame, u32 len); int net_mac(u8 out[6]); u32 net_rx_credit_bytes(void);
extern const struct NetAdapter net_lgy98_adapter, net_e82557_adapter;

#define NET_RX_SCAN_MAX 8u
/* Logical protocols, not EtherTypes. Empty AGAIN; insufficient cap NOSPC
 * sets required length without consuming; unknown protocol INVAL. */
enum NetRxProtocol { NET_RX_SFS = 1, NET_RX_LINK = 2 };
void net_rx_reset(u16 protocol); void net_tick(void);
/* AUTO prefers E82557. Missing explicit adapter: NOSYS; double start: BUSY.
 * task acquire owns the module reference; unbind stops tick/queues with IF
 * saved, then releases in task context. Borrowed ops live until release.
 * tx accepts the whole Ethernet frame without FCS, never waits (0/AGAIN/
 * HOSTDOWN). peer=NULL leaves peer selection to each protocol. */

#endif /* OS32_NET_NIC_H */
