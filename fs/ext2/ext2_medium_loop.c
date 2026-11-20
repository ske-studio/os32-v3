#include "ext2.h"
#include "dev.h"
#include "loop_contract.h"
#include "ext2_medium_contract.h"

static int loop_present(int drv)
{
    return loop_dev_status(drv - 0x0600, 0, 0);
}

static int loop_total(int drv, u32 *out)
{
    return loop_dev_status(drv - 0x0600, out, 0) ? EXT2_OK : EXT2_ERR_IO;
}

static int loop_extent(int drv, u32 *start, u32 *len)
{
    int bps;
    int slot = drv - 0x0600;
    if (!loop_dev_status(slot, len, &bps)) return EXT2_ERR_IO;
    if (bps != 512 || !loop_dev_lba_writable(slot)) return EXT2_ERR_INVAL;
    *start = 0;
    return EXT2_OK;
}

static int loop_range(int drv, u32 lba, u32 count)
{
    u32 start, len;
    return loop_extent(drv, &start, &len) == EXT2_OK &&
           lba < len && count <= len - lba;
}

static int loop_read(Device *dev, int lba, int count, void *buf)
{
    return loop_dev_read_lba(*(const int *)dev->priv, lba, count, buf);
}

static int loop_write(Device *dev, int lba, int count, const void *buf)
{
    return loop_dev_write_lba(*(const int *)dev->priv, lba, count, buf);
}

static Device *loop_device(int drv)
{
    /* priv は slot の番号を指す (整数を void * に詰めない — check_p2v の physical-cast を避ける) */
    static const int slots[4] = { 0, 1, 2, 3 };  /* lo0〜lo3 (drv 0x0600..0x0603) */
    static Device dev = { .blk_read = loop_read, .blk_write = loop_write };
    dev.priv = (void *)&slots[drv - 0x0600];
    return &dev;
}

const struct ext2_medium_ops ext2_medium_loop_ops = {
    .present = loop_present,
    .total_sectors = loop_total,
    .find_part = loop_extent,
    .range_ok = loop_range,
    .dev = loop_device,
    .min_lba = 0,
};
