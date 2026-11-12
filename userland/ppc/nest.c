#include "os32api.h"

static void put_text(KernelAPI *api, const char *text)
{
    while (*text) api->shell_putchar(*text++, 7);
}

static void put_number(KernelAPI *api, int value)
{
    char digits[12];
    unsigned int n = value < 0 ? 0u - (unsigned int)value : (unsigned int)value;
    int count = 0;
    if (value < 0) api->shell_putchar('-', 7);
    do { digits[count++] = '0' + n % 10; n /= 10; } while (n);
    while (count) api->shell_putchar(digits[--count], 7);
}

int main(int argc, char **argv, KernelAPI *api)
{
    volatile int parent = 12345;
    int round;
    (void)argc;
    (void)argv;
    for (round = 0; round < 2; ++round) {
        int kind = EXEC_KIND_NONE, code = 0;
        api->exec_run("/bin/hello.bin ok");
        api->exec_last_result(&kind, &code);
        put_text(api, "nest kind=");
        put_number(api, kind);
        put_text(api, " code=");
        put_number(api, code);
        put_text(api, " parent=");
        put_number(api, parent);
        put_text(api, "\n");
    }
    return 5;
}
