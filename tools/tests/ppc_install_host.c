/* Real PowerMac provider, formatter and reservation manager over a RAM disk. */
#include "install_management.h"
#include "include/platform_install_boot.h"
#include "install_access.h"
#include "install_backend.h"
#include "install_apm.h"
#include "platform_ext2.h"
#include "fs/ext2/ext2_priv.h"
#include "drivers/ata/ide.h"
#include "lib/crc32.h"
#include "kstring.h"

enum { UNIT = 1, SECTORS = PM_ROOT_START+16416, START = PM_ROOT_START, LENGTH = 16384 };
static u8 disk[SECTORS][512], before[sizeof(disk)], pool[1024*1024];
static u32 used, writes;
static int owner = 1, allowed = 1, corrupt_read = -1, syncs;
static Ext2Ctx mounted;
void *kmalloc(u32 size)
{
    size = (size+7) & ~7UL;
    if (size > sizeof(pool)-used) return 0;
    used += size;
    return pool+used-size;
}
void kfree(void *ptr) { (void)ptr; }
int exec_disk_write_allowed(void) { return allowed; }
int res_owner_get(void) { return owner; }
int vfs_install_precheck(u32 u) { return u == UNIT ? 0 : -1; }
int vfs_install_detach(u32 unit)
{
    if (!mounted.mounted) return vfs_install_precheck(unit);
    int rc = ext2_sync(&mounted);
    if (!rc) ext2_unmount(&mounted);
    return rc;
}
void vfs_install_discard(u32 u) { (void)u; }
int ide_drive_present(int u) { return u == UNIT || u == 0; }
int ide_identify(int u, IdeInfo *out)
{
    if (!ide_drive_present(u)) return -1;
    kmemset(out, 0, sizeof(*out));
    out->total_sectors = SECTORS;
    out->phys_sector_size = 512;
    kstrcpy(out->serial, "OS32");
    return 0;
}
int ide_install_identify(u32 u, IdeInfo *out, char serial[IDE_SERIAL_LEN])
{
    kmemset(serial, ' ', IDE_SERIAL_LEN);
    kmemcpy(serial, "OS32", 4);
    return ide_identify(u, out);
}
int ide_get_info(int u, IdeInfo *out) { return ide_identify(u, out); }
int ide_range_ok(int u, u32 lba, u32 n)
{
    return ide_drive_present(u) && lba <= SECTORS && n <= SECTORS-lba;
}
int ide_install_read_many(u32 u, u32 lba, u32 n, void *out)
{
    if (u != UNIT || !ide_range_ok(u,lba,n)) return -1;
    kmemcpy(out, disk[lba], n*512);
    return 0;
}
int ide_install_read(u32 u, u32 lba, void *out) { return ide_install_read_many(u,lba,1,out); }
int ide_install_write(u32 u, u32 lba, u32 count, const void *in)
{
    if (u != UNIT || !ide_range_ok(u,lba,count)) return -1;
    kmemcpy(disk[lba], in, count * 512);
    ++writes;
    return 0;
}
int ide_install_flush(u32 u) { return u == UNIT ? 0 : -1; }
int ide_read_sectors(int u, u32 lba, u32 n, void *out) { return ide_install_read_many(u,lba,n,out); }
int ide_write_sectors(int u, u32 lba, u32 n, const void *in)
{
    struct install_access a;
    int handled;
    int rc = install_raw_io(&a,u,lba,n,0,in,1,&handled);
    if (handled) return rc;
    install_complete(&a);
    return ide_install_write(u,lba,n,in);
}
void kprintf(u8 color, const char *format, ...) { (void)color; (void)format; }
u32 ext2_current_time(void) { return 0; }

static int identify_for_api(int unit, void *info) { return ide_identify(unit,info); }
static int selected_drive(int unit) { return unit == UNIT; }
static int read_for_api(int unit, u32 lba, void *buf)
{
    int rc = ide_install_read(unit,lba,buf);
    if (!rc && lba == (u32)corrupt_read) ((u8 *)buf)[17] ^= 1;
    return rc;
}
static int sync_for_api(void) { ++syncs; return 0; }
static u32 last_begin;
static int begin_for_api(const InstallPlanV1 *plan,u32 *handle)
{
    int rc = install_begin(plan,handle);
    if (!rc) last_begin = *handle;
    return rc;
}
static int write_for_api(int unit, u32 lba, const void *buf)
{
    return ide_write_sectors(unit,lba,1,buf);
}
static int write_many_for_api(int unit, u32 lba, u32 count, const void *buf)
{
    if (!allowed || !count) return allowed ? 0 : OS32_ERR_INVAL;
    return ide_write_sectors(unit,lba,count,buf);
}

