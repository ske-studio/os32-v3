#include "../ide_limits.h"
#include "ext2_priv.h"
#include "ext2_medium_contract.h"
#include "ide_contract.h"
#include "platform_device.h"
#include "bootinfo.h"   /* BOOTINFO_GEOM_BIOS */
#include "kprintf.h"

static int ide_total_sectors(int drv, u32 *out)
{
    struct platform_disk_geometry info;
    if (platform_disk_geometry_get(drv, &info) != IDE_OK) return EXT2_ERR_IO;
    *out = info.total_sectors;
    return EXT2_OK;
}

static int ide_find_part(int ide_drive, u32 *start, u32 *len)
{
    int src = 0;
    int ret = ext2_find_partition_src(ide_drive, start, len, &src);
    if (ret != EXT2_OK) return ret;
    /* 区画表の位置を IDENTIFY の幾何で読んだだけのときは書かない (m3)。
     * BIOS の幾何と違えば別の場所を指している (F4)。 */
    if (src != BOOTINFO_GEOM_BIOS) {
        kprintf(KLOG_LEGACY_LIGHT_RED, "[EXT2] format: no BIOS geometry for hd%d; "
                      "refusing to write\n", ide_drive & FS_IDE_DRIVE_ID_MASK);
        return EXT2_ERR_INVAL;
    }
    return EXT2_OK;
}

const struct ext2_medium_ops ext2_medium_ide_ops = {
    .present = platform_ide_drive_present,
    .total_sectors = ide_total_sectors,
    .find_part = ide_find_part,
    .range_ok = platform_ide_range_ok,
    .dev = ext2_dev_for,
    .after_write = ext2_read_cache_reset,
    .min_lba = EXT2_FMT_MIN_LBA,
};
