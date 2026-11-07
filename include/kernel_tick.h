/* OS tick counter; storage remains in the architecture interrupt code. */
#ifndef KERNEL_TICK_H
#define KERNEL_TICK_H

#ifndef __ASSEMBLER__
#include "types.h"
extern volatile u32 tick_count;
#endif

#endif /* KERNEL_TICK_H */
