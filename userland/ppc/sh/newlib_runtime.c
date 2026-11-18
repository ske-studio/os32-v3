/* newlib owns formatting/stdio/string; shared CRT syscalls/help own ls/help.
 * Only the attributed KAPI formatter and USER API copy remain shell-specific.
 * Keep UTF-8 on the kernel path. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>

extern KernelAPI *kapi;
static KernelAPI runtime_api;

static void runtime_kprintf(u8 attr, const char *fmt, ...)
{
    va_list args;
    int size;
    char *text;

    va_start(args, fmt);
    size = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (size < 0) return;
    text = malloc((size_t)size + 1);
    if (!text) return;
    va_start(args, fmt);
    vsnprintf(text, (size_t)size + 1, fmt, args);
    va_end(args);
    for (int i = 0; i < size; ++i) runtime_api.shell_putchar(text[i], attr);
    free(text);
}

KernelAPI *sh_runtime_init(KernelAPI *api)
{
    runtime_api = *api;
    runtime_api.kprintf = runtime_kprintf;
    /* Keep the kernel UTF-8 endpoint: bytewise putchar cannot decode glyphs. */
    kapi = &runtime_api;
    return &runtime_api;
}
