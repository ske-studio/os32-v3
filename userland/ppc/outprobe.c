#include "os32api.h"

int main(int argc, char **argv, KernelAPI *api)
{
    const char marker[] = "FD1 PROBE\n";
    (void)argc;
    (void)argv;
    api->exec_run("/bin/hello.bin ok");
    api->shell_putchar('H', 7);
    api->shell_putchar('\n', 7);
    return api->sys_write(1, marker, sizeof(marker) - 1) == sizeof(marker) - 1 ? 0 : 1;
}
