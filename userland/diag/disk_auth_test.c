/* PM chooses a disposable image and an unused, harmless sector explicitly. */
#include "os32api.h"
#include <stdlib.h>
#include <string.h>

#define TEST_SECTOR_BYTES 512
#define TEST_IDE_DEVICES { "hd0", "hd1", "hd2", "hd3" }
#define TEST_FIRST_SAFE_LBA 18u /* never probe the PC-98 IPL/partition area */

int main(int argc, char **argv, KernelAPI *api)
{
    static unsigned char before[TEST_SECTOR_BYTES], after[TEST_SECTOR_BYTES];
    static const char *const devices[] = TEST_IDE_DEVICES;
    char *end;
    unsigned long drv, lba;
    int rc, failed = 0;
    if (argc != 3) {
        api->kprintf(0xE1, "usage: disk_auth_test <IDE drive> <unused LBA >=18>\n");
        return 2;
    }
    drv = strtoul(argv[1], &end, 10);
    if (!*argv[1] || *end || drv >= sizeof(devices) / sizeof(devices[0])) return 2;
    lba = strtoul(argv[2], &end, 10);
    if (!*argv[2] || *end || lba < TEST_FIRST_SAFE_LBA || lba == 0xffffffffUL) return 2;
    for (int i = 0; i < 3; i++) {
        rc = i == 2 ? api->dev_blk_read(devices[drv], lba, 1, before) :
                      api->ide_read_sector(drv, lba, before);
        if (rc < 0) { api->kprintf(0xE1, "read failed; no write attempted\n"); return 2; }
        if (i == 0) rc = api->ide_write_sector(drv, lba, before);
        else if (i == 1) rc = api->ide_write_sectors(drv, lba, 1, before);
        else rc = api->dev_blk_write(devices[drv], lba, 1, before);
        if (rc != -1) failed++;
        api->kprintf(0xE1, "disk auth API %d: rc=%d (expected -1)\n", i, rc);
        rc = i == 2 ? api->dev_blk_read(devices[drv], lba, 1, after) :
                      api->ide_read_sector(drv, lba, after);
        if (rc < 0 || memcmp(before, after, sizeof(before))) failed++;
    }
    api->kprintf(0xE1, "disk_auth_test: %s\n", failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
