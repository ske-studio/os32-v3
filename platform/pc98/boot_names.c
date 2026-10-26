/* PC-9821 lexical names, independent of the live Device registry. */
#include "boot_names.h"
#include "../../fs/vfs_device.h"
#include "../../drivers/ide.h"

#define PC98_FD_NAME_UNITS VFS_DEV_UNIT_MAX
#define PC98_SERIAL_NAME_ID 1
#define PC98_SINGLE_NAME_COUNT 1
STATIC_ASSERT(IDE_MAX_DRIVES == VFS_DEV_UNIT_MAX, vfs_ide_unit_limit);
static const struct vfs_device_name pc98_device_names[] = {
    {"hd", VFS_NAME_DIGIT_PREFIX, VFS_DEV_HD, 0, IDE_MAX_DRIVES},
    {"fd", VFS_NAME_DIGIT_PREFIX, VFS_DEV_FD, 0, PC98_FD_NAME_UNITS},
    {"COM", VFS_NAME_PREFIX, VFS_DEV_SERIAL, PC98_SERIAL_NAME_ID, PC98_SINGLE_NAME_COUNT},
    {"cd", VFS_NAME_DIGIT_PREFIX, VFS_DEV_CD, 0, IDE_MAX_DRIVES},
    {"hostdrv", VFS_NAME_EXACT, VFS_DEV_HOSTDRV, 0, PC98_SINGLE_NAME_COUNT},
};
void pc98_boot_names_bind(void)
{
    vfs_device_names_register(pc98_device_names,
                             sizeof(pc98_device_names) / sizeof(pc98_device_names[0]));
}
