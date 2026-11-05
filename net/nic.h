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
/* Separate fixed SFS/link queues (4 x 1514B each). Save IF/cli; if this queue
 * is empty poll/demux once; take a frame; restore IF. Never drive link here.
 * SFS reset discards only its own queue. */
int net_rx_take(u16 protocol, void *out, u32 cap, u32 *len);

#endif /* OS32_NET_NIC_H */
