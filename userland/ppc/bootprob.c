#include "os32api.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv, KernelAPI *api)
{
    BootImageInfo info = {
        .source = 255,
        .crc_valid = 255,
        .image_size = 1,
        .image_crc = 1,
    };
    char line[80];

    (void)argc;
    (void)argv;
    /* ISO9660 primary descriptor, sector 16. Use the original bytes for
     * the write probe so a faulty writable device preserves the fixture. */
    static u8 sector[2048];
    if (api->dev_blk_read("rd0", 16, 1, sector) ||
        memcmp(sector, "\001CD001\001", 7) ||
        api->dev_blk_write("rd0", 16, 1, sector) >= 0)
        return 2;
    int rc = api->boot_image_info(&info);
    if (rc) {
        snprintf(line, sizeof(line), "BOOT info error %d\n", rc);
    } else {
        snprintf(line, sizeof(line), "BOOT %u %u %u %08x\n",
                 (unsigned)info.source, (unsigned)info.crc_valid,
                 (unsigned)info.image_size, (unsigned)info.image_crc);
    }
    for (const char *p = line; *p; ++p)
        api->shell_putchar(*p, 7);
    return rc ? 1 : 0;
}
