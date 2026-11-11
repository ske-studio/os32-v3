#ifndef RAMFS_H
#define RAMFS_H

#include "fs/vfs.h"

enum { RAMFS_FILES = 16, RAMFS_FILE_MAX = 1024,
       RAMFS_DATA_MAX = 16 * 1024, RAMFS_META_MAX = 4 * 1024 };
extern VfsOps ramfs_ops;

#endif
