#include "root_boot.h"
#include "fs/vfs_device.h"
#include "kernel/dev/dev_name.h"
#include "lib/path.h"

int pm_root_boot(const struct boot_root_image *image)
{
    static const struct vfs_device_name names[] = {
        {"rd", VFS_NAME_DIGIT_PREFIX, VFS_DEV_RAM, 0, 2},
#if KBOOT_SH
        {"COM1", VFS_NAME_EXACT, VFS_DEV_SERIAL, 0, 1},
#endif
    };
    int rc;

    vfs_device_names_register(names, sizeof(names) / sizeof(names[0]));
    rc = boot_root_fs_init();
    if (rc) return rc;
    rc = boot_root_start(image);
    if (rc) return rc;
    path_init();
    path_set_device_validator(dev_name_present);
    return path_set_drive(vfs_devname("/"));
}
