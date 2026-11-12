#include "root_boot.h"
#include "fs/vfs_device.h"

int pm_root_boot(const struct boot_root_image *image)
{
    static const struct vfs_device_name names[] = {
        {"rd", VFS_NAME_DIGIT_PREFIX, VFS_DEV_RAM, 0, 2},
    };
    int rc;

    vfs_device_names_register(names, 1);
    rc = boot_root_fs_init();
    if (rc) return rc;
    return boot_root_start(image);
}
