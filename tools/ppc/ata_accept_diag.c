/* Included only in the acceptance boot adapter. The raw disk is made by ata_accept.py. */
#include "kprintf.h"
static void ata_accept_report(void)
{
    IdeInfo info;
    Device *disk = dev_find("hd0");
    if (!disk || ide_get_info(0, &info)) {
        kprintf(7, "ATA-TEST absent=%u registered=%u\r\n",
                !ide_drive_present(0), !!disk);
        return;
    }
    kprintf(7, "ATA-TEST model=%s sectors=%u\r\n", info.model, info.total_sectors);
    u8 data[513] __attribute__((aligned(2)));
    static const u32 lbas[] = {0, 1024, 2047};
    for (unsigned n = 0; n < sizeof(lbas) / sizeof(*lbas); ++n) {
        u32 hash = 2166136261U;
        int rc = disk->blk_read(disk, lbas[n], 1, data + 1);
        if (!rc)
            for (unsigned i = 1; i <= 512; ++i) hash = (hash ^ data[i]) * 16777619U;
        kprintf(7, "ATA-TEST lba=%u rc=%d hash=%08x\r\n", lbas[n], rc, hash);
    }
    kprintf(7, "ATA-TEST done\r\n");
}
