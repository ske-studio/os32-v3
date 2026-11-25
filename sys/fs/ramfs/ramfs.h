#ifndef RAMFS_H
#define RAMFS_H

#include "sys/fs/vfs.h"

#define VFS_SQLITE_RAMFS 2

enum { RAMFS_FILES = 16, RAMFS_GROW = 1024, RAMFS_FILE_MAX = 32 * 1024,
       RAMFS_DATA_MAX = 64 * 1024, RAMFS_META_MAX = 4 * 1024 };
extern VfsOps ramfs_ops;

#endif
