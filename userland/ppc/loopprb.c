#include "os32api.h"
#include <stdio.h>

int main(int argc, char **argv, KernelAPI *api)
{
    u32 total = 77;
    int bps = 88;
    char line[64];
    (void)argc;
    (void)argv;
    int attach = api->loop_attach("/host/loop.img", 0);
    int status = api->loop_status(0, &total, &bps);
    api->loop_detach(0);
    int after = api->loop_status(0, &total, &bps);
    snprintf(line, sizeof(line), "LOOP %d %d %u %d %d\n",
             attach, status, (unsigned)total, bps, after);
    for (const char *p = line; *p; ++p)
        api->shell_putchar(*p, 7);
    return 0;
}
