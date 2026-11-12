#include "runtime.h"

extern KernelAPI *kapi;
static KernelAPI runtime_api;

static void runtime_kprintf(u8 attr, const char *fmt, ...)
{
    char buffer[1024];
    va_list args;
    const char *p;
    va_start(args, fmt);
    sh_vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    for (p = buffer; *p; ++p)
        runtime_api.shell_putchar(*p, attr);
}

static void runtime_print_utf8(const char *text, u8 color)
{
    (void)color;
    while (*text)
        runtime_api.shell_putchar(*text++, 0);
}

KernelAPI *sh_runtime_init(KernelAPI *api)
{
    runtime_api = *api;
    runtime_api.kprintf = runtime_kprintf;
    runtime_api.shell_print_utf8 = runtime_print_utf8;
    kapi = &runtime_api;
    return &runtime_api;
}
