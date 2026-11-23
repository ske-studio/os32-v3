#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "crc32.h"
#include "ide_addr.h"
#include "dev.h"
#include "ext2.h"
#include "ext2_ctx.h"
void ext2_meta_touch(Ext2Ctx *ctx);
void ext2_read_cache_reset(void) {}
void kprintf(u8 level, const char *fmt, ...) { (void)level; (void)fmt; }
#include "vfs_device.h"
#include "install_access.h"
#include "install_management.h"

static int owner = 1, authorized = 1, busy, published, corrupt, retry_seen;
enum { CAPACITY = 24 };
static u8 disk[CAPACITY][512], saved[sizeof(disk)], table[5][512];
static int mounted, reclaim_mount, reclaim_sync, view_io_error, registering_view;
static unsigned irq_depth;
unsigned test_irq_save(void) { return irq_depth++; }
void test_irq_restore(unsigned depth) { assert(irq_depth == depth + 1); irq_depth = depth; }
static int existing, sync_error, flush_pending, fail_restore_write;
static u8 queued_sector[512];
static const char serial[20] = "MOCK V1 DISK";
static void big32(u8 *dest, unsigned n)
{
    dest[0] = n >> 24;
    dest[1] = n >> 16;
    dest[2] = n >> 8;
    dest[3] = n;
}
static void entry(unsigned n, unsigned start, unsigned len, const char *type)
{
    u8 *out = table[n];
    out[0] = 'P';
    out[1] = 'M';
    big32(out + 4, existing ? 4 : 3);
    big32(out + 8, start);
    big32(out + 12, len);
    strcpy((char *)out + 48, type);
}
static void setup_media(InstallPlanV1 *p)
{
    entry(1, 1, 4, "Apple_partition_map");
    entry(2, 5, 3, "Apple_HFS");
    entry(3, 8, 8, "Apple_UNIX_SVR2");
    if (existing) {
        entry(4, 16, 8, "Apple_HFS");
        memcpy(disk, table, sizeof(table));
        memset(disk[2] + 48, 0, 32);
        strcpy((char *)disk[2] + 48, "Apple_Free");
        memset(disk[3] + 48, 0, 32);
        strcpy((char *)disk[3] + 48, "Apple_Free");
        for (unsigned i = 16; i < CAPACITY; ++i)
            for (unsigned j = 0; j < 512; ++j) disk[i][j] = i + j;
    }
    p->original_table_sectors = existing ? 5 : 2;
    p->serial_crc = crc32_calc(serial, sizeof(serial));
    p->original_table_crc = crc32_calc(disk, p->original_table_sectors * 512);
}
static u32 original_lba[] = {0, 1, 2, 3};
static u32 media_generation = 1;
static int identify(u32 unit, struct install_identity *out)
{
    if (unit) return OS32_ERR_INVAL;
    *out = (struct install_identity){ crc32_calc(serial, sizeof(serial)),
                                      CAPACITY, 512, media_generation };
    return 0;
}
static unsigned load_be32(const u8 *p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) |
           ((unsigned)p[2] << 8) | p[3];
}

