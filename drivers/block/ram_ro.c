#include "ram_ro.h"
#include "lib/kstring.h"
#include "os32_kapi_shared.h"

#define RAM_RO_SECTOR_SIZE 2048U

int ram_ro_read(struct ram_ro *ram, u32 lba, u32 count, void *dst)
{
    u32 total = ram->bytes / RAM_RO_SECTOR_SIZE;
    if (lba > total || count > total - lba) return -1;
    kmemcpy(dst, ram->base + lba * RAM_RO_SECTOR_SIZE,
            count * RAM_RO_SECTOR_SIZE);
    return 0;
}

static int ram_ro_dev_read(Device *dev, u32 lba, int count, void *dst)
{
    return ram_ro_read(dev->priv, lba, (u32)count, dst);
}

static int ram_ro_dev_write(Device *dev, int lba, int count, const void *src)
{
    (void)dev; (void)lba; (void)count; (void)src;
    return OS32_ERR_ROFS;
}

int ram_ro_register(struct ram_ro *ram, const struct boot_root_image *image)
{
    ram->base = image->base;
    ram->bytes = image->bytes;
    kmemset(&ram->dev, 0, sizeof(ram->dev));
    ram->dev.name = "rd0";
    ram->dev.type = DEV_BLOCK;
    ram->dev.bus_type = DEV_BUS_NONE;
    ram->dev.sect_size = RAM_RO_SECTOR_SIZE;
    ram->dev.total_sects = image->bytes / RAM_RO_SECTOR_SIZE;
    ram->dev.blk_read_u32 = ram_ro_dev_read;
    ram->dev.blk_write = ram_ro_dev_write;
    ram->dev.priv = ram;
    return dev_register(&ram->dev);
}
