#ifndef PPC_EXEC_CACHE_H
#define PPC_EXEC_CACHE_H

#include "types.h"

/* dst is the kernel-root P2V identity alias; bytes is nonzero. */
void arch_exec_publish(void *dst, u32 bytes);

#endif
