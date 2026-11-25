#include "boot_root.h"
#include "sys/kern/dev/ram_ro.h"
#include "sys/fs/vfs.h"
#include "sys/fs/isofs/iso9660.h"
#include "sys/fs/ramfs/ramfs.h"

static int mount_registered(const struct boot_root_image *image, int *registered)
{
    static struct ram_ro root;
    int rc = ram_ro_register(&root, image);

    *registered = !rc;
    if (rc) return rc;
    return vfs_mount("/", "rd0", "iso9660");
}

int boot_root_mount(const struct boot_root_image *image)
{
    int registered;
    return mount_registered(image, &registered);
}

int boot_root_fs_init(void)
{
    return vfs_register_fs(&iso9660_ops);
}

int boot_root_start_registered(const struct boot_root_image *image, int *registered)
{
    int rc = mount_registered(image, registered);

    if (rc) return rc;
    rc = vfs_register_fs(&ramfs_ops);
    if (rc) return rc;
    return vfs_mount("/tmp", "rd1", "ramfs");
}

int boot_root_start(const struct boot_root_image *image)
{
    int registered;
    return boot_root_start_registered(image, &registered);
}
