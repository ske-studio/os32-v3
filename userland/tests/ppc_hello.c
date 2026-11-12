#include "os32api.h"

int main(int argc, char **argv, KernelAPI *api)
{
    const char *name = argv[0];
    const char *p;
    const char *end;

    for (p = name; *p; ++p)
        if (*p == '/')
            name = p + 1;
    for (end = name; *end && *end != '.'; ++end) {}
    for (p = name; p < end; ++p)
        api->shell_putchar(*p, 7);
    if (argc > 1) {
        api->shell_putchar(' ', 7);
        for (p = argv[1]; *p; ++p)
            api->shell_putchar(*p, 7);
    }
    api->shell_putchar('\n', 7);
    return 7;
}
