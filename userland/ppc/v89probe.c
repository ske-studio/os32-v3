/* KAPI_CAPACITY_V89_DESIGN sections 1 and 4. Read the published wire offsets. */
#include "os32api.h"
#include <stdio.h>

int main(int argc, char **argv, KernelAPI *api)
{
    volatile const u32 *table = (volatile const u32 *)api;
    char line[96];
    if (argc == 2) {
        unsigned slot = argv[1][0] == 'a' ? 298 : 383;
        int (*reserved)(void) = (int (*)(void))table[2 + slot];
        int result = reserved();
        snprintf(line, sizeof(line), "V89 slot=%u result=%d\n", slot, result);
        for (char *p = line; *p; ++p) api->shell_putchar(*p, 7);
        return 1; /* The range check should terminate this child. */
    }
    api->gfx_init();
    int result = api->io_pump(0);
    snprintf(line, sizeof(line), "V89 return=%d heap=%u shm=%u\n",
             result, (unsigned)table[0x608 / 4], (unsigned)table[0x60c / 4]);
    for (char *p = line; *p; ++p) api->shell_putchar(*p, 7);
    return 0;
}
