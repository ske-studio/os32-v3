#ifndef ARCH_CTX_OBSERVER_H
#define ARCH_CTX_OBSERVER_H
#include "os32_kapi_shared.h"
#include "exec/shell_service.h"

typedef u32 ctx_observer(unsigned, unsigned, char **, KernelAPI *, ShellService *,
                         unsigned, int (*)(int), u32);
#endif
