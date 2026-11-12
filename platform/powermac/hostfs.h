/* PowerMac PLN1 HostFS transport; peer and boot ownership join in stage D. */
#ifndef POWERMAC_HOSTFS_H
#define POWERMAC_HOSTFS_H

#include "lan_boot.h"
#include "fs/serialfs/sfs_client.h"

struct pm_hfs_transport {
    u32 tb_hz;
    u8 rx[PLN1_FS_FRAME_MAX];
    u32 rx_pos, rx_end;
};
extern struct pm_hfs_transport pm_hostfs_transport;
extern const SfsIo pm_hostfs_io;

int pm_hfs_set_clock(u32 tb_hz);
/* Exactly one complete SFS frame within the PLN1 declared payload length.
 * Reject malformed frames and preserve an unread response. */
int pm_hfs_rx_frame(const u8 *frame, u32 len);
void pm_hfs_receive(const struct lan_header *h, const u8 src[6], const u8 *payload);
int pm_lan_fs_running(void);
int pm_lan_fs_send(const u8 *frame, u32 len);

#endif
