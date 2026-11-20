#include "root_boot.h"
#include "fs/vfs_device.h"
#include "kernel/dev/dev_name.h"
#include "lib/path.h"

#if OS32_HOOK_IME
#include "fs/vfs.h"
static int dict_ready, learning_enabled;
int pm_root_dict_ready(void) { return dict_ready; }
int pm_root_learning_enabled(void) { return learning_enabled; }
#endif

int pm_root_boot(const struct boot_root_image *image)
{
    static const struct vfs_device_name names[] = {
        {"rd", VFS_NAME_DIGIT_PREFIX, VFS_DEV_RAM, 0, 4},
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
    rc = path_set_drive(vfs_devname("/"));
#if OS32_HOOK_IME
    if (!rc) {
        dict_ready = vfs_mount("/db", "rd2", "ramfs") == 0;
        learning_enabled = vfs_mount("/dbu", "rd3", "ramfs") == 0;
    }
#endif
    return rc;
}
