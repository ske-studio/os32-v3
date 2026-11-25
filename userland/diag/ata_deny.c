/* Ordinary child installed only in the acceptance RAM root. */
#include "os32api.h"

int main(int argc, char **argv, KernelAPI *api)
{
    static unsigned char data[1025] __attribute__((aligned(2)));
    (void)argc;
    (void)argv;
    int one = api->ide_write_sector(0, 32, data + 1);
    int many = api->ide_write_sectors(0, 64, 2, data + 1);
    return one == -1 && many == -1 ? 73 : 1;
}
