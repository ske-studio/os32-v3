#include "boot_root.h"
#include "drivers/block/ram_ro.h"
#include "fs/vfs.h"
#include "fs/isofs/iso9660.h"
#include "fs/ramfs/ramfs.h"

int boot_root_mount(const struct boot_root_image *image)
{
    static struct ram_ro root;
    int rc = ram_ro_register(&root, image);

    if (rc) return rc;
    return vfs_mount("/", "rd0", "iso9660");
}

int boot_root_fs_init(void)
{
    return vfs_register_fs(&iso9660_ops);
}

int boot_root_start(const struct boot_root_image *image)
{
    int rc = boot_root_mount(image);

    if (rc) return rc;
    rc = vfs_register_fs(&ramfs_ops);
    if (rc) return rc;
    return vfs_mount("/tmp", "rd1", "ramfs");
}