/* VFS fixtures expose the same template as files and as payloads at
 * unaligned BOOT.PKG offsets. Short reads exercise streaming on both paths. */
enum { HFS_BYTES = PM_HFS_SECTORS*512, BUNDLE_BASE = 43, DESCRIPTOR_BASE = BUNDLE_BASE+HFS_BYTES+7 };
static u8 template[HFS_BYTES], boot_desc[20], bundle[DESCRIPTOR_BASE+20];
static const u8 *file_data[] = {template,boot_desc,bundle};
static const u32 file_size[] = {sizeof(template),sizeof(boot_desc),sizeof(bundle)};
static int opened[3];
static u32 position[3];
static int open_file(const char *path, int mode)
{
    const char *names[] = {platform_boot_paths[0],platform_boot_paths[1],"/BOOT.PKG"};
    for (int i = 0; !mode && i < 3; ++i) {
        if (!kstrcmp(path,names[i]) && !opened[i]) {
            opened[i] = 1;
            position[i] = 0;
            return i;
        }
    }
    return -1;
}
static int seek_file(int fd, int offset, int whence)
{
    if (fd < 0 || fd > 2 || !opened[fd] || whence || (u32)offset > file_size[fd]) return -1;
    position[fd] = offset;
    return offset;
}
static int read_file(int fd, void *out, u32 count)
{
    if (fd < 0 || fd > 2 || !opened[fd]) return -1;
    u32 left = file_size[fd]-position[fd];
    if (count > left) count = left;
    if (count > 341) count = 341;
    kmemcpy(out,file_data[fd]+position[fd],count);
    position[fd] += count;
    return count;
}
static void close_file(int fd) { opened[fd] = 0; }

static int unmount_for_api(const char *path)
{
    if (kstrcmp(path, "/hd0") || !mounted.mounted) return OS32_ERR_NOTFOUND;
    int rc = vfs_install_detach(UNIT);
    return rc ? rc : install_view_detached(UNIT);
}

static KernelAPI api = {
    .sys_umount_checked = unmount_for_api,
    .ide_identify = identify_for_api, .ide_drive_present = selected_drive,
    .ide_read_sector = read_for_api, .ide_write_sector = write_for_api,
    .ide_write_sectors = write_many_for_api,
    .install_begin = begin_for_api, .install_end = install_end, .install_rescan = install_rescan,
    .vfs_sync = sync_for_api, .kprintf = kprintf,
    .sys_open = open_file, .sys_read = read_file, .sys_lseek = seek_file, .sys_close = close_file
};

/* Independent expected bytes include a configuration field crossing a
 * sector boundary. The kernel's reservation protects the real RAM image. */
