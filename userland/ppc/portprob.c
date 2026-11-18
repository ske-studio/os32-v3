#include "os32api.h"

static int report(KernelAPI *api, const char *text, int status)
{
    while (*text)
        api->shell_putchar(*text++, 7);
    return status;
}

static int at_rate(KernelAPI *api, u32 expected)
{
    u32 mode, baud, fifo;

    return api->serial_get_status(&mode, &baud, &fifo) == 0 &&
           mode == 0 && baud == expected && fifo == 1;
}

int main(int argc, char **argv, KernelAPI *api)
{
    (void)argv;

    api->serial_init(9600);
    if (!api->serial_is_initialized())
        return report(api, "PORT initialized failed\n", 1);
    if (!at_rate(api, 9600))
        return report(api, "PORT initial rate failed\n", 2);
    if (api->serial_init_vfast(38400) != -1 || !at_rate(api, 38400))
        return report(api, "PORT alternate rate failed\n", 3);
    if (api->serial_init_vfast(12345) != -2 || !at_rate(api, 38400))
        return report(api, "PORT rejection failed\n", 4);
    if (api->serial_trygetchar() != -1)
        return report(api, "PORT empty try failed\n", 5);
    if (argc == 1)
        return report(api, "PORT settings OK\n", 0);

    /* The host waits for '?' before sending anything on channel A. */
    if (api->serial_putchar('?'))
        return report(api, "PORT ready TX failed\n", 6);
    const unsigned char expected[] = { 0xa5, 0x5a };
    for (unsigned i = 0; i < sizeof(expected); ++i) {
        int byte = api->serial_getchar();
        if (byte != expected[i])
            return report(api, "PORT receive failed\n", 7);
        if (api->serial_putchar((u8)byte))
            return report(api, "PORT echo TX failed\n", 8);
    }
    if (api->serial_trygetchar() != -1)
        return report(api, "PORT trailing try failed\n", 9);
    return report(api, "PORT echo OK\n", 0);
}
