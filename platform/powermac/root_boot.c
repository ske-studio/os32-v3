#include "root_boot.h"
#include "fs/vfs_device.h"
#include "fs/isofs/iso9660.h"
#include "fs/ramfs/ramfs.h"

int pm_root_boot(const struct boot_root_image *image)
{
    static const struct vfs_device_name names[] = {
        {"rd", VFS_NAME_DIGIT_PREFIX, VFS_DEV_RAM, 0, 2},
    };
    int rc;

    vfs_device_names_register(names, 1);
    rc = vfs_register_fs(&iso9660_ops);
    if (rc) return rc;
    rc = boot_root_mount(image);
    if (rc) return rc;
    rc = vfs_register_fs(&ramfs_ops);
    if (rc) return rc;
    return vfs_mount("/tmp", "rd1", "ramfs");
}