static int deploy(const InstallPlanV1 *approved)
{
    enum { LOADER_SLOT = 2044, PRODUCT_SLOT = 2560 };
    static u8 expected[HFS_BYTES];
    const u32 sizes[] = {sizeof(template),sizeof(boot_desc)};
    const u32 offsets[] = {BUNDLE_BASE,DESCRIPTOR_BASE};
    InstTarget target = {.plan = {.media = *approved}};
    template[1024] = template[HFS_BYTES-1024] = 'B';
    template[1025] = template[HFS_BYTES-1023] = 'D';
    template[HFS_BYTES-1] = 0xa7;
    pm_put_word(boot_desc,0x504d4231);
    pm_put_word(boot_desc+4,HFS_BYTES);
    pm_put_word(boot_desc+12,LOADER_SLOT);
    pm_put_word(boot_desc+16,PRODUCT_SLOT);
    kmemcpy(bundle,boot_desc+12,8);
    kmemcpy(bundle+8,template,HFS_BYTES);
    pm_put_word(boot_desc+8,crc32_calc(bundle,HFS_BYTES+8));
    target.plan.media.boot_hfs.sectors = HFS_BYTES/512-1;
    kmemcpy(before,disk,sizeof(disk));
    boot_desc[8] ^= 1;
    if (!pm_install_check_directory(&api,sizes) ||
        memcmp(before,disk,sizeof(disk))) return 41;
    boot_desc[8] ^= 1;
    boot_desc[15] ^= 4; /* Still aligned and zero, but no longer the configured ELF field. */
    if (!pm_install_check_directory(&api,sizes) ||
        memcmp(before,disk,sizeof(disk))) return 47;
    boot_desc[15] ^= 4;
    if (pm_install_check_directory(&api,sizes) ||
        !platform_install_directory_boot(&api,&target,sizes,0,0) ||
        memcmp(before,disk,sizeof(disk))) return 40;
    target.plan.media.boot_hfs.sectors++;
    u32 start = approved->boot_hfs.start_lba;
    corrupt_read = start+HFS_BYTES/512-1;
    if (!platform_install_directory_boot(&api,&target,sizes,0,0)) return 42;
    corrupt_read = -1;
    if (platform_install_directory_boot(&api,&target,sizes,0,0) || !syncs) return 43;
    pm_install_release_assets(&api);
    kmemcpy(expected,template,HFS_BYTES);
    const u32 words[] = {2,3,START,LENGTH,3};
    for (unsigned i = 0; i < 5; ++i) {
        pm_put_word(expected+LOADER_SLOT+i*4,words[i]);
        pm_put_word(expected+PRODUCT_SLOT+i*4,words[i]);
    }
    if (memcmp(disk[start],expected,HFS_BYTES)) return 44;
    kmemcpy(bundle+BUNDLE_BASE,template,HFS_BYTES);
    kmemcpy(bundle+DESCRIPTOR_BASE,boot_desc,sizeof(boot_desc));
    kmemset(disk[start],0x5a,HFS_BYTES);
    if (pm_install_stage_package(&api,"/BOOT.PKG",offsets,sizes) ||
        platform_install_package_boot(&api,&target,0,sizes)) return 45;
    pm_install_release_assets(&api);
    if (opened[0] || opened[1] || opened[2] ||
        memcmp(disk[start],expected,HFS_BYTES) || memcmp(before,disk,start*512) ||
        memcmp(before+start*512+HFS_BYTES, disk[start+HFS_BYTES/512],
               sizeof(disk)-start*512-HFS_BYTES)) return 46;
    return 0;
}

static int existing_apm(InstallPlanV1 p, Device *old_view, u32 previous)
{
    const struct install_medium *m = platform_install_medium();
    p.map.sectors = 4;
    p.boot_hfs.start_lba--;
    p.lba0_sectors = 0;
    p.original_table_sectors = 5;
    pm_apm_entry(disk[1],4,&p.map);
    pm_apm_entry(disk[2],4,&p.boot_hfs);
    pm_apm_entry(disk[3],4,&p.root_ext2);
    for (unsigned i = 2; i <= 3; ++i) {
        kmemset(disk[i]+48,0,32);
        kstrcpy((char *)disk[i]+48,"Apple_Free");
    }
    InstallPlanPartitionV1 keep = {4, START+LENGTH-1, 32, 2};
    pm_apm_entry(disk[4],4,&keep);
    p.original_table_crc = crc32_calc(disk,5*512);
    u32 handle = 0;
    if (!install_begin(&p,&handle) || handle) return 20; /* independently parsed overlap */
    keep.start_lba++;
    pm_apm_entry(disk[4],4,&keep);
    p.original_table_crc = crc32_calc(disk,5*512);
    disk[keep.start_lba][37] = 91;
    if (install_begin(&p,&handle) || handle == previous) return 21;
    u32 unit,physical,generation;
    if (m->resolve(old_view,0,1,&unit,&physical,&generation) >= 0) return 22;
    if (ext2_format(UNIT,LENGTH-1) >= 0 || ext2_format(UNIT,LENGTH)) return 23;
    const InstallPlanPartitionV1 *parts[] = {&p.boot_hfs,&p.root_ext2};
    for (unsigned i = 0; i < 2; ++i) {
        struct install_access access;
        u8 sector[512];
        int handled;
        pm_apm_entry(sector,4,parts[i]);
        if (install_raw_io(&access,UNIT,parts[i]->entry,1,0,sector,1,&handled) || !handled)
            return 24;
    }
    if (install_rescan(handle)) return 25;
    disk[4][20] ^= 1;
    if (!m->verify(&p)) return 26;
    disk[4][20] ^= 1;
    if (install_end(handle,OS32_INSTALL_END_COMMIT) || disk[keep.start_lba][37] != 91)
        return 27;
    /* Replan from the actual table. A foreign entry occupies the tail and
     * must survive both rejecting an overlap and reusing the OS32 entries. */
    HdprepGeom geometry;
    kmemcpy(before, disk, sizeof(disk));
    if (platform_hdprep_check(&api, &geometry) ||
        geometry.media.original_table_crc != crc32_calc(disk,5*512) ||
        geometry.media.root_ext2.start_lba != START ||
        memcmp(before,disk,sizeof(disk))) return 32;
    InstTarget target;
    if (platform_install_target(&api,&target) || target.plan.start != START ||
        target.plan.len != LENGTH || inst_hdd_release(&api,&target) ||
        ext2_format_at(UNIT,target.plan.start,target.plan.len) ||
        platform_install_partition(&api,&target) || pm_hdprep_finish(&api,1) ||
        memcmp(before,disk,START*512) ||
        memcmp(before+(START+LENGTH)*512,disk[START+LENGTH],32*512))
        return 37;
    pm_put_word(disk[4]+8, START+LENGTH-1);
    kmemcpy(before, disk, sizeof(disk));
    if (!platform_hdprep_check(&api, &geometry) || memcmp(before,disk,sizeof(disk))) return 33;
    return 0;
}

