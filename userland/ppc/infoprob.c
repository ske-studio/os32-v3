#include "os32api.h"
#include <string.h>

static int report(KernelAPI *api, const char *message, int status)
{
    while (*message)
        api->shell_putchar(*message++, 7);
    return status;
}

int main(int argc, char **argv, KernelAPI *api)
{
    char build[32];
    u32 lo1 = ~0u, hi1 = ~0u, lo2 = ~0u, hi2 = ~0u;

    (void)argc;
    (void)argv;
    if (api->version < KAPI_VERSION)
        return report(api, "INFO version failed\n", 1);
    memset(build, 0x5a, sizeof(build));
    api->sys_get_build_info(build, sizeof(build));
    /* __DATE__ + space + __TIME__, including a terminating NUL. */
    if (build[0] == 0 || build[11] != ' ' || build[14] != ':' ||
        build[17] != ':' || build[20] != 0)
        return report(api, "INFO build failed\n", 2);
    if (api->sys_time_now(&lo1, &hi1) || api->sys_time_now(&lo2, &hi2))
        return report(api, "INFO clock unavailable\n", 3);
    /* A freshly booted guest has less than 2^32 microseconds of uptime. */
    if (hi1 || hi2 || !lo1 || lo2 == ~0u || lo2 < lo1)
        return report(api, "INFO clock words failed\n", 4);
    return report(api, "INFO OK\n", 0);
}
