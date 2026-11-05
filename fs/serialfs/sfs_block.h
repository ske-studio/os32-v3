/* NET_HOSTSVC_DESIGN addendum: shared declarations, implementation in later stages. */
#ifndef OS32_FS_SERIALFS_SFS_BLOCK_H
#define OS32_FS_SERIALFS_SFS_BLOCK_H
#include "sfs_client.h"

/* 新 fs/serialfs/sfs_block.h */
struct SfsBlock { u32 sid, handle, size[2], mtime[2]; };
int sfs_bopen(const char *alias, u16 len, struct SfsBlock *out, u32 deadline);
int sfs_bread(const struct SfsBlock *block, u32 lba, u16 count, void *out, u32 cap, u32 deadline);
int sfs_bclose(const struct SfsBlock *block, u32 deadline);

#endif