static int old_apm_valid(void)
{
    unsigned ends[4], starts[4];
    for (unsigned i = 0; i < 4; i++) {
        const u8 *e = disk[i + 1];
        unsigned start = load_be32(e + 8), length = load_be32(e + 12);
        if (e[0] != 'P' || e[1] != 'M' || load_be32(e + 4) != 4 ||
            !length || start > CAPACITY || length > CAPACITY - start)
            return 0;
        starts[i] = start;
        ends[i] = start + length;
        for (unsigned j = 0; j < i; j++)
            if (start < ends[j] && starts[j] < ends[i]) return 0;
        if ((i == 1 || i == 2) && memcmp(e + 48, "Apple_Free", 11)) return 0;
    }
    return !memcmp(disk[1], table[1], 512) && !memcmp(disk[4], table[4], 512);
}
static int validate(const InstallPlanV1 *p, int retry, u32 *n)
{
    retry_seen = retry;
    *n = sizeof(original_lba) / sizeof(original_lba[0]);
    if (p->capacity != CAPACITY || p->serial_crc != crc32_calc(serial, sizeof(serial)) ||
        p->original_table_sectors != (existing ? 5u : 2u) ||
        p->original_table_crc != crc32_calc(disk, p->original_table_sectors * 512))
        return OS32_ERR_INVAL;
    if (p->map.start_lba != 1 || p->map.sectors != 4 || p->map.entry != 1 ||
        p->boot_hfs.start_lba != 5 || p->boot_hfs.sectors != 3 || p->boot_hfs.entry != 2 ||
        p->root_ext2.start_lba != 8 || p->root_ext2.sectors != 8 || p->root_ext2.entry != 3)
        return OS32_ERR_INVAL;
    if (!existing && !retry)
        for (unsigned i = 0; i < sizeof(disk); i++)
            if (((u8 *)disk)[i]) return OS32_ERR_INVAL;
    if (existing && !old_apm_valid()) return OS32_ERR_INVAL;
    return 0;
}
static int locate(const InstallPlanV1 *p, u32 i, u32 *lba)
{
    (void)p;
    *lba = original_lba[i];
    return 0;
}
static int check_table(const InstallPlanV1 *p, u32 lba, u32 count, const void *bytes)
{
    (void)p;
    return lba <= 3 && count <= 4 - lba && !memcmp(table[lba], bytes, count * 512)
        ? 0 : OS32_ERR_INVAL;
}
static u32 view_generation;
static Device raw, view;
static int view_unit(unsigned logical, u32 *unit)
{
    if (logical > 1) return 0;
    *unit = logical;
    return 1;
}
static int resolve(struct _Device *dev, u32 lba, u32 count, u32 *unit, u32 *physical, u32 *gen)
{
    if (reclaim_sync && lba == 2 && --reclaim_sync == 0) {
        install_reclaim_owner(owner);
        install_retry_recovery();
        assert(mounted == 1);
    }
    if (dev == &raw) {
        *unit = 0;
        *physical = lba;
        *gen = 0;
        return 1;
    }
    if (lba > 8 || count > 8 - lba) return OS32_ERR_INVAL;
    *unit = 0;
    *physical = lba + 8;
    *gen = dev && dev->priv ? *(u32 *)dev->priv : view_generation;
    return 1;
}
static int read_sector(u32 unit, u32 lba, void *buf)
{
    assert(!unit && lba < CAPACITY);
    if (view_io_error && lba == 8) return OS32_ERR_IO;
    memcpy(buf, disk[lba], 512);
    if (corrupt && lba == 2) ((u8 *)buf)[511] ^= 1;
    return 0;
}
static int write_sector(u32 unit, u32 lba, u32 count, const void *buf)
{
    assert(!unit && lba < CAPACITY);
    if (view_io_error && lba == 8) return OS32_ERR_IO;
    if (fail_restore_write && !authorized && lba == 1) return OS32_ERR_IO;
    assert(count <= CAPACITY - lba);
    memcpy(disk[lba], buf, count * 512);
    return 0;
}
/* Test the public single/multiple-sector and CHS entry points with only the
 * final PIO command replaced. No port access or emulated guest is involved. */
static int drive_present[IDE_MAX_DRIVES] = {1};
static IdeGeom drive_geom[IDE_MAX_DRIVES] = {
    { .valid = 1, .w49 = IDE_ID_CAP_LBA, .total = CAPACITY }
};
static IdeInfo drive_info[IDE_MAX_DRIVES];
static void ide_chs_addr(int drive, u16 cyl, u8 head, u8 sect, IdeAddr *a)
{
    (void)drive;
    assert(cyl == 0 && head == 0 && sect);
    a->sect_num = sect - 1;
}
static int ide_pio_command(int drive, const IdeAddr *a, u32 count, void *dst, const void *src)
{
    assert(drive == 0 && a->sect_num < CAPACITY && count <= CAPACITY - a->sect_num);
    if (src) memcpy(disk[a->sect_num], src, count * 512);
    else memcpy(dst, disk[a->sect_num], count * 512);
    return IDE_OK;
}
#include "drivers/ata/ide_request.inc"

