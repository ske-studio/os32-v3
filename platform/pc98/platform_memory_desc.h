/* PC-9821 geometry. Include through kernel/platform_memory.h. */
#ifndef PC98_PLATFORM_MEMORY_DESC_H
#define PC98_PLATFORM_MEMORY_DESC_H

#include "memmap.h"

static const struct platform_memory_geometry platform_memory_geometry = {
    MEM_SYSTEM_SPACE_BASE, MEM_SYSTEM_SPACE_BASE, MEM_SYSTEM_SPACE_END,
    MEM_HIGH_RAM_BASE, MEM_PHYS_RAM_CEILING
};

#endif /* PC98_PLATFORM_MEMORY_DESC_H */
