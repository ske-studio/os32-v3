#include "apm.h"
#include "platform_ext2.h"
#include "ide_contract.h"
#include "fs/ext2/ext2_priv.h"
#include "kprintf.h"
#include "kstring.h"

static u8 disk[24][512], saved[sizeof(disk)];
static Device *registered;
static int fail_sector = -1;

static char events[32];
static unsigned event_count;
static int tracing, logged_rc;
static Ext2Ctx ctx;

static void event(char value)
{
    if (tracing && event_count < sizeof(events) - 1)
        events[event_count++] = value;
}

void ext2_read_cache_reset(void) { event(ctx.mounted ? 'M' : 'R'); }

void kprintf(u8 attr, const char *fmt, ...)
{
    if (!kstrcmp(fmt, "HDD root hd%d ext2_mount rc=%d\r\n")) {
        __builtin_va_list ap;
        __builtin_va_start(ap, fmt);
        int drive = __builtin_va_arg(ap, int);
        if (!drive) logged_rc = __builtin_va_arg(ap, int);
        __builtin_va_end(ap);
        event('D');
    }
}

int dev_blk_read_lba(Device *dev, u32 lba, int count, void *buf)
{
    return dev->blk_read_u32(dev, lba, count, buf);
}

int dev_blk_write_lba(Device *dev, u32 lba, int count, const void *buf)
{
    return dev->blk_write(dev, lba, count, buf);
}

int ide_read_sectors(int unit, u32 lba, u32 count, void *buf)
{
    event((int)lba == fail_sector ? 'F' : 'S');
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

int ide_install_write(u32 unit, u32 lba, u32 count, const void *buf)
{
    return ide_write_sectors(unit, lba, count, buf);
}

int dev_register(Device *dev) { registered = dev; return 0; }
Device *dev_find(const char *name) { event('V'); return kstrcmp(name, "hd0") ? 0 : registered; }

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
    entry(2, 8, 6, "Apple_UNIX_SVR2");
    entry(3, 14, 4, "Apple_HFS");
    fail_sector = 3;
    if (pm_apm_root(1, 24, 2, &p) != EXT2_ERR_IO) return 1;
    fail_sector = -1;
    disk[3][11] = 11;
    if (pm_apm_root(1, 24, 2, &p) != EXT2_ERR_NOPART) return 2;
    disk[3][11] = 23;
    if (pm_apm_root(1, 24, 2, &p) != EXT2_ERR_NOPART) return 3;
    disk[3][11] = 14;
    if (pm_partition_register(1, 24, 2) != EXT2_ERR_MAGIC || registered) return 10;
    disk[10][56] = disk[16][56] = 0x53;
    disk[10][57] = disk[16][57] = 0xef;
    if (pm_partition_register(1, 24, 3) != EXT2_ERR_NOPART || registered) return 11;
    if (pm_partition_register(1, 24, 2) ||
        platform_ext2_resolve(0, &d, &base, &len) || base || len != 6) return 4;
    kmemset(buf, 42, sizeof(buf));
    kmemcpy(saved, disk, sizeof(disk));
    if (!d->blk_write(d, 0, 1, buf)) return 12;
    if (memcmp(saved, disk, sizeof(disk))) return 13;
    pm_partition_enable_writes();
    disk[8][0] = 81;
    disk[13][511] = 119;
    if (d->blk_read_u32(d, 0, 1, buf) || buf[0] != 81) return 5;
    if (d->blk_read_u32(d, 5, 1, buf) || buf[511] != 119) return 6;
    kmemcpy(saved, disk, sizeof(disk));
    if (!d->blk_write(d, 5, 2, buf) || !d->blk_read_u32(d, U32_MAX, 1, buf)) return 7;
    kmemset(buf, 42, 512);
    if (d->blk_write(d, 5, 1, buf)) return 8;
    kmemset(saved + 13 * 512, 42, 512);
    if (memcmp(saved, disk, sizeof(disk))) return 9;

    disk[10][EXT2_SB_BLOCKS_COUNT_OFF] = 4; /* four blocks in six sectors */
    disk[10][EXT2_SB_BLOCKS_PER_GROUP_OFF] = 8;
    if (platform_ext2_vfs_mount(&ctx, 0) != EXT2_ERR_NOPART ||
        logged_rc != EXT2_ERR_NOPART || ctx.mounted) return 14;

    disk[10][EXT2_SB_BLOCKS_COUNT_OFF] = 3;
    if (platform_ext2_vfs_mount(&ctx, 0) || !ctx.mounted) return 15;
    fail_sector = 12; /* first group descriptor, after both superblock sectors */
    tracing = 1;
    if (platform_ext2_vfs_mount(&ctx, 0) != EXT2_ERR_IO ||
        logged_rc != EXT2_ERR_IO || ctx.mounted) return 16;
    /* Remount and unmount resets, resolver, superblock, failed descriptor,
     * raw and metadata error resets, then the boot diagnostic. */
    return kstrcmp(events, "MMVSSFRRD") ? 17 : 0;
}

void _start(void)
{
    int status = main();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(status) : "memory");
    __builtin_unreachable();
}
