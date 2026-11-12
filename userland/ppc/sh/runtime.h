#ifndef PPC_SH_RUNTIME_H
#define PPC_SH_RUNTIME_H

#include <stdarg.h>
#include <stddef.h>
#include "os32api.h"

int sh_vsnprintf(char *out, size_t size, const char *fmt, va_list args);
KernelAPI *sh_runtime_init(KernelAPI *api);

#endif
