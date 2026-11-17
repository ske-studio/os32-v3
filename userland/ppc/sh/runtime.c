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

KernelAPI *sh_runtime_init(KernelAPI *api)
{
    runtime_api = *api;
    runtime_api.kprintf = runtime_kprintf;
    /* Keep the kernel UTF-8 endpoint: bytewise putchar cannot decode glyphs. */
    kapi = &runtime_api;
    return &runtime_api;
}
