/* NET_HOSTSVC_DESIGN addendum: shared declarations, implementation in later stages. */
#ifndef OS32_DRIVERS_BLOCK_NETCD_H
#define OS32_DRIVERS_BLOCK_NETCD_H
#include "../base/dev.h"
#include "../../fs/serialfs/sfs_block.h"

struct NetCd { u32 handle, media_gen, last_use, alive_seen; u32 size[2], mtime[2]; };
int netcd_register(const char *alias); int netcd_read(Device *dev, u32 lba, int count, void *buf);
int netcd_media_check(Device *dev, u32 *generation);

/* Private Device.priv owns NetCd, alias copy and SfsBlock. register reserves
 * cd0 without communication (EXIST if occupied). BREAD splits to <=2 sectors
 * under one deadline; renewed sid never sends an old handle. */

#endif
