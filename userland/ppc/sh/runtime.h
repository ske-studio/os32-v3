#ifndef PPC_SH_RUNTIME_H
#define PPC_SH_RUNTIME_H

#include <stdarg.h>
#include <stddef.h>
#include "os32api.h"

typedef void (*sh_put_fn)(void *ctx, size_t index, char ch);
int sh_vformat(sh_put_fn put, void *ctx, const char *fmt, va_list args);
int sh_vsnprintf(char *out, size_t size, const char *fmt, va_list args);
KernelAPI *sh_runtime_init(KernelAPI *api);

#endif
