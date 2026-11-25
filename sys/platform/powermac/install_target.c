#include "sys/include/platform_install_boot.h"
#include "sys/include/ide_contract.h"
#include "install_apm.h"
#include "apm.h"
#include "lib/crc32_core.inc"

static u32 handle;

static u32 map_word(const u8 *p)
{
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static int has_type(const u8 *block, const char *type)
{
    unsigned i;
    for (i = 0; type[i]; ++i)
        if (block[48+i] != (u8)type[i]) return 0;
    return !block[48+i];
}

/* Reuse the two OS32 slots in an existing map. Other entries, including
 * their table bytes, are left alone. begin independently checks this plan. */
static int inspect_map(KernelAPI *api, HdprepGeom *g, u8 *block, u32 *crc)
{
    InstallPlanV1 *p = &g->media;
    struct pm_partition seen[PM_APM_MAX_ENTRIES];
    u32 count = 1;
    if (block[0] != 'E' || block[1] != 'R' || block[2] != 2 || block[3] ||
        map_word(block+4) != p->capacity) return HDPREP_E_PT_USED;
    for (u32 i = 0; i < count; ++i) {
        if (api->ide_read_sector(p->raw_unit, i+1, block)) return OS32_ERR_IO;
        *crc = crc32_core_update(*crc, block, 512);
        if (!i) count = map_word(block+4);
        u32 start = map_word(block+8), n = map_word(block+12);
        if (block[0] != 'P' || block[1] != 'M' || !block[48] ||
            count != map_word(block+4) || count < 3 || count > PM_APM_MAX_ENTRIES ||
            !start || !n || start >= p->capacity || n > p->capacity-start)
            return HDPREP_E_PT_USED;
        for (u32 j = 0; j < i; ++j)
            if (start < seen[j].start + seen[j].sectors && seen[j].start < start+n)
                return HDPREP_E_PT_USED;
        seen[i] = (struct pm_partition){start,n};
        if (has_type(block, pm_types[0]) != (i == 0)) return HDPREP_E_PT_USED;
        if (i == 0) {
            if (start != 1 || n < count) return HDPREP_E_PT_USED;
            p->map = (InstallPlanPartitionV1){1, start, n, 1};
        } else if (i <= 2) {
            if (!has_type(block, pm_types[i])) {
                if (!has_type(block, "Apple_Free")) return HDPREP_E_PT_USED;
                g->table_writes |= 1u << (i+1);
            }
            InstallPlanPartitionV1 part = {i+1, start, n, i+1};
            if (i == 1) {
                if (n != PM_HFS_SECTORS) return HDPREP_E_PT_USED;
                p->boot_hfs = part;
            } else p->root_ext2 = part;
        }
    }
    p->original_table_sectors = count+1;
    p->lba0_sectors = 0;
    return 0;
}

static int inspect(KernelAPI *api, HdprepGeom *g)
{
    IdeInfo info;
    u8 block[512];
    int selected = -1;
    for (int i = 0; i < IDE_MAX_DRIVES; ++i) {
        if (!api->ide_drive_present(i)) continue;
        if (selected >= 0) return OS32_ERR_BUSY;
        selected = i;
    }
    if (selected < 0 || api->ide_identify(selected, &info) || info.phys_sector_size != 512)
        return OS32_ERR_IO;
    for (int i = IDE_SERIAL_LEN-1; i >= 0 && !info.serial[i]; --i) info.serial[i] = ' ';
    g->drive = selected;
    g->table_writes = 0;
    g->media = (InstallPlanV1){
        .version = 1, .size = sizeof(g->media), .raw_unit = selected,
        .capacity = info.total_sectors, .sector_bytes = 512, .original_table_sectors = 2,
        .serial_crc = crc32_core_final(crc32_core_update(CRC32_INIT, info.serial, IDE_SERIAL_LEN)),
        .map = {1, 1, PM_MAP_SECTORS, 1}, .boot_hfs = {2, PM_HFS_START, PM_HFS_SECTORS, 2}, .lba0_sectors = 1
    };
    if (api->ide_read_sector(selected, 0, block)) return OS32_ERR_IO;
    u32 crc = crc32_core_update(CRC32_INIT, block, sizeof(block));
    if (block[0] == 'E') {
        int rc = inspect_map(api, g, block, &crc);
        if (rc) return rc;
    } else {
        g->table_writes = 0xf;
        for (unsigned i = 0; i < 2; ++i) {
            if (i) {
                if (api->ide_read_sector(selected, 1, block)) return OS32_ERR_IO;
                crc = crc32_core_update(crc, block, sizeof(block));
            }
            for (unsigned j = 0; j < sizeof(block); ++j)
                if (block[j]) return HDPREP_E_PT_USED;
        }
    }
    g->media.original_table_crc = crc32_core_final(crc);
    return 0;
}

int hdprep_plan(const HdprepGeom *g, unsigned long mb, HdprepPlan *out)
{
    u32 start = g->media.boot_hfs.start_lba + g->media.boot_hfs.sectors;
    if (g->media.root_ext2.entry) start = g->media.root_ext2.start_lba;
    if (mb < HDPREP_MIN_MB || mb > HDPREP_MAX_MB ||
        start >= g->media.capacity) return HDPREP_E_TOO_SMALL;
    u32 length = mb * HDPREP_SECTORS_PER_MB;
    if (length > g->media.capacity-start) length = g->media.capacity-start;
    if (g->media.root_ext2.entry) {
        if (g->media.root_ext2.sectors > length) return HDPREP_E_TOO_SMALL;
        length = g->media.root_ext2.sectors;
    }
    if (length < HDPREP_MIN_MB * HDPREP_SECTORS_PER_MB) return HDPREP_E_TOO_SMALL;
    *out = (HdprepPlan){.drive = g->drive, .start = start, .len = length,
                        .media = g->media, .table_writes = g->table_writes};
    out->media.root_ext2 = (InstallPlanPartitionV1){3, start, length, 3};
    return 0;
}

void platform_hdprep_refuse(KernelAPI *api, int code)
{
    api->kprintf(0x41, "hdprep: refused (rc=%d)\n", code);
}

int platform_hdprep_check(KernelAPI *api, HdprepGeom *g)
{
    int rc = inspect(api, g);
    if (rc) platform_hdprep_refuse(api, rc);
    return rc;
}

void platform_hdprep_describe(KernelAPI *api, const HdprepPlan *p)
{
    const InstallPlanV1 *m = &p->media;
    if (m->lba0_sectors) {
        api->kprintf(0xe1, "IDE %d: new APM, write DDR and entries 1-3.\n", p->drive);
    } else {
        api->kprintf(0xe1, "IDE %d: existing APM (%u entries), original table CRC %08x.\n",
                     p->drive, m->original_table_sectors-1, m->original_table_crc);
        for (unsigned i = 2; i <= 3; ++i)
            if (p->table_writes & (1u << i))
                api->kprintf(0xe1, "  Replace Apple_Free entry #%u with %s.\n", i, pm_types[i-1]);
    }
    api->kprintf(0xe1, "  #1 Apple_partition_map %u+%u, #2 Apple_HFS %u+%u, "
                 "#3 Apple_UNIX_SVR2 %u+%u sectors. Root data will be erased.\n",
                 m->map.start_lba, m->map.sectors, m->boot_hfs.start_lba,
                 m->boot_hfs.sectors, p->start, p->len);
}

void platform_hdprep_incomplete(KernelAPI *api, const char *what, int rc)
{
    if (handle) {
        api->install_end(handle, OS32_INSTALL_END_ABORT);
        handle = 0;
    }
    api->kprintf(0x41, "hdprep: INCOMPLETE: %s (%d)\n", what, rc);
}

int platform_hdprep_probe(KernelAPI *api, const HdprepPlan *p)
{
    int rc = api->install_begin(&p->media, &handle);
    if (rc) platform_hdprep_refuse(api, rc);
    return rc;
}

int pm_hdprep_finish(KernelAPI *api, int success)
{
    if (!handle) return 0;
    int rc = api->install_end(handle, success ? OS32_INSTALL_END_COMMIT : OS32_INSTALL_END_ABORT);
    handle = 0;
    if (rc) platform_hdprep_incomplete(api, "end reservation", rc);
    return rc;
}

int platform_hdprep_partition(KernelAPI *api, const HdprepPlan *p)
{
    u8 sector[512];
    const InstallPlanPartitionV1 *parts[] = {&p->media.map, &p->media.boot_hfs, &p->media.root_ext2};
    int rc = 0;
    u32 count = p->media.original_table_sectors-1;
    if (p->media.lba0_sectors) {
        count = 3;
        pm_apm_header(sector, p->media.capacity);
        rc = api->ide_write_sector(p->drive, 0, sector);
    }
    for (unsigned i = 0; !rc && i < 3; ++i) {
        if (!(p->table_writes & (1u << parts[i]->entry))) continue;
        pm_apm_entry(sector, count, parts[i]);
        rc = api->ide_write_sector(p->drive, parts[i]->entry, sector);
    }
    if (!rc) rc = api->install_rescan(handle);
    if (rc) platform_hdprep_incomplete(api, "APM readback", rc);
    return rc;
}

int platform_install_identify(KernelAPI *api)
{
    HdprepGeom g;
    return platform_hdprep_check(api, &g);
}

int platform_install_target(KernelAPI *api, InstTarget *t)
{
    HdprepGeom g;
    int rc = inspect(api, &g);
    if (!rc && api->ide_write_sectors(g.drive, 0, 0, 0)) {
        api->kprintf(0x41, "%s", "Disk write not authorized. Nothing was written.\n");
        return OS32_ERR_INVAL;
    }
    if (!rc) rc = hdprep_plan(&g, INST_PART_MB, &t->plan);
    return rc;
}

void inst_hdd_describe(KernelAPI *api, const InstTarget *t)
{
    platform_hdprep_describe(api, &t->plan);
    api->kprintf(0xe1, "%s", "  The installer will overwrite the entire Apple_HFS boot partition.\n");
}

void inst_hdd_incomplete(KernelAPI *api, const char *what, int rc)
{
    api->kprintf(0x41, "install: INCOMPLETE: %s (%d)\n", what, rc);
}

int inst_hdd_release(KernelAPI *api, InstTarget *t)
{
    return platform_hdprep_probe(api, &t->plan);
}

int platform_install_partition(KernelAPI *api, const InstTarget *t)
{
    return platform_hdprep_partition(api, &t->plan);
}

/* PLAN.BIN identifies both reserved ELF fields in the exact distributed HFS. */
const char *const platform_boot_paths[] = {"/cd0/BOOT.HFS", "/cd0/PLAN.BIN"};
const char *const platform_boot_entries[] = {"/boot/boot.hfs", "/boot/plan.bin"};

static int same(const u8 *a, const u8 *b, u32 bytes)
{
    for (u32 i = 0; i < bytes; ++i)
        if (a[i] != b[i]) return 0;
    return 1;
}

int platform_install_check_media(KernelAPI *api, const u32 *sizes, u32 kernel_len)
{
    (void)api;
    return sizes[0] == PM_HFS_SECTORS * 512 &&
           sizes[1] == 20 && kernel_len ? 0 : OS32_ERR_INVAL;
}

/* BOOT.PKG can exceed the guest heap. Keep one open source fd and stream
 * the selected extent for preflight, deployment and independent readback. */
struct hfs_source {
    int fd;
    u32 offset;
};
static struct hfs_source staged = {.fd = -1};
static u8 descriptor[20];

static int read_source(KernelAPI *api, const struct hfs_source *s, u32 pos, u8 *out, u32 n)
{
    if (s->fd < 0 || api->sys_lseek(s->fd, s->offset+pos, 0) < 0) return OS32_ERR_IO;
    while (n) {
        int got = api->sys_read(s->fd, out, n);
        if (got <= 0 || (u32)got > n) return OS32_ERR_IO;
        n -= got;
        out += got;
    }
    return 0;
}

static int validate_source(KernelAPI *api, const struct hfs_source *s, const u8 *desc, u32 size)
{
    static const u8 empty[20];
    u32 a = map_word(desc+12), b = map_word(desc+16);
    if (map_word(desc) != 0x504d4231 || map_word(desc+4) != size ||
        ((a|b) & 3) || a < 1536 || b < 1536 || a > size-1044 || b > size-1044 ||
        (a < b ? b-a : a-b) < 20) return OS32_ERR_INVAL;
    u8 buf[4096], primary[512];
    if (read_source(api, s, a, buf, sizeof(empty)) || !same(buf, empty, sizeof(empty)) ||
        read_source(api, s, b, buf, sizeof(empty)) || !same(buf, empty, sizeof(empty)) ||
        read_source(api, s, 1024, primary, sizeof(primary)) ||
        primary[0] != 'B' || primary[1] != 'D' ||
        read_source(api, s, size-1024, buf, sizeof(primary)) || !same(primary, buf, 512))
        return OS32_ERR_INVAL;
    u32 crc = crc32_core_update(CRC32_INIT, desc+12, 8);
    for (u32 pos = 0; pos < size;) {
        u32 n = size-pos < sizeof(buf) ? size-pos : sizeof(buf);
        if (read_source(api, s, pos, buf, n)) return OS32_ERR_IO;
        crc = crc32_core_update(crc, buf, n);
        pos += n;
    }
    return map_word(desc+8) == crc32_core_final(crc) ? 0 : OS32_ERR_INVAL;
}

void pm_install_release_assets(KernelAPI *api)
{
    if (staged.fd >= 0) api->sys_close(staged.fd);
    staged.fd = -1;
}

static int stage(KernelAPI *api, const char *path, u32 base, const u32 *sizes)
{
    staged.fd = api->sys_open(path, KAPI_O_RDONLY);
    if (staged.fd < 0) return OS32_ERR_IO;
    staged.offset = base;
    int rc = validate_source(api, &staged, descriptor, sizes[0]);
    if (rc) pm_install_release_assets(api);
    return rc;
}

static int read_descriptor(KernelAPI *api, const char *path, u32 offset)
{
    struct hfs_source src = {.fd = api->sys_open(path, KAPI_O_RDONLY), .offset = offset};
    if (src.fd < 0) return OS32_ERR_IO;
    int rc = read_source(api, &src, 0, descriptor, sizeof(descriptor));
    api->sys_close(src.fd);
    return rc;
}

int pm_install_check_directory(KernelAPI *api, const u32 *sizes)
{
    if (platform_install_check_media(api, sizes, 1) ||
        read_descriptor(api, platform_boot_paths[1], 0)) return OS32_ERR_INVAL;
    return stage(api, platform_boot_paths[0], 0, sizes);
}

int pm_install_stage_package(KernelAPI *api, const char *path, const u32 *offsets, const u32 *sizes)
{
    if (platform_install_check_media(api, sizes, 1) ||
        read_descriptor(api, path, offsets[1])) return OS32_ERR_INVAL;
    return stage(api, path, offsets[0], sizes);
}

int platform_install_package_boot(KernelAPI *api, const InstTarget *t,
                                  const u8 *const *assets, const u32 *sizes)
{
    const InstallPlanV1 *p = &t->plan.media;
    (void)assets; /* The package extents stay on the opened medium. */
    const u8 *desc = descriptor;
    if (platform_install_check_media(api, sizes, 1) || p->boot_hfs.sectors > PM_HFS_SECTORS ||
        sizes[0]/512 != p->boot_hfs.sectors || staged.fd < 0) return OS32_ERR_INVAL;
    u8 plan[20], block[8192], check[512];
    const u32 values[] = {p->boot_hfs.entry, p->root_ext2.entry, p->root_ext2.start_lba,
                          p->root_ext2.sectors, p->root_ext2.type};
    for (unsigned i = 0; i < 5; ++i) pm_put_word(plan+i*4, values[i]);
    int rc = api->vfs_sync();
    if (rc) return rc;
    rc = api->sys_umount_checked("/hd0");
    if (rc && rc != OS32_ERR_NOTFOUND) return rc;
    u32 count = p->boot_hfs.sectors;
    for (unsigned pass = 0; pass < 2; ++pass) {
        u32 crc = crc32_core_update(CRC32_INIT, desc+12, 8);
        for (u32 i = 0; i < count; ++i) {
            u32 off = i*512;
            u32 within = off % sizeof(block);
            if (!within) {
                u32 n = sizes[0]-off < sizeof(block) ? sizes[0]-off : sizeof(block);
                rc = read_source(api, &staged, off, block, n);
                if (rc) return rc;
                crc = crc32_core_update(crc, block, n);
            }
            u8 *sector = block+within;
            for (unsigned slot = 0; slot < 2; ++slot) {
                u32 pos = map_word(desc+12+slot*4);
                if (pos+20 <= off || pos >= off+512) continue;
                for (unsigned j = 0; j < 20; ++j)
                    if (pos+j >= off && pos+j < off+512) sector[pos+j-off] = plan[j];
            }
            u32 lba = p->boot_hfs.start_lba+i;
            if (!pass) rc = api->ide_write_sector(p->raw_unit, lba, sector);
            else {
                rc = api->ide_read_sector(p->raw_unit, lba, check);
                if (!rc && !same(check, sector, sizeof(check))) rc = OS32_ERR_IO;
            }
            if (rc) return rc;
        }
        if (crc32_core_final(crc) != map_word(desc+8)) return OS32_ERR_IO;
    }
    return 0;
}

int platform_install_directory_boot(KernelAPI *api, const InstTarget *t,
                                    const u32 *sizes, u8 *scratch, int capacity)
{
    (void)scratch;
    (void)capacity;
    return platform_install_package_boot(api, t, 0, sizes);
}
