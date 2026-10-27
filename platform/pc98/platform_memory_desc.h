/* PC-9821 geometry and attributes. Include through kernel/platform_memory.h. */
#ifndef PC98_PLATFORM_MEMORY_DESC_H
#define PC98_PLATFORM_MEMORY_DESC_H

#include "memmap.h"
#include "pc98.h"

static const struct platform_memory_geometry platform_memory_geometry = {
    MEM_SYSTEM_SPACE_BASE, MEM_SYSTEM_SPACE_BASE, MEM_SYSTEM_SPACE_END,
    MEM_HIGH_RAM_BASE, MEM_PHYS_RAM_CEILING
};

static const struct platform_memory_attributes platform_memory_attributes = {
    { {TVRAM_CHAR_BASE, TVRAM_CG_WINDOW},
      {GVRAM_PLANE_B, GVRAM_BRG_END}, {GVRAM_PLANE_I, GVRAM_I_END} },
    {MEM_BIOS_ROM_START, MEM_BIOS_ROM_END + 1},
    {MEM_CONV_END, MEM_BIOS_ROM_END + 1}
};

#endif /* PC98_PLATFORM_MEMORY_DESC_H */
