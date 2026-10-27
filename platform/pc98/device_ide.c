#include "platform_device.h"
#include "ide.h"

void platform_ide_init(void)
{
    (void)ide_init();
}

int platform_ide_drive_present(int drv)
{
    return ide_drive_present(drv);
}

int platform_ide_identify(int drv, void *info)
{
    return ide_identify(drv, (IdeInfo *)info);
}

int platform_ide_read_sector(int drv, u32 lba, void *buf)
{
    return ide_read_sector(drv, lba, buf);
}

int platform_ide_write_sector(int drv, u32 lba, const void *buf)
{
    return ide_write_sector(drv, lba, buf);
}

int platform_ide_write_sectors(int drv, u32 lba, u32 cnt, const void *buf)
{
    return ide_write_sectors(drv, lba, cnt, buf);
}

int platform_ide_get_info(int drv, void *info)
{
    return ide_get_info(drv, (IdeInfo *)info);
}
