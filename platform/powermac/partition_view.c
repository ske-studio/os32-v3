#if PM_PRODUCT_BOOT
#include "product_boot.h"
#endif
#include "apm.h"
#include "platform_ext2.h"
#include "drivers/ata/ide.h"
#include "fs/ext2/ext2.h"

static u32 first;
static Device view;
static Device *current = &view;
static int writable;

static int view_lba(Device *dev, u32 offset, int count, u32 *lba)
{
    if (dev != current || count <= 0 || offset > dev->total_sects || (u32)count > dev->total_sects - offset)
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
    if (!writable || dev != current) return IDE_ERR_RANGE;
    int rc = view_lba(dev, (u32)offset, count, &lba);
    if (rc) return rc;
#if OS32_FEATURE_IDE
    return ide_install_write(dev->bus_id, lba, count, buf);
#else
    return ide_write_sectors(dev->bus_id, lba, count, buf);
#endif
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
    writable = current == &view && dev_find(view.name) == &view;
}

int platform_ext2_resolve(int logical_drive, Device **dev, u32 *base, u32 *len)
{
    if (logical_drive || !current || dev_find(view.name) != current) return EXT2_ERR_NOPART;
    *dev = current;
    *base = 0;
    *len = current->total_sects;
    return EXT2_OK;
}

#if OS32_FEATURE_IDE
#include "install_backend.h"
#include "kmalloc.h"

static u32 view_generation;

int pm_install_view_unit(unsigned logical, u32 *unit)
{
    if (logical || !current || dev_find("hd0") != current) return 0;
    *unit = current->bus_id;
    return 1;
}

int pm_install_is_view(Device *dev)
{
    return dev == current;
}

int pm_install_resolve(Device *dev, u32 lba, u32 count, u32 *unit,
                       u32 *physical, u32 *generation)
{
    if (!dev || dev->bus_type != DEV_BUS_IDE) return 0;
    if (dev->blk_read_u32 == view_read && dev != current) return OS32_ERR_STALE;
    if (lba > dev->total_sects || count > dev->total_sects-lba) return OS32_ERR_INVAL;
    *unit = dev->bus_id;
    *physical = dev == current ? first+lba : lba;
    *generation = dev == current ? view_generation : 0;
    return 1;
}

void pm_install_invalidate(u32 unit)
{
    if (!current || current->bus_id != (int)unit || dev_find("hd0") != current) return;
    dev_unregister(current);
    current = 0;
    writable = 0;
}

int pm_install_bind(const InstallPlanV1 *p, u32 generation)
{
    Device *registered = dev_find("hd0");
    if (registered)
        return registered == current && current->bus_id == (int)p->raw_unit &&
               view_generation == generation ? 0 : OS32_ERR_BUSY;
    /* Device pointers can survive unregistration. Each generation gets a
     * distinct identity so old pointers always resolve as stale. */
    Device *next = kmalloc(sizeof(*next));
    if (!next) return OS32_ERR_NOSPC;
    *next = view;
    next->bus_id = p->raw_unit;
    next->total_sects = p->root_ext2.sectors;
    if (dev_register(next)) {
        kfree(next);
        return OS32_ERR_BUSY;
    }
    first = p->root_ext2.start_lba;
    view_generation = generation;
    current = next;
    writable = 0;
    return 0;
}

void pm_install_publish(u32 unit)
{
    (void)unit;
    writable = 0; /* Published installation views are read-only. */
}
#endif