static int sync_disk(u32 unit)
{
    assert(!unit);
    if (sync_error) { sync_error = 0; return OS32_ERR_IO; }
    return 0;
}
static int verify(const InstallPlanV1 *p)
{
    return p->raw_unit || memcmp(disk, table, sizeof(table)) ||
           disk[8][0] != 0x53 ? OS32_ERR_IO : 0;
}
static int rescan(const InstallPlanV1 *p, u32 gen)
{
    int rc = verify(p);
    if (!rc) view_generation = gen;
    return rc;
}
static void invalidate(u32 unit) { assert(!unit); }
static void publish(u32 unit) { assert(!unit); ++published; }
static struct install_medium backend = {
    .identify = identify, .validate = validate, .changed_lba = locate, .check_table = check_table,
    .view_unit = view_unit, .resolve = resolve, .read = read_sector, .write = write_sector, .sync = sync_disk, .drain = sync_disk,
    .rescan = rescan, .verify = verify, .invalidate = invalidate, .publish = publish
};
const struct install_medium *platform_install_medium(void) { return &backend; }
int exec_disk_write_allowed(void) { return authorized; }
int res_owner_get(void) { return owner; }
static void *mount_fs(int dev)
{
    (void)dev;
    mounted++;
    if (reclaim_mount) install_reclaim_owner(owner);
    return &mounted;
}
static void unmount_fs(void *ctx) { assert(ctx == &mounted); mounted--; }
void vfs_fd_invalidate_mount(void *ctx) { assert(ctx == &mounted); }
int vfs_fd_mount_busy(void *ctx) { assert(ctx == &mounted); return busy; }
int kstrcmp(const char *a, const char *b) { return strcmp(a, b); }
int kstrlen(const char *s) { return strlen(s); }
char *kstrncpy(char *d, const char *s, u32 n)
{
    if (registering_view && !strcmp(s, "/hd0")) assert(irq_depth > 0);
    if (strlen(s) >= n) abort();
    return strcpy(d, s);
}
static int raw_write(Device *d, int lba, int count, const void *src)
{
    assert(d == &raw && count == 1);
    memcpy(disk[lba], src, 512);
    return 0;
}
static int sync_fs(void *ctx)
{
    int old_owner = owner, old_authorized = authorized, handled;
    if (!flush_pending) return 0;
    owner = 0;
    authorized = 0;
    assert(!install_dirty_transfer(ctx, &view, 7, 1, 0, queued_sector, &handled) && handled);
    assert(install_dirty_transfer(ctx, &view, 7, 2, 0, queued_sector, &handled) < 0 && handled);
    install_dirty_finish(ctx);
    owner = old_owner;
    authorized = old_authorized;
    flush_pending = 0;
    return 0;
}
static void setup_mounts(void)
{
    static const struct vfs_device_name names[] = {
        { "hd", VFS_NAME_DIGIT_PREFIX, VFS_DEV_HD, 0, 2 },
    };
    static VfsOps ops = { .name = "ext2", .mount = mount_fs, .umount = unmount_fs, .sync = sync_fs };
    vfs_device_names_register(names, 1);
    assert(!vfs_register_fs(&ops));
    raw.blk_write = raw_write;
}

void *kmalloc(u32 n) { return malloc(n); }
void kfree(void *p) { free(p); }

