/* Internal lexical registry. Rows remain resident; register before use.
 * Rebinding the same table is allowed. VFS init does not reset it. */
#ifndef VFS_DEVICE_H
#define VFS_DEVICE_H
#include "vfs.h"
enum vfs_name_mode { VFS_NAME_DIGIT_PREFIX, VFS_NAME_PREFIX, VFS_NAME_EXACT };
struct vfs_device_name {
    const char *prefix;
    int mode, type, first, count;
};
void vfs_device_names_register(const struct vfs_device_name *rows, unsigned n);
#endif
