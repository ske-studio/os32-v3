/* OS tick counter; storage is owned by kernel/time/tick.c. */
#ifndef KERNEL_TICK_H
#define KERNEL_TICK_H

#ifndef __ASSEMBLER__
#include "types.h"
extern volatile u32 tick_count;
#endif

#endif /* KERNEL_TICK_H */
