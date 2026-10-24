#ifndef FS_SFS_LAN_H
#define FS_SFS_LAN_H

#include "sfs_client.h"
#include "net_eth.h"

#define SFS_LAN_ETHERTYPE OS32_ETHERTYPE
#define SFS_LAN_MAC_LEN ETH_ALEN
#define SFS_LAN_HDR_LEN ETH_HLEN
#define SFS_LAN_MIN_FRAME ETH_ZLEN
#define SFS_LAN_MAX_FRAME (SFS_LAN_HDR_LEN + SFS_BUFFER_FRAME)
#define SFS_LAN_RX_CAP 1536
#define SFS_LAN_RX_BUDGET 8
#define SFS_LAN_PUT_MS 1000
#define SFS_LAN_BAUD 115200

/* Foreground only. These three signatures accept e82557_* directly in stage 2.
 * send: 0 = queued, AGAIN/BUSY = retry, other negative = failed.
 * recv: 0 = frame, AGAIN = empty; always supply the full RX_CAP buffer. */
typedef struct {
    int (*send)(const void *buf, u32 len);
    int (*recv)(void *buf, u32 cap, u32 *len);
    void (*mac)(u8 *mac);
} SfsLanNic;

typedef struct {
    const SfsLanNic *nic;
    /* Only now/idle/can_wait are used; no UART put/get is called. */
    const SfsIo *clock;
    void *clock_ctx;
    u8 mac[SFS_LAN_MAC_LEN];
    u8 peer[SFS_LAN_MAC_LEN];
    u8 linked;
    u8 peer_fixed;
    u8 hello;
    u8 prepared;
    u32 nonce;
    u32 rx_pos, rx_end;
    u8 rx[SFS_LAN_RX_CAP];
    u8 tx[SFS_LAN_MAX_FRAME];
} SfsLan;

void sfs_lan_init(SfsLan *lan, const SfsLanNic *nic,
                  const SfsIo *clock, void *clock_ctx);
/* Optional configured peer, before HELLO. Empty string restores discovery. */
int sfs_lan_set_peer(SfsLan *lan, const char *mac);
/* Discard partial frames/peer and finitely drain queued NIC frames. */
void sfs_lan_reset(SfsLan *lan);
extern const SfsIo sfs_lan_io;

#endif
