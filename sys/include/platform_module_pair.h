#ifndef PLATFORM_MODULE_PAIR_H
#define PLATFORM_MODULE_PAIR_H

#include "types.h"

/* Write the verified kernel/root pair only on success (1). */
int platform_module_pair(u32 *out);

#endif
