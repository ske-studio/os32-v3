#include "boot_root.h"
#include "drivers/block/ram_ro.h"
#include "fs/vfs.h"

int boot_root_mount(const struct boot_root_image *image)
{
    static struct ram_ro root;
    int rc = ram_ro_register(&root, image);

    if (rc) return rc;
    return vfs_mount("/", "rd0", "iso9660");
}
