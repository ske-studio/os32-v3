/* ATA_HAL_DESIGN §6-4. Embedded only in the acceptance kernel between
 * exec_init and shell_loop_run. The host checks the entire disposable disk. */
#include "arch_exec_kapi.h"
#include "kapi_table.h"
#include "drivers/ata/ide.h"
#include "kstring.h"

#include "install_apm.h"
#include "platform_install_management.h"
#include "lib/crc32_core.inc"

unsigned ata_write_provider_calls;
int ata_payload_phase;
unsigned ata_write_commands;

enum { ROOT_LENGTH = 4000, WRITE_CAPACITY = PM_ROOT_START + ROOT_LENGTH };

/* Name the step that stopped the fixture: a silent return looked like a lost serial line in CI. */
static int ata_step(const char *step, int rc)
{
    kprintf(7, "ATA-TEST failed=%s rc=%d\r\n", step, rc);
    return -1;
}

static int prepare_disk(const KernelAPI *api, u32 *handle)
{
    InstallPlanV1 p = {.version=1, .size=sizeof(p), .sector_bytes=512,
        .capacity=WRITE_CAPACITY, .original_table_sectors=2, .lba0_sectors=1,
        .map={1,1,PM_MAP_SECTORS,1}, .boot_hfs={2,PM_HFS_START,PM_HFS_SECTORS,2},
        .root_ext2={3,PM_ROOT_START,ROOT_LENGTH,3}};
    struct install_identity id;
    static u8 table[4][512];
    int rc;
    if ((rc = platform_install_medium()->identify(0, &id)) != 0) return ata_step("identify", rc);
    p.serial_crc = id.serial_crc;
    p.original_table_crc = crc32_core_final(crc32_core_update(CRC32_INIT, table, 1024));
    if ((rc = api->install_begin(&p, handle)) != 0) return ata_step("install_begin", rc);
    if ((rc = api->ext2_format_at(0, PM_ROOT_START, ROOT_LENGTH)) != 0) return ata_step("ext2_format", rc);
    pm_apm_header(table[0], WRITE_CAPACITY);
    pm_apm_entry(table[1], 3, &p.map);
    pm_apm_entry(table[2], 3, &p.boot_hfs);
    pm_apm_entry(table[3], 3, &p.root_ext2);
    if ((rc = ide_write_sectors(0, 0, 1, table[0])) != 0) return ata_step("write_lba0", rc);
    if ((rc = ide_write_sectors(0, 1, 3, table[1])) != 0) return ata_step("write_map", rc);
    if ((rc = api->install_rescan(*handle)) != 0) return ata_step("install_rescan", rc);

    /* Host checks the formatted regions against their pre-payload CRC. */
    static u8 block[16*512];
    u32 crc = crc32_core_update(CRC32_INIT, table, sizeof(table));
    for (u32 i = 0; i < ROOT_LENGTH; i += 16) {
        if ((rc = ide_read_sectors(0, PM_ROOT_START+i, 16, block)) != 0) return ata_step("read_root", rc);
        crc = crc32_core_update(crc, block, sizeof(block));
    }
    kprintf(7, "ATA-TEST metadata=%08x\r\n", crc32_core_final(crc));
    return 0;
}

void ata_write_report(int child)
{
    const KernelAPI *api = (const KernelAPI *)exec_kapi_table();
    IdeInfo id;
    static u8 payload[1025] __attribute__((aligned(2)));
    static u8 buffer[513] __attribute__((aligned(2)));
    if (api->ide_identify(0, &id) || kstrcmp(id.model, "QEMU HARDDISK") ||
        id.total_sectors != (child ? 2048 : WRITE_CAPACITY)) {
        kprintf(7, "ATA-TEST wrong disk\r\n");
        return;
    }
    if (child) {
        payload[1] = 0xab;
        int one = api->ide_write_sector(0, 32, payload + 1);
        int many = api->ide_write_sectors(0, 64, 2, payload + 1);
        kprintf(7, "ATA-TEST unreserved=%d,%d\r\n", one, many);
        ata_write_provider_calls = 0;
        int status = api->exec_run("/bin/atadeny.bin");
        kprintf(7, "ATA-TEST child=%d provider_calls=%u\r\n", status, ata_write_provider_calls);
        return;
    }
    u32 handle = 0;
    if (prepare_disk(api, &handle)) return;
    int rc;
    if ((rc = api->sys_mount("/hd0", "hd0", "ext2")) != 0) { ata_step("mount", rc); return; }
    if ((rc = api->sys_umount_checked("/hd0")) != 0) { ata_step("umount", rc); return; }
    ata_payload_phase = 1;
    for (unsigned i = 0; i < 1024; ++i)
        payload[1+i] = i * 37 + (i >> 8) + 13;
    int one = api->ide_write_sector(0, 96, payload + 1);
    ata_write_commands = 0;
    int many = api->ide_write_sectors(0, 64, 2, payload + 1);
    kprintf(7, "ATA-TEST commands=%u\r\n", ata_write_commands);
    int range = api->ide_write_sectors(0, PM_ROOT_START-1, 2, payload + 1);
    kprintf(7, "ATA-TEST writes=%d,%d range=%d\r\n", one, many, range);
    for (unsigned n = 0; n < 3; ++n) {
        int rc = api->ide_read_sector(0, n ? 63+n : 96, buffer + 1);
        int equal = !rc && !memcmp(buffer + 1, payload + 1 + (n == 2 ? 512 : 0), 512);
        kprintf(7, "ATA-TEST read=%u equal=%d\r\n", n, equal);
    }
    ata_payload_phase = 0;
    kprintf(7, "ATA-TEST end=%d\r\n", api->install_end(handle, OS32_INSTALL_END_COMMIT));
}
