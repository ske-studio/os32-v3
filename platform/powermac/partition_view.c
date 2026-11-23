#if PM_PRODUCT_BOOT
#include "product_boot.h"
#endif
#include "apm.h"
#include "platform_ext2.h"
#include "drivers/ata/ide.h"
#include "fs/ext2/ext2.h"

static u32 first;
static Device view;
static int writable;

static int view_lba(Device *dev, u32 offset, int count, u32 *lba)
{
    if (count <= 0 || offset > dev->total_sects || (u32)count > dev->total_sects - offset)
        return IDE_ERR_RANGE;
    *lba = first + offset;
    return IDE_OK;
}

static int view_read(Device *dev, u32 offset, int count, void *buf)
{
    u32 lba;
    int rc = view_lba(dev, offset, count, &lba);
    return rc ? rc : ide_read_sectors(dev->bus_id, lba, count, buf);
}

static int view_write(Device *dev, int offset, int count, const void *buf)
{
    u32 lba;
    if (!writable || dev != &view) return IDE_ERR_RANGE;
    int rc = view_lba(dev, (u32)offset, count, &lba);
    return rc ? rc : ide_write_sectors(dev->bus_id, lba, count, buf);
}

static Device view = {
    .name = "hd0", .type = DEV_BLOCK, .bus_type = DEV_BUS_IDE, .sect_size = 512,
    .blk_read_u32 = view_read, .blk_write = view_write
};

int pm_partition_register(int unit, u32 capacity, u32 number)
{
    if (dev_find(view.name)) return EXT2_ERR_INVAL;
    struct pm_partition part;
    int rc = pm_apm_root(unit, capacity, number, &part);
    if (rc) return rc;
#if PM_PRODUCT_BOOT
    if (number != pm_expected_plan.root_partition || part.start != pm_expected_plan.root_start ||
        part.sectors != pm_expected_plan.root_sectors) return EXT2_ERR_INVAL;
#endif
    first = part.start;
    view.bus_id = unit;
    view.total_sects = part.sectors;
    return dev_register(&view);
}

void pm_partition_enable_writes(void)
{
    /* Stage 3a has no installer reservation authorizing a product write. */
#if !PM_PRODUCT_BOOT
    writable = dev_find(view.name) == &view;
#endif
}

int platform_ext2_resolve(int logical_drive, Device **dev, u32 *base, u32 *len)
{
    if (logical_drive || dev_find(view.name) != &view) return EXT2_ERR_NOPART;
    *dev = &view;
    *base = 0;
    *len = view.total_sects;
    return EXT2_OK;
}
