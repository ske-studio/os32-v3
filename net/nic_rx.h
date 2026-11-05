/* NET_HOSTSVC_DESIGN addendum: shared declarations, implementation in later stages. */
#ifndef OS32_NET_NIC_RX_H
#define OS32_NET_NIC_RX_H
#include "nic.h"

/* A2 -> A3, IF=0; borrowed ops. Scan at most NET_RX_SCAN_MAX frames.
 * Check Ethernet length/type/destination before the three payload bytes:
 * n>=3 && p[0]==0x05 && p[1]==0x53 && p[2]==0x46 selects SFS; else link.
 * peer=NULL: do not filter sender here; SFS locks to its first HELLO sender
 * and accepts only its own destination. Only link HELLO accepts broadcast.
 * With a fixed peer, check that sender too. Full queue drops its new frame
 * and continues scanning the other protocol. */
void net_rx_demux(const struct NetNicOps *ops, const u8 local[6], const u8 peer[6]);

#endif
