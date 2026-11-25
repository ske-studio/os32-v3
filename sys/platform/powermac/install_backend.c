/* PowerMac V1 reservation backend. Keep the approved bytes through the final
 * readback so a preserved entry is checked just as strictly as a changed one. */
#include "install_backend.h"
#include "endian_le.h"
#include "sys/fs/ext2/ext2_layout.h"
#include "sys/fs/ext2/ext2_priv.h"
#include "install_apm.h"
#include "apm.h"
#include "sys/dev/ata/ide.h"
#include "ext2_medium_contract.h"
#include "sys/fs/ext2/ext2.h"
#include "kmalloc.h"
#include "kstring.h"
#include "lib/crc32_core.inc"

struct approved_table {
    u8 (*sector)[512];
    u32 count, changed[4], nchanged;
    struct pm_partition root;
};
static struct approved_table tables[IDE_MAX_DRIVES];
static u32 word(const u8 *p)
{
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static int present(int unit)
{
    return (unsigned)unit < IDE_MAX_DRIVES && ide_drive_present(unit);
}

static int capacity(int unit, u32 *size)
{
    IdeInfo info;
    if (!present(unit) || ide_get_info(unit, &info) || info.phys_sector_size != 512)
        return OS32_ERR_INVAL;
    *size = info.total_sectors;
    return 0;
}

static int identify(u32 unit, struct install_identity *id)
{
    IdeInfo info;
    char serial[IDE_SERIAL_LEN];
    if (!present(unit) || ide_install_identify(unit, &info, serial) || info.phys_sector_size != 512)
        return OS32_ERR_IO;
    *id = (struct install_identity){
        crc32_core_final(crc32_core_update(CRC32_INIT, serial, sizeof(serial))),
        info.total_sectors, info.phys_sector_size, 0
    };
    return 0;
}

static const InstallPlanPartitionV1 *partition(const InstallPlanV1 *p, u32 i)
{
    if (i == p->map.entry) return &p->map;
    if (i == p->boot_hfs.entry) return &p->boot_hfs;
    if (i == p->root_ext2.entry) return &p->root_ext2;
    return 0;
}

static int blank_media(const InstallPlanV1 *p, int retry)
{
    const u32 chunk = 32;
    u8 *block = kmalloc(chunk * 512);
    if (!block) return OS32_ERR_NOSPC;
    int rc = 0;
    for (u32 i = 0; !rc && i < p->capacity;) {
        u32 n = p->capacity-i < chunk ? p->capacity-i : chunk;
        rc = ide_install_read_many(p->raw_unit, i, n, block);
        for (u32 j = 0; !rc && j < n; ++j) {
            u32 lba = i+j;
            if (retry && ((lba >= p->root_ext2.start_lba && lba-p->root_ext2.start_lba < p->root_ext2.sectors) ||
                          (lba >= p->boot_hfs.start_lba && lba-p->boot_hfs.start_lba < p->boot_hfs.sectors)))
                continue;
            for (unsigned k = 0; k < 512; ++k)
                if (block[j*512+k]) { rc = OS32_ERR_INVAL; break; }
        }
        i += n;
    }
    kfree(block);
    return rc;
}

static int approve_entry(struct approved_table *t, const InstallPlanV1 *p, u32 i, int fresh)
{
    u8 *s = t->sector[i];
    const InstallPlanPartitionV1 *part = partition(p, i);
    if (!fresh) {
        u32 start = word(s+8), len = word(s+12);
        if (s[0] != 'P' || s[1] != 'M' || word(s+4) != t->count-1 ||
            !s[48] || !start || !len || start >= p->capacity || len > p->capacity-start)
            return OS32_ERR_INVAL;
        for (u32 j = 1; j < i; ++j) {
            u32 old = word(t->sector[j]+8), length = word(t->sector[j]+12);
            if (old < start+len && start < old+length) return OS32_ERR_INVAL;
        }
        if (!part) return kstrncmp((char *)s+48, pm_types[0], 32) ? 0 : OS32_ERR_INVAL;
        if (start != part->start_lba || len != part->sectors) return OS32_ERR_INVAL;
        if (!kstrncmp((char *)s+48, pm_types[part->type-1], 32)) return 0;
        if (part == &p->map || kstrncmp((char *)s+48, "Apple_Free", 32))
            return OS32_ERR_INVAL;
    }
    if (!part) return OS32_ERR_INVAL;
    pm_apm_entry(s, t->count-1, part);
    t->changed[t->nchanged++] = i;
    return 0;
}

static int validate(const InstallPlanV1 *p, int retry, u32 *changed)
{
    struct install_identity id;
    int rc = identify(p->raw_unit, &id);
    if (rc) return rc;
    u32 bound_unit;
    if (pm_install_view_unit(0, &bound_unit) && bound_unit != p->raw_unit)
        return OS32_ERR_BUSY;
    if (id.capacity != p->capacity || id.serial_crc != p->serial_crc ||
        p->map.entry != 1 || p->original_table_sectors > PM_APM_MAX_ENTRIES+1 ||
        !ide_range_ok(p->raw_unit, 0, p->capacity)) return OS32_ERR_INVAL;
    Ext2Layout layout;
    if (ext2_layout_plan(p->root_ext2.sectors, EXT2_MAX_GROUPS, &layout) != EXT2L_OK)
        return OS32_ERR_INVAL;
    struct approved_table *t = &tables[p->raw_unit];
    if (t->sector) kfree(t->sector);
    *t = (struct approved_table){0};
    int fresh = p->original_table_sectors == 2;
    if (fresh && (p->map.sectors != PM_MAP_SECTORS || p->boot_hfs.entry != 2 ||
                  p->boot_hfs.start_lba != PM_HFS_START || p->boot_hfs.sectors != PM_HFS_SECTORS ||
                  p->root_ext2.start_lba != PM_ROOT_START || p->root_ext2.entry != 3))
        return OS32_ERR_INVAL;
    t->count = fresh ? 4 : p->original_table_sectors;
    if (p->boot_hfs.entry >= t->count || p->root_ext2.entry >= t->count ||
        t->count-1 > p->map.sectors || p->lba0_sectors != (u32)fresh)
        return OS32_ERR_INVAL;
    t->sector = kmalloc(t->count * 512);
    if (!t->sector) return OS32_ERR_NOSPC;
    u32 crc = CRC32_INIT;
    for (u32 i = 0; i < p->original_table_sectors; ++i) {
        if (ide_install_read(p->raw_unit, i, t->sector[i])) return OS32_ERR_IO;
        crc = crc32_core_update(crc, t->sector[i], 512);
    }
    if (crc32_core_final(crc) != p->original_table_crc) return OS32_ERR_INVAL;
    u8 *ddr = t->sector[0];
    if (fresh) {
        rc = blank_media(p, retry);
        if (rc) return rc;
        pm_apm_header(ddr, p->capacity);
        t->changed[t->nchanged++] = 0;
    } else if (ddr[0] != 'E' || ddr[1] != 'R' || ddr[2] != 2 || ddr[3] ||
               word(ddr+4) != p->capacity) return OS32_ERR_INVAL;
    for (u32 i = 1; i < t->count; ++i) {
        rc = approve_entry(t, p, i, fresh);
        if (rc) return rc;
    }
    t->root = (struct pm_partition){p->root_ext2.start_lba, p->root_ext2.sectors};
    *changed = t->nchanged;
    return 0;
}

static int changed_lba(const InstallPlanV1 *p, u32 index, u32 *out)
{
    struct approved_table *t = &tables[p->raw_unit];
    if (index >= t->nchanged) return OS32_ERR_INVAL;
    *out = t->changed[index];
    return 0;
}

static int check_table(const InstallPlanV1 *p, u32 lba, u32 count, const void *buf)
{
    struct approved_table *t = &tables[p->raw_unit];
    if (lba >= t->count || count > t->count-lba ||
        memcmp(t->sector[lba], buf, count * 512)) return OS32_ERR_INVAL;
    return 0;
}

static int verify(const InstallPlanV1 *p)
{
    struct install_identity id;
    int rc = identify(p->raw_unit, &id);
    if (rc) return rc;
    if (id.serial_crc != p->serial_crc || id.capacity != p->capacity)
        return OS32_ERR_STALE;
    u8 s[512];
    for (u32 i = 0; i < tables[p->raw_unit].count; ++i) {
        if (ide_install_read(p->raw_unit, i, s)) return OS32_ERR_IO;
        if (check_table(p, i, 1, s)) return OS32_ERR_INVAL;
    }
    if (p->root_ext2.sectors <= EXT2_SUPER_LBA ||
        ide_install_read(p->raw_unit, p->root_ext2.start_lba+EXT2_SUPER_LBA, s))
        return OS32_ERR_IO;
    Ext2Layout layout;
    if (ext2_layout_plan(p->root_ext2.sectors, EXT2_MAX_GROUPS, &layout) != EXT2L_OK ||
        le16_rd(s + EXT2_SB_MAGIC_OFF) != EXT2_SUPER_MAGIC ||
        le32_rd(s + EXT2_SB_LOG_BLOCK_SIZE_OFF) != 0 ||
        le32_rd(s + EXT2_SB_BLOCKS_COUNT_OFF) != layout.total_blocks)
        return OS32_ERR_INVAL;
    return 0;
}

static int rescan(const InstallPlanV1 *p, u32 generation)
{
    int rc = verify(p);
    return rc ? rc : pm_install_bind(p, generation);
}

static int find_part(int unit, u32 *start, u32 *len)
{
    if (!present(unit) || !tables[unit].root.sectors) return EXT2_ERR_NOPART;
    *start = tables[unit].root.start;
    *len = tables[unit].root.sectors;
    return 0;
}

static int raw_read(Device *dev, u32 lba, int count, void *out)
{
    return ide_read_sectors(dev->bus_id, lba, count, out);
}

static int raw_write(Device *dev, int lba, int count, const void *in)
{
    return ide_write_sectors(dev->bus_id, (u32)lba, count, in);
}

static Device *format_device(int unit)
{
    /* Private raw descriptors have physical coordinates, whereas the
     * registered hd0 descriptor starts at the partition's first sector. */
    static Device raw[IDE_MAX_DRIVES];
    u32 size;
    if (capacity(unit, &size)) return 0;
    raw[unit] = (Device){
        .type = DEV_BLOCK, .bus_type = DEV_BUS_IDE, .bus_id = unit,
        .sect_size = 512, .total_sects = size,
        .blk_read_u32 = raw_read, .blk_write = raw_write
    };
    return &raw[unit];
}

void pm_format_register(void)
{
    static const struct ext2_medium_ops ops = {
        .present = present, .total_sectors = capacity, .find_part = find_part,
        .range_ok = ide_range_ok, .dev = format_device, .after_write = ext2_read_cache_reset, .min_lba = 1
    };
    ext2_medium_register(EXT2_MEDIUM_HD, &ops);
}

const struct install_medium *platform_install_medium(void)
{
    static const struct install_medium ops = {
        .requires_raw_reservation = 1, .is_view = pm_install_is_view,
        .identify = identify, .validate = validate, .changed_lba = changed_lba,
        .check_table = check_table, .view_unit = pm_install_view_unit,
        .resolve = pm_install_resolve, .read = ide_install_read, .write = ide_install_write,
        .drain = ide_install_flush, .sync = ide_install_flush, .rescan = rescan,
        .verify = verify, .invalidate = pm_install_invalidate, .publish = pm_install_publish
    };
    return &ops;
}
