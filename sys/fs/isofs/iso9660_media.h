/* NET_HOSTSVC_DESIGN addendum: shared declarations, implementation in later stages. */
#ifndef OS32_FS_ISOFS_ISO9660_MEDIA_H
#define OS32_FS_ISOFS_ISO9660_MEDIA_H
#include "types.h"

int iso9660_resource_check(void *ctx, u32 *generation); int iso9660_resource_refresh(void *ctx);
void iso9660_media_drop(void *ctx);

#endif
