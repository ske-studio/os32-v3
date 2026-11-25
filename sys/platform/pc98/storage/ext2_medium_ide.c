#include "../../../fs/ide_limits.h"
#include "../../../fs/ext2/ext2_priv.h"
#include "ext2_medium_contract.h"
#include "ide_contract.h"
#include "platform_device.h"
#include "bootinfo.h"   /* BOOTINFO_GEOM_BIOS */
#include "kprintf.h"

#include "pc98pt.h"
#include "platform_ext2.h"

#define EXT2_DEVICE_NAME_CAP 8

/* ======================================================================== */
/*  パーティションテーブル解析                                               */
/* ======================================================================== */

Device *ext2_dev_for(int ide_drive)
{
    char devname[EXT2_DEVICE_NAME_CAP];
    devname[0] = 'h';
    devname[1] = 'd';
    devname[2] = (char)('0' + (ide_drive & 0xFF));
    devname[3] = '\0';
    return dev_find(devname);
}

/* 区画表を読む共通部。geom_src (NULL 可) にどちらの幾何で CHS を LBA にしたか
 * (BOOTINFO_GEOM_BIOS / BOOTINFO_GEOM_IDENTIFY) を返す。legacy (NULL 可) は
 * NOPART のとき「旧配置の OS32 項目があった」なら 1。 */
static int ext2_find_partition_ex(int ide_drive, u32 *out_start, u32 *out_len,
                                  int *geom_src, int *legacy)
{
    u8 pt_sect[PC98PT_SECTOR_SIZE];
    u16 heads, spt;
    unsigned long start, len;
    struct platform_disk_geometry info;
    Device *dev;
    int src;

    if (legacy) *legacy = 0;
    if (!out_start || !out_len) return EXT2_ERR_INVAL;
    if (platform_disk_geometry_get(ide_drive, &info) != IDE_OK) return EXT2_ERR_IO;

    dev = ext2_dev_for(ide_drive);
    if (!dev) return EXT2_ERR_IO;

    /* 区画表 (LBA 1)。**読めなければ失敗** — 以前はここで LBA 1088 を仮定し、
     * format はその位置から書き始めた (F12)。 */
    if (dev_blk_read_lba(dev, PC98PT_LBA, 1, pt_sect) != 0) return EXT2_ERR_IO;

    /* 区画表の CHS は BIOS 幾何で書かれている (IPL と同じ)。ATA の IDENTIFY と
     * 違う機械がある (実機 8GB: BIOS 未測定、IDENTIFY 16/63。F4)。 */
    src = bootinfo_part_geom(ide_drive, &heads, &spt);
    if (src < 0) return EXT2_ERR_NOPART;
    if (geom_src) *geom_src = src;

    if (pc98pt_find_os32(pt_sect, heads, spt, info.total_sectors,
                         (int *)0, &start, &len) != PC98PT_OK) {
        if (legacy)
            *legacy = pc98pt_os32_is_legacy(pt_sect, heads, spt, info.total_sectors);
        return EXT2_ERR_NOPART;
    }

    *out_start = (u32)start;
    *out_len = (u32)len;
    return EXT2_OK;
}

int ext2_find_partition(int ide_drive, u32 *out_start, u32 *out_len)
{
    return ext2_find_partition_ex(ide_drive, out_start, out_len, (int *)0, (int *)0);
}

int ext2_find_partition_src(int ide_drive, u32 *out_start, u32 *out_len,
                            int *geom_src)
{
    return ext2_find_partition_ex(ide_drive, out_start, out_len, geom_src, (int *)0);
}

int platform_ext2_resolve(int ide_drive, Device **dev, u32 *base, u32 *len)
{
    int ret;
    if (!platform_ide_drive_present(ide_drive)) return EXT2_ERR_IO;

    /* Device API ポインタを取得 */
    *dev = ext2_dev_for(ide_drive);
    if (!*dev) return EXT2_ERR_IO;

    /* 区画表から (開始, 長さ) を得る。見つからない / 読めないならマウントしない */
    {
        int src = 0, legacy = 0;
        ret = ext2_find_partition_ex(ide_drive, base, len,
                                     &src, &legacy);
        if (ret == EXT2_ERR_NOPART && legacy) {
            /* v63 までの OS32 が書いた旧配置。v64 は読まない (票 段 1-4) ので、
             * NOPART だけでは直し方が分からない — 何をすればよいかを出す (Opus M2) */
            kprintf(KLOG_LEGACY_LIGHT_RED, "[EXT2] hd%d: partition table is in the pre-v64 OS32 layout; "
                          "migrate it (host: make nhd-migrate-pt) or reinstall\n",
                    ide_drive & FS_IDE_DRIVE_ID_MASK);
        }
        if (ret != EXT2_OK) return ret;
        /* どちらの幾何で区画の位置を決めたか (BIOS が無いと IDENTIFY に落ちる。m3) */
        kprintf(KLOG_ATTR, "[EXT2] hd%d: partition LBA %u +%u (CHS geometry from %s)\n",
                ide_drive & FS_IDE_DRIVE_ID_MASK, *base, *len,
                src == BOOTINFO_GEOM_BIOS ? "BIOS" : "IDENTIFY");
    }
    return EXT2_OK;
}

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
