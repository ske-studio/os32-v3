#include "os32api.h"
#include <stdio.h>

int main(int argc, char **argv, KernelAPI *api)
{
    BootImageInfo info = {0};
    char line[80];

    (void)argc;
    (void)argv;
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
