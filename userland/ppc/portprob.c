#include "os32api.h"

static int report(KernelAPI *api, const char *text, int status)
{
    while (*text)
        api->shell_putchar(*text++, 7);
    return status;
}

int main(int argc, char **argv, KernelAPI *api)
{
    (void)argv;

    api->serial_init(9600);
    if (!api->serial_is_initialized())
        return report(api, "PORT initialized failed\n", 1);
    u32 mode, baud, fifo;
    /* KAPI の出力の確かめ (TASK_KAPI_OUTPUT_GUARD) は NULL の欄もアプリを止める — 全欄を渡す */
    if (api->serial_get_status(&mode, &baud, &fifo) != 0 || mode != 0 || fifo != 1)
        return report(api, "PORT status failed\n", 2);
    if (api->serial_init_vfast(38400) != -1)
        return report(api, "PORT alternate rate failed\n", 3);
    if (api->serial_init_vfast(12345) != -2)
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
