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

int platform_ide_range_ok(int drv, u32 lba, u32 count)
{
    return ide_range_ok(drv, lba, count);
}

int platform_disk_geometry_get(int drv, struct platform_disk_geometry *out)
{
    IdeInfo info;
    int rc = ide_get_info(drv, &info);
    if (rc) return rc;
    out->total_sectors = info.total_sectors;
    out->cylinders = info.cylinders;
    out->heads = info.heads;
    out->sectors = info.sectors;
    out->phys_sector_size = info.phys_sector_size;
    return rc;
}
