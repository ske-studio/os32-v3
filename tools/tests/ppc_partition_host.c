#include "apm.h"
#include "platform_ext2.h"
#include "ide_contract.h"
#include "fs/ext2/ext2.h"
#include "kstring.h"

static u8 disk[24][512], saved[sizeof(disk)];
static Device *registered;
static int fail_sector = -1;

int ide_read_sectors(int unit, u32 lba, u32 count, void *buf)
{
    if (unit != 1 || lba >= 24 || count > 24 - lba || (int)lba == fail_sector) return -1;
    kmemcpy(buf, disk[lba], 512 * count);
    return 0;
}

int ide_read_sector(int unit, u32 lba, void *buf)
{
    return ide_read_sectors(unit, lba, 1, buf);
}

int ide_write_sectors(int unit, u32 lba, u32 count, const void *buf)
{
    if (unit != 1 || lba >= 24 || count > 24 - lba) return -1;
    kmemcpy(disk[lba], buf, 512 * count);
    return 0;
}

int dev_register(Device *dev) { registered = dev; return 0; }
Device *dev_find(const char *name) { return kstrcmp(name, "hd0") ? 0 : registered; }

static void entry(int i, int start, int length, const char *type)
{
    u8 *s = disk[i];
    s[0] = 'P'; s[1] = 'M'; s[7] = 3; s[11] = start; s[15] = length;
    kstrcpy((char *)s + 48, type);
}

int main(void)
{
    struct pm_partition p;
    Device *d;
    u32 base, len;
    u8 buf[1024];
    disk[0][0] = 'E'; disk[0][1] = 'R'; disk[0][2] = 2; disk[0][7] = 24;
    entry(1, 1, 3, "Apple_partition_map");
    entry(2, 8, 4, "Apple_UNIX_SVR2");
    entry(3, 14, 4, "Apple_HFS");
    fail_sector = 3;
    if (pm_apm_root(1, 24, 2, &p) != EXT2_ERR_IO) return 1;
    fail_sector = -1;
    disk[3][11] = 11;
    if (pm_apm_root(1, 24, 2, &p) != EXT2_ERR_NOPART) return 2;
    disk[3][11] = 23;
    if (pm_apm_root(1, 24, 2, &p) != EXT2_ERR_NOPART) return 3;
    disk[3][11] = 14;
    if (pm_apm_root(1, 24, 2, &p) || pm_partition_register(1, &p) ||
        platform_ext2_resolve(0, &d, &base, &len) || base || len != 4) return 4;
    disk[8][0] = 81;
    disk[11][511] = 119;
    if (d->blk_read_u32(d, 0, 1, buf) || buf[0] != 81) return 5;
    if (d->blk_read_u32(d, 3, 1, buf) || buf[511] != 119) return 6;
    kmemcpy(saved, disk, sizeof(disk));
    if (!d->blk_write(d, 3, 2, buf) || !d->blk_read_u32(d, U32_MAX, 1, buf)) return 7;
    kmemset(buf, 42, 512);
    if (d->blk_write(d, 3, 1, buf)) return 8;
    kmemset(saved + 11 * 512, 42, 512);
    return memcmp(saved, disk, sizeof(disk)) ? 9 : 0;
}

void _start(void)
{
    int status = main();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(status) : "memory");
    __builtin_unreachable();
}
