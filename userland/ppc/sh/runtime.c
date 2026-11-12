#include "runtime.h"

extern KernelAPI *kapi;
static KernelAPI runtime_api;

static void put_terminal(void *ctx, size_t index, char ch)
{
    (void)index;
    runtime_api.shell_putchar(ch, *(const u8 *)ctx);
}

static void runtime_kprintf(u8 attr, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    sh_vformat(put_terminal, &attr, fmt, args);
    va_end(args);
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