static int exercise(void)
{
    InstallPlanV1 p = {.version=1, .size=128, .raw_unit=UNIT, .sector_bytes=512,
        .capacity=SECTORS, .original_table_sectors=2, .map={1,1,PM_MAP_SECTORS,1},
        .boot_hfs={2,PM_HFS_START,PM_HFS_SECTORS,2}, .root_ext2={3,START,LENGTH,3}, .lba0_sectors=1};
    u32 handle = 0;
    struct install_identity id;
    const struct install_medium *m = platform_install_medium();
    if (m->identify(UNIT,&id)) return 1;
    p.serial_crc = id.serial_crc;
    p.original_table_crc = crc32_calc(disk,1024);
    HdprepGeom geometry;
    HdprepPlan plan;
    if (platform_hdprep_check(&api,&geometry) || hdprep_plan(&geometry,8,&plan) ||
        memcmp(&plan.media,&p,sizeof(p)) || writes || memcmp(before,disk,sizeof(disk)))
        return 38;
    allowed = 0;
    InstTarget target;
    if (!platform_install_target(&api,&target) || writes || memcmp(before,disk,sizeof(disk)))
        return 39;
    allowed = 1;
    pm_format_register();
    u8 sectors[1024] = {91};
    int single = ide_write_sectors(UNIT,1,1,sectors);
    int multi = ide_write_sectors(UNIT,START,2,sectors);
    int format = ext2_format_at(UNIT,START,LENGTH);
    if (memcmp(before,disk,sizeof(disk)) || writes || !single || !multi || !format) return 28;
    p.boot_hfs.sectors = 60;
    if (!install_begin(&p,&handle) || handle || writes) return 29;
    p.boot_hfs.sectors = PM_HFS_SECTORS;
    disk[SECTORS-1][500] = 43;
    if (!install_begin(&p,&handle) || handle || writes) return 2;
    disk[SECTORS-1][500] = 0;
    if (install_begin(&p,&handle) || !handle) return 3;
    owner++;
    if (ext2_format_at(UNIT,START,LENGTH) >= 0) return 4;
    owner--;
    if (ext2_format_at(0,START,LENGTH) >= 0) return 5;
    if (ext2_format_at(UNIT,START+1,LENGTH) >= 0) return 6;
    if (ext2_format_at(UNIT,START,LENGTH-1) >= 0) return 7;
    if (writes || memcmp(before,disk,sizeof(disk))) return 8;
    if (ext2_format_at(UNIT,START,LENGTH) || !writes) return 9;
    if (memcmp(before,disk,START*512) || memcmp(before+(START+LENGTH)*512,
        disk[START+LENGTH],(SECTORS-START-LENGTH)*512)) return 10;
    /* Interrupt first before the initial APM write, then with a partial APM.
     * Replan after each restoration; the normal begin rechecks the media. */
    for (int partial = 0; partial <= 1; ++partial) {
        u8 header[512];
        pm_apm_header(header, SECTORS);
        if (partial && write_for_api(UNIT,0,header)) return 34;
        u32 old = handle;
        install_reclaim_owner(owner);
        HdprepGeom geometry;
        if (dev_find("hd0") || memcmp(before,disk,4*512) ||
            platform_hdprep_check(&api,&geometry) ||
            geometry.media.original_table_crc != p.original_table_crc ||
            hdprep_plan(&geometry,8,&plan) || platform_hdprep_probe(&api,&plan) ||
            last_begin == old || ext2_format_at(UNIT,START,LENGTH))
            return 35;
        handle = last_begin;
    }
    if (disk[START+2][56] != 0x53 || disk[START+2][57] != 0xef) return 11;
    if (platform_hdprep_partition(&api,&plan)) return 12;
    if (install_rescan(handle) || ext2_mount(&mounted,0)) return 13;
    kmemcpy(before,disk,sizeof(disk));
    if (!ide_write_sectors(UNIT,PM_HFS_START,1,sectors) || memcmp(before,disk,sizeof(disk))) return 48;
    allowed = 0;
    if (!dev_blk_write_lba(dev_find("hd0"),LENGTH-1,1,sectors) || memcmp(before,disk,sizeof(disk))) return 49;
    allowed = 1;
    /* A request crossing either HFS edge must leave the whole disk intact. */
    kmemcpy(before, disk, sizeof(disk));
    if (!ide_write_sectors(UNIT,PM_HFS_START-1,2,sectors) ||
        !ide_write_sectors(UNIT,START-1,2,sectors) ||
        memcmp(before,disk,sizeof(disk))) return 36;
    if (unmount_for_api("/hd0")) return 50;
    int early = ide_write_sectors(UNIT,PM_HFS_START,1,sectors);
    if (memcmp(before+PM_HFS_START*512,disk[PM_HFS_START],PM_HFS_SECTORS*512) || !early)
        return 51;
    if (ext2_mount(&mounted,0)) return 52;
    static const char payload[] = "PowerMac reserved view";
    char output[sizeof(payload)];
    u32 ino;
    if (ext2_create(&mounted,EXT2_ROOT_INO,"marker",payload,sizeof(payload)) < 0 ||
        ext2_lookup(&mounted,"/marker",&ino) ||
        ext2_read_file(&mounted,ino,output,sizeof(output)) != sizeof(output) ||
        memcmp(output,payload,sizeof(output))) return 15;
    if (pm_install_files_verified(&api)) return 53;
    int deployed = deploy(&p);
    if (deployed) return deployed;
    kmemcpy(before,disk,sizeof(disk));
    if (!ide_write_sectors(UNIT,START-1,2,sectors) || memcmp(before,disk,sizeof(disk))) return 36;
    if (install_end(handle,OS32_INSTALL_END_COMMIT) || mounted.mounted) return 16;
    Device *registered = dev_find("hd0");
    if (!registered || ext2_mount(&mounted,0) || ext2_lookup(&mounted,"/marker",&ino) ||
        ext2_read_file(&mounted,ino,output,sizeof(output)) != sizeof(output) ||
        memcmp(output,payload,sizeof(output))) return 17;
    u32 unit,physical,generation;
    if (m->resolve(registered,2,1,&unit,&physical,&generation) != 1 ||
        unit != UNIT || physical != START+2 || generation != handle) return 14;
    /* Both the published view and its raw alias are read-only. */
    u32 last = LENGTH-1;
    kmemcpy(before,disk,sizeof(disk));
    if (!dev_blk_write_lba(registered,last,1,sectors) || memcmp(before,disk,sizeof(disk))) return 30;
    kmemcpy(before,disk,sizeof(disk));
    if (!ide_write_sectors(UNIT,START+last,1,sectors+512) ||
        memcmp(before,disk,sizeof(disk))) return 31;
    return existing_apm(p,registered,handle);
}
void _start(void)
{
    int rc = exercise();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
