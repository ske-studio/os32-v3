#include "kprintf.h"

extern u32 ata_first_pa;
extern void ata_wait_probe(void);

static void ata_read_report(void)
{
    unsigned present = 0, registered = 0;
    char name[] = "hd0";
    for (int unit = 0; unit < IDE_MAX_DRIVES; ++unit) {
        name[2] = '0' + unit;
        if (ide_drive_present(unit)) present |= 1U << unit;
        if (dev_find(name)) registered |= 1U << unit;
    }
    kprintf(7, "ATA present=%u registered=%u\r\n", present, registered);
    IdeInfo info;
    if (!ide_get_info(0, &info)) {
        kprintf(7, "ATA model=%s sectors=%u\r\n", info.model, info.total_sectors);
        const unsigned lbas[] = {0, 1024, 2047};
        u8 bytes[513] __attribute__((aligned(2)));
        for (unsigned n = 0; n < 3; ++n) {
            int rc = ide_read_sector(0, lbas[n], bytes + 1);
            u32 hash = 2166136261U;
            if (!rc)
                for (int i = 1; i <= 512; ++i) hash = (hash ^ bytes[i]) * 16777619U;
            kprintf(7, "ATA lba=%u rc=%d hash=%08x\r\n", lbas[n], rc, hash);
        }
    }
    kprintf(7, "ATA done\r\n");
}
