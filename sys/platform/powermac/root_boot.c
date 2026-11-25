#include "root_boot.h"
#if OS32_FEATURE_IDE
#include "kprintf.h"
#include "apm.h"
#include "platform_ext2.h"
#endif
#include "sys/fs/vfs.h"
#include "sys/kern/module/module.h"
#include "sys/fs/vfs_device.h"
#include "sys/kern/dev/dev_name.h"
#include "lib/path.h"


#if OS32_FEATURE_IDE
int platform_ext2_vfs_mount(Ext2Ctx *ctx, int drive)
{
    int rc = ext2_mount(ctx, drive);
    if (rc) kprintf(KLOG_ATTR, "HDD root hd%d ext2_mount rc=%d\r\n", drive, rc);
    return rc;
}
#endif

int pm_root_boot(const struct boot_root_image *image, int *registered)
{
    static const struct vfs_device_name names[] = {
#if OS32_FEATURE_IDE
        {"cd", VFS_NAME_DIGIT_PREFIX, VFS_DEV_CD, 0, 1},
        {"hd", VFS_NAME_DIGIT_PREFIX, VFS_DEV_HD, 0, 1},
#endif
        {"rd", VFS_NAME_DIGIT_PREFIX, VFS_DEV_RAM, 0, 4},
#if KBOOT_SH
        {"COM1", VFS_NAME_EXACT, VFS_DEV_SERIAL, 0, 1},
#endif
    };
    int rc;

    *registered = 0;
    vfs_device_names_register(names, sizeof(names) / sizeof(names[0]));
#if OS32_FEATURE_IDE
    if (!image) {
        rc = ext2_init();
        if (!rc) rc = vfs_mount("/", "hd0", "ext2");
        if (!rc) pm_partition_enable_writes();
    } else
#endif
    {
        rc = boot_root_fs_init();
#if OS32_FEATURE_IDE
        if (!rc) rc = ext2_init();
#endif
        if (!rc) rc = boot_root_start_registered(image, registered);
    }
    if (rc) return rc;
    path_init();
    path_set_device_validator(dev_name_present);
    rc = path_set_drive(vfs_devname("/"));
#if OS32_FEATURE_IDE
    if (rc && !image) kprintf(KLOG_ATTR, "HDD root hd0 path rc=%d\r\n", rc);
#endif
#if PM_LOAD_KDEBUG
    if (!rc) kdebug_load();
#endif
    return rc;
}
