#include "include/platform_install_boot.h"
#include "include/ide_contract.h"
#include "install_apm.h"
#include "lib/crc32_core.inc"

static u32 handle;

/* Empty disk preparation reserves the first 63 entries and an 8 MiB HFS
 * extent for the boot image. A unit must be unambiguous before confirmation. */
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
    u32 crc = CRC32_INIT;
    for (unsigned i = 0; i < 2; ++i) {
        if (api->ide_read_sector(selected, i, block)) return OS32_ERR_IO;
        for (unsigned j = 0; j < sizeof(block); ++j)
            if (block[j]) return HDPREP_E_PT_USED;
        crc = crc32_core_update(crc, block, sizeof(block));
    }
    for (int i = IDE_SERIAL_LEN-1; i >= 0 && !info.serial[i]; --i) info.serial[i] = ' ';
    g->drive = selected;
    g->media = (InstallPlanV1){
        .version = 1, .size = sizeof(g->media), .raw_unit = selected,
        .capacity = info.total_sectors, .sector_bytes = 512, .original_table_sectors = 2,
        .original_table_crc = crc32_core_final(crc),
        .serial_crc = crc32_core_final(crc32_core_update(CRC32_INIT, info.serial, IDE_SERIAL_LEN)),
        .map = {1, 1, PM_MAP_SECTORS, 1}, .boot_hfs = {2, PM_HFS_START, PM_HFS_SECTORS, 2}, .lba0_sectors = 1
    };
    return 0;
}

int hdprep_plan(const HdprepGeom *g, unsigned long mb, HdprepPlan *out)
{
    u32 start = g->media.boot_hfs.start_lba + g->media.boot_hfs.sectors;
    if (mb < HDPREP_MIN_MB || mb > HDPREP_MAX_MB ||
        start >= g->media.capacity) return HDPREP_E_TOO_SMALL;
    u32 length = mb * HDPREP_SECTORS_PER_MB;
    if (length > g->media.capacity-start) length = g->media.capacity-start;
    if (length < HDPREP_MIN_MB * HDPREP_SECTORS_PER_MB) return HDPREP_E_TOO_SMALL;
    *out = (HdprepPlan){.drive = g->drive, .start = start, .len = length, .media = g->media};
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
    api->kprintf(0xe1, "IDE %d: APM 1+63, Apple_HFS %u+%u, ext2 %u+%u sectors\n",
                 p->drive, p->media.boot_hfs.start_lba, p->media.boot_hfs.sectors,
                 p->start, p->len);
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
    pm_apm_header(sector, p->media.capacity);
    int rc = api->ide_write_sector(p->drive, 0, sector);
    for (unsigned i = 0; !rc && i < 3; ++i) {
        pm_apm_entry(sector, 3, parts[i]);
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
    if (!rc) rc = hdprep_plan(&g, INST_PART_MB, &t->plan);
    return rc;
}

void inst_hdd_describe(KernelAPI *api, const InstTarget *t)
{
    platform_hdprep_describe(api, &t->plan);
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

/* Product HFS boot assets are supplied by the subsequent deployment stage.
 * Media inspection fails before confirmation until that provider is linked. */
const char *const platform_boot_paths[] = {"/boot/boot.hfs"};
const char *const platform_boot_entries[] = {"/boot/boot.hfs"};
int platform_install_check_media(KernelAPI *api, const u32 *sizes, u32 kernel_len)
{
    (void)sizes; (void)kernel_len;
    api->kprintf(0x41, "%s", "PowerMac boot image deployment unavailable\n");
    return OS32_ERR_NOSYS;
}

int platform_install_directory_boot(KernelAPI *api, const InstTarget *t,
                                    const u32 *sizes, u8 *scratch, int capacity)
{
    (void)api; (void)t; (void)sizes; (void)scratch; (void)capacity;
    return OS32_ERR_NOSYS;
}

int platform_install_package_boot(KernelAPI *api, const InstTarget *t,
                                  const u8 *const *assets, const u32 *sizes)
{
    (void)api; (void)t; (void)assets; (void)sizes;
    return OS32_ERR_NOSYS;
}