int main(int argc, char **argv)
{
    InstallPlanV1 p = { .version = 1, .size = 128, .sector_bytes = 512,
        .capacity = CAPACITY, .map = {1,1,4,1},
        .boot_hfs = {2,5,3,2}, .root_ext2 = {3,8,8,3}, .lba0_sectors = 1 };
    struct install_access a, format;
    u32 h = 99, old;
    u8 sector[512] = {93};
    existing = argc == 2 && !strcmp(argv[1], "existing");
    setup_media(&p);
    setup_mounts();
    backend.requires_raw_reservation = 1;
    memcpy(saved, disk, sizeof(disk));
    int chs = ide_write_sector_chs(0, 0, 0, 7, sector);
    assert(!memcmp(saved, disk, sizeof(disk)));
    assert(chs == OS32_ERR_BUSY);
    assert(!ide_read_sector_chs(0, 0, 0, 7, sector));
    sector[0] = 93;
    backend.requires_raw_reservation = 0;
    assert(install_format_begin(&format, 0, 8, 8) < 0);
    assert(!dev_blk_write_lba(&raw, 6, 1, sector));
    assert(!memcmp(disk[6], sector, 512));
    assert(!ide_write_sector(0, 7, sector));
    assert(!memcmp(disk[7], sector, 512));
    memset(sector, 0, sizeof(sector));
    assert(!ide_write_sectors(0, 7, 1, sector));
    assert(!dev_blk_write_lba(&raw, 6, 1, sector));
    sector[0] = 0x53;
    memcpy(saved, disk, sizeof(disk));
    assert(!vfs_mount("/", "hd0", "ext2"));
    assert(install_begin(&p, &h) == OS32_ERR_BUSY && h == 99);
    vfs_umount("/");
    assert(!vfs_mount("/old", "hd0", "ext2"));
    assert(!install_admit(&a, 0, INSTALL_WRITE, 0, 16, sector));
    assert(install_begin(&p, &h) == OS32_ERR_BUSY && h == 99);
    install_complete(&a);
    busy = 1;
    assert(install_begin(&p, &h) == OS32_ERR_BUSY && h == 99);
    busy = 0;
    /* A caller-supplied CRC matching corrupt media cannot authorize a plan. */
    unsigned bad_lba = existing ? 1 : 7;
    disk[bad_lba][0] ^= 1;
    unsigned original_crc = p.original_table_crc;
    p.original_table_crc = crc32_calc(disk, p.original_table_sectors * 512);
    assert(install_begin(&p, &h) < 0 && h == 99);
    disk[bad_lba][0] ^= 1;
    p.original_table_crc = original_crc;
    assert(!memcmp(saved, disk, sizeof(disk)));
    p.serial_crc++;
    assert(install_begin(&p, &h) < 0 && h == 99);
    p.serial_crc--;
    p.root_ext2.sectors++;
    assert(install_begin(&p, &h) < 0 && h == 99);
    p.root_ext2.sectors--;
    assert(!memcmp(saved, disk, sizeof(disk)));
    assert(!install_begin(&p, &h));
    assert(install_end(h, 2) == OS32_ERR_INVAL);
    owner++;
    assert(dev_blk_write_lba(&raw, 6, 1, sector) < 0);
    assert(ide_write_sector(0, 6, sector) < 0);
    assert(ide_write_sectors(IDE_MAX_DRIVES, 6, 1, sector) < 0);
    assert(ide_write_sector_chs(0, 0, 0, 7, sector) < 0);
    assert(vfs_mount("/hd0", "hd0", "ext2") < 0);
    assert(install_admit(&a, 0, INSTALL_WRITE, 8, 1, sector) < 0);
    assert(install_admit(&a, 0, INSTALL_MOUNT, 0, 0, 0) < 0);
    assert(install_end(h, OS32_INSTALL_END_ABORT) < 0);
    owner--;
    assert(!memcmp(saved, disk, sizeof(disk)));
    owner++;
    assert(install_format_begin(&format, 0, 8, 8) < 0);
    owner--;
    assert(install_format_begin(&format, 1, 8, 8) < 0);
    assert(install_format_begin(&format, 0, 9, 8) < 0);
    assert(install_format_begin(&format, 0, 8, 7) < 0);
    assert(!memcmp(saved, disk, sizeof(disk)));
    assert(!install_format_begin(&format, 0, 8, 8));
    assert(install_admit(&a, 0, INSTALL_WRITE, 15, 1, sector) < 0);
    authorized = 0;
    assert(install_transfer_allowed(&format, 15, 1));
    assert(!install_transfer_allowed(&format, 15, 2));
    authorized = 1;
    assert(install_admit(&a, 0, INSTALL_WRITE, 15, 2, sector) < 0);
    assert(!install_format_finish(&format, 0));
    assert(dev_blk_write_lba(&raw, 1, 1, sector) < 0);
    assert(!dev_blk_write_lba(&raw, 2, 1, table[2]));
    corrupt = 1;
    authorized = 0;
    install_reclaim_owner(owner);
    authorized = 1;
    old = h;
    assert(install_begin(&p, &h) == OS32_ERR_BUSY);
    corrupt = 0;
    authorized = 0;
    fail_restore_write = 1;
    install_retry_recovery();
    fail_restore_write = 0;
    authorized = 1;
    assert(install_begin(&p, &h) == OS32_ERR_BUSY);
    install_retry_recovery();
    assert(!memcmp(saved, disk, sizeof(disk)) && !published);
    assert(!install_begin(&p, &h) && h != old && retry_seen);
    assert(install_rescan(old) < 0);
    assert(!install_format_begin(&format, 0, 8, 8));
    assert(!install_format_transfer(&format, 8, 1, 0, sector));
    assert(!install_format_finish(&format, 0));
    assert(!install_end(h, OS32_INSTALL_END_ABORT));
    if (argc == 2 && !strcmp(argv[1], "swap")) {
        media_generation++;
        assert(install_begin(&p, &h) < 0);
        media_generation--;
        assert(install_begin(&p, &h) < 0);
        return 0;
    }
    assert(!install_begin(&p, &h) && retry_seen);
    assert(!install_format_begin(&format, 0, 8, 8));
    sync_error = 1;
    assert(install_format_finish(&format, 0) == OS32_ERR_IO);
    assert(!published && !memcmp(saved, disk, 5 * 512));
    assert(!install_begin(&p, &h));
    assert(!install_format_begin(&format, 0, 8, 8));
    assert(!install_format_finish(&format, 0));
    assert(!dev_blk_write_lba(&raw, 1, 3, table[1]));
    assert(!install_rescan(h) && !install_rescan(h));
    owner++;
    assert(!install_mount_visible(0));
    owner--;
    assert(install_mount_visible(0));
    for (int race = 0; race < 4; race++) {
        if (race == 0) {
            reclaim_mount = 1;
            assert(vfs_mount("/hd0", "hd0", "ext2") == OS32_ERR_STALE);
            reclaim_mount = 0;
            assert(!mounted);
        } else if (race == 1) {
            registering_view = 1;
            assert(!vfs_mount("/hd0", "hd0", "ext2"));
            registering_view = 0;
            Ext2Ctx ctx = { .dev = &view, .mounted = 1, .part_len = 8 };
            ext2_meta_touch(&ctx);
            /* Interrupt between the superblock read and its writeback. */
            reclaim_sync = 2;
            assert(ext2_sync(&ctx) != 0);
            assert(!reclaim_sync && mounted);
        } else {
            view_io_error = 1;
            if (race == 2)
                assert(dev_blk_write_lba(&view, 0, 1, sector) == OS32_ERR_IO);
            else
                assert(dev_blk_read_lba(&view, 0, 1, sector) == OS32_ERR_IO);
            view_io_error = 0;
            assert(!install_mount_visible(0));
        }
        install_retry_recovery();
        assert(!mounted && !published && !memcmp(saved, disk, 5 * 512));
        assert(!install_begin(&p, &h) && retry_seen);
        assert(!install_format_begin(&format, 0, 8, 8));
        assert(!install_format_finish(&format, 0));
        assert(!dev_blk_write_lba(&raw, 1, 3, table[1]));
        assert(!install_rescan(h));
    }
    view_generation = h;
    registering_view = 1;
    assert(!vfs_mount("/hd0", "hd0", "ext2"));
    registering_view = 0;
    assert(!dev_blk_write_lba(&view, 0, 1, sector));
    assert(dev_blk_write_lba(&view, 7, 2, sector) < 0);
    owner++;
    assert(vfs_umount_checked("/hd0") == OS32_ERR_BUSY && mounted);
    assert(!vfs_install_visible(&mounted));
    assert(dev_blk_write_lba(&view, 0, 1, sector) < 0);
    owner--;
    u32 stale_generation = old;
    Device stale = view;
    stale.priv = &stale_generation;
    assert(dev_blk_write_lba(&stale, 0, 1, sector) == OS32_ERR_STALE);
    install_dirty_begin(&mounted, &view, 0, 8);
    memset(queued_sector, 0x84, sizeof(queued_sector));
    flush_pending = 1;
    assert(!install_end(h, OS32_INSTALL_END_COMMIT));
    assert(!mounted && !flush_pending && !memcmp(disk[15], queued_sector, 512));
    assert(published == 1 && install_end(h, OS32_INSTALL_END_COMMIT) < 0);
    assert(!memcmp(saved + 16 * 512, disk[16], 8 * 512));
    return 0;
}
