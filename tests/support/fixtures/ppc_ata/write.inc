#include "arch_exec_kapi.h"
#include "kapi_table.h"
#include "sys/dev/ata/ide.h"
#include "install_apm.h"
#include "platform_install_management.h"
#include "lib/crc32_core.inc"
#include "kstring.h"

extern unsigned ata_commands;
unsigned ata_provider_calls;
enum { ROOT_BLOCKS = 4000, CAPACITY = PM_ROOT_START + ROOT_BLOCKS };

static int ata_failed(const char *stage, int rc)
{
    kprintf(7, "ATA failed=%s rc=%d\r\n", stage, rc);
    return rc;
}

static int ata_prepare(const KernelAPI *api, u32 *handle)
{
    static u8 map[4][512];
    static u8 blocks[16 * 512];
    struct install_identity identity;
    InstallPlanV1 plan = {
        .version = 1, .size = sizeof(plan), .sector_bytes = 512, .capacity = CAPACITY,
        .original_table_sectors = 2, .lba0_sectors = 1, .map = {1,1,PM_MAP_SECTORS,1},
        .boot_hfs = {2,PM_HFS_START,PM_HFS_SECTORS,2},
        .root_ext2 = {3,PM_ROOT_START,ROOT_BLOCKS,3}
    };
    int rc = platform_install_medium()->identify(0, &identity);
    if (rc) return ata_failed("identify", rc);
    plan.serial_crc = identity.serial_crc;
    plan.original_table_crc = crc32_core_final(crc32_core_update(CRC32_INIT, map, 1024));
    rc = api->install_begin(&plan, handle);
    if (rc) return ata_failed("install_begin", rc);
    rc = api->ext2_format_at(0, PM_ROOT_START, ROOT_BLOCKS);
    if (rc) return ata_failed("format", rc);
    pm_apm_header(map[0], CAPACITY);
    pm_apm_entry(map[1], 3, &plan.map);
    pm_apm_entry(map[2], 3, &plan.boot_hfs);
    pm_apm_entry(map[3], 3, &plan.root_ext2);
    rc = ide_write_sectors(0, 0, 1, map[0]);
    if (rc) return ata_failed("header", rc);
    rc = ide_write_sectors(0, 1, 3, map[1]);
    if (rc) return ata_failed("map", rc);
    rc = api->install_rescan(*handle);
    if (rc) return ata_failed("rescan", rc);
    u32 crc = crc32_core_update(CRC32_INIT, map, sizeof(map));
    for (unsigned i = 0; i < ROOT_BLOCKS; i += 16) {
        rc = ide_read_sectors(0, PM_ROOT_START + i, 16, blocks);
        if (rc) return ata_failed("metadata", rc);
        crc = crc32_core_update(crc, blocks, sizeof(blocks));
    }
    kprintf(7, "ATA metadata=%08x\r\n", crc32_core_final(crc));
    rc = api->sys_mount("/hd0", "hd0", "ext2");
    if (rc) return ata_failed("mount", rc);
    rc = api->sys_umount_checked("/hd0");
    return rc ? ata_failed("umount", rc) : 0;
}

void ata_write_report(int deny)
{
    const KernelAPI *api = (const KernelAPI *)exec_kapi_table();
    static u8 storage[1025] __attribute__((aligned(2)));
    static u8 check[513] __attribute__((aligned(2)));
    u8 *payload = storage + 1;
    IdeInfo info;
    int rc = api->ide_identify(0, &info);
    if (rc || kstrcmp(info.model, "QEMU HARDDISK") ||
        info.total_sectors != (deny ? 2048 : CAPACITY)) {
        ata_failed("medium", rc);
        return;
    }
    if (deny) {
        int one = api->ide_write_sector(0, 32, payload);
        int many = api->ide_write_sectors(0, 64, 2, payload);
        ata_provider_calls = 0;
        int child = api->exec_run("/bin/atadeny.bin");
        kprintf(7, "ATA unreserved=%d,%d child=%d provider_calls=%u\r\n",
                one, many, child, ata_provider_calls);
        return;
    }
    u32 handle;
    if (ata_prepare(api, &handle)) return;
    for (unsigned i = 0; i < 1024; ++i) payload[i] = i * 37 + (i >> 8) + 13;
    int one = api->ide_write_sector(0, 96, payload);
    unsigned before = ata_commands;
    int many = api->ide_write_sectors(0, 64, 2, payload);
    kprintf(7, "ATA commands=%u\r\n", ata_commands - before);
    int range = api->ide_write_sectors(0, PM_ROOT_START - 1, 2, payload);
    kprintf(7, "ATA writes=%d,%d range=%d\r\n", one, many, range);
    const unsigned sectors[] = {96, 64, 65};
    for (unsigned i = 0; i < 3; ++i) {
        rc = api->ide_read_sector(0, sectors[i], check + 1);
        kprintf(7, "ATA read=%u equal=%d\r\n", sectors[i],
                !rc && !memcmp(check + 1, payload + (i == 2 ? 512 : 0), 512));
    }
    kprintf(7, "ATA end=%d\r\n", api->install_end(handle, OS32_INSTALL_END_COMMIT));
}
