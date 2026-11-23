/* Real PowerMac provider, formatter and reservation manager over a RAM disk. */
#include "install_management.h"
#include "install_access.h"
#include "install_backend.h"
#include "install_apm.h"
#include "platform_ext2.h"
#include "fs/ext2/ext2_priv.h"
#include "drivers/ata/ide.h"
#include "lib/crc32.h"
#include "kstring.h"

enum { UNIT = 1, SECTORS = 4096, START = 64, LENGTH = 4000 };
static u8 disk[SECTORS][512], before[sizeof(disk)], pool[256*1024];
static u32 used, writes;
static int owner = 1, allowed = 1;
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
int ide_install_write(u32 u, u32 lba, const void *in)
{
    if (u != UNIT || !ide_range_ok(u,lba,1)) return -1;
    kmemcpy(disk[lba], in, 512);
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
    for (u32 i = 0; i < n; ++i) if (ide_install_write(u,lba+i,(const u8 *)in+i*512)) return -1;
    return 0;
}
void kprintf(u8 color, const char *format, ...) { (void)color; (void)format; }
u32 ext2_current_time(void) { return 0; }

static int existing_apm(InstallPlanV1 p, Device *old_view, u32 previous)
{
    const struct install_medium *m = platform_install_medium();
    p.map.sectors = 4;
    p.boot_hfs.start_lba++;
    p.boot_hfs.sectors--;
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
    return 0;
}

static int exercise(void)
{
    InstallPlanV1 p = {.version=1, .size=128, .raw_unit=UNIT, .sector_bytes=512,
        .capacity=SECTORS, .original_table_sectors=2, .map={1,1,3,1},
        .boot_hfs={2,4,60,2}, .root_ext2={3,START,LENGTH,3}, .lba0_sectors=1};
    u32 handle = 0;
    struct install_identity id;
    const struct install_medium *m = platform_install_medium();
    if (m->identify(UNIT,&id)) return 1;
    p.serial_crc = id.serial_crc;
    p.original_table_crc = crc32_calc(disk,1024);
    pm_format_register();
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
    if (disk[START+2][56] != 0x53 || disk[START+2][57] != 0xef) return 11;
    u8 s[512];
    pm_apm_header(s,SECTORS);
    const InstallPlanPartitionV1 *parts[] = {&p.map,&p.boot_hfs,&p.root_ext2};
    for (unsigned i = 0; i < 4; ++i) {
        if (i) pm_apm_entry(s,3,parts[i-1]);
        struct install_access a;
        int handled;
        if (install_raw_io(&a,UNIT,i,1,0,s,1,&handled) || !handled) return 12;
    }
    if (install_rescan(handle) || ext2_mount(&mounted,0)) return 13;
    static const char payload[] = "PowerMac reserved view";
    char output[sizeof(payload)];
    u32 ino;
    if (ext2_create(&mounted,EXT2_ROOT_INO,"marker",payload,sizeof(payload)) < 0 ||
        ext2_lookup(&mounted,"/marker",&ino) ||
        ext2_read_file(&mounted,ino,output,sizeof(output)) != sizeof(output) ||
        memcmp(output,payload,sizeof(output))) return 15;
    if (install_end(handle,OS32_INSTALL_END_COMMIT) || mounted.mounted) return 16;
    Device *registered = dev_find("hd0");
    if (!registered || ext2_mount(&mounted,0) || ext2_lookup(&mounted,"/marker",&ino) ||
        ext2_read_file(&mounted,ino,output,sizeof(output)) != sizeof(output) ||
        memcmp(output,payload,sizeof(output))) return 17;
    u32 unit,physical,generation;
    if (m->resolve(registered,2,1,&unit,&physical,&generation) != 1 ||
        unit != UNIT || physical != START+2 || generation != handle) return 14;
    return existing_apm(p,registered,handle);
}
void _start(void)
{
    int rc = exercise();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
