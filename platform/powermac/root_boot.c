#include "root_boot.h"
#if OS32_FEATURE_IDE
#include "kprintf.h"
#include "fs/ext2/ext2.h"
#endif
#include "fs/vfs.h"
#include "kernel/module/module.h"
#include "fs/vfs_device.h"
#include "kernel/dev/dev_name.h"
#include "lib/path.h"

#if OS32_HOOK_IME
static int dict_ready, learning_enabled;
int pm_root_dict_ready(void) { return dict_ready; }
int pm_root_learning_enabled(void) { return learning_enabled; }
#endif

int pm_root_boot(const struct boot_root_image *image, int *registered)
{
    static const struct vfs_device_name names[] = {
#if OS32_FEATURE_IDE
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
        if (rc) kprintf(KLOG_ATTR, "HDD root hd0 ext2 rc=%d\r\n", rc);
    } else
#endif
    {
        rc = boot_root_fs_init();
        if (!rc) rc = boot_root_start_registered(image, registered);
    }
    if (rc) return rc;
    path_init();
    path_set_device_validator(dev_name_present);
    rc = path_set_drive(vfs_devname("/"));
#if OS32_FEATURE_IDE
    if (rc && !image) kprintf(KLOG_ATTR, "HDD root hd0 path rc=%d\r\n", rc);
#endif
#if OS32_HOOK_IME
    if (!rc && image) {
        dict_ready = vfs_mount("/db", "rd2", "ramfs") == 0;
        learning_enabled = vfs_mount("/dbu", "rd3", "ramfs") == 0;
    }
#endif
#if PM_LOAD_KDEBUG
    if (!rc) kdebug_load();
#endif
    return rc;
}
