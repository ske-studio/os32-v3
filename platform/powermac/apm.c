/* Apple Partition Map entries use big-endian block counts and LBAs.
 * Apple TN1189, "How the ROM Loads SCSI and ATA Drivers".
 * FreeBSD sys/geom/part/g_part_apm.c, g_part_apm_probe/read. */
#include "apm.h"
#if PM_PRODUCT_BOOT
#include "product_boot.h"
#endif
#include "ide_contract.h"
#include "fs/ext2/ext2.h"
#include "kstring.h"

static u32 apm_word(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

int pm_apm_root(int unit, u32 capacity, u32 number, struct pm_partition *root)
{
    /* The firmware map normally reserves 63 sectors. Bound boot storage. */
    struct pm_partition parts[PM_APM_MAX_ENTRIES];
    u8 block[512];
    u32 count = 1;
    if (!number || capacity < 2) return EXT2_ERR_NOPART;
    if (ide_read_sector(unit, 0, block)) return EXT2_ERR_IO;
    if (block[0] != 'E' || block[1] != 'R' || block[2] != 2 || block[3] ||
        apm_word(block + 4) != capacity) return EXT2_ERR_NOPART;
    for (u32 i = 0; i < count; ++i) {
        if (ide_read_sector(unit, i + 1, block)) return EXT2_ERR_IO;
        if (block[0] != 'P' || block[1] != 'M' || !block[48]) return EXT2_ERR_NOPART;
        u32 entries = apm_word(block + 4);
        if (!i) count = entries;
        if (count != entries || count < number || count >= capacity ||
            count > sizeof(parts) / sizeof(parts[0])) return EXT2_ERR_NOPART;
        u32 start = apm_word(block + 8), sectors = apm_word(block + 12);
        if (!start || !sectors || start >= capacity || sectors > capacity - start)
            return EXT2_ERR_NOPART;
#if PM_PRODUCT_BOOT
        if (pm_expected_plan.boot_partition == i+1 &&
            kstrncmp((char *)block+48, "Apple_HFS", 32)) return EXT2_ERR_NOPART;
        if (pm_expected_plan.boot_partition > count) return EXT2_ERR_NOPART;
#endif
        int is_map = !kstrncmp((char *)block + 48, "Apple_partition_map", 32);
        if (is_map != (i == 0) || (is_map && (start != 1 || sectors < count)))
            return EXT2_ERR_NOPART;
        if (i + 1 == number && kstrncmp((char *)block + 48, "Apple_UNIX_SVR2", 32))
            return EXT2_ERR_NOPART;
        for (u32 j = 0; j < i; ++j)
            if (start < parts[j].start + parts[j].sectors && parts[j].start < start + sectors)
                return EXT2_ERR_NOPART;
        parts[i] = (struct pm_partition){start, sectors};
    }
    struct pm_partition selected = parts[number - 1];
    if (selected.sectors < EXT2_MIN_PART_SECTORS) return EXT2_ERR_NOPART;
    if (ide_read_sector(unit, selected.start + EXT2_SUPER_LBA, block)) return EXT2_ERR_IO;
    if (block[EXT2_SB_MAGIC_OFF] != (EXT2_SUPER_MAGIC & 0xff) ||
        block[EXT2_SB_MAGIC_OFF + 1] != (EXT2_SUPER_MAGIC >> 8)) return EXT2_ERR_MAGIC;
    *root = selected;
    return EXT2_OK;
}
