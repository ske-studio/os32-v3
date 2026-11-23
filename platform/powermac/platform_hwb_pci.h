#ifndef PLATFORM_HWB_PCI_H
#define PLATFORM_HWB_PCI_H

#include "diag_slots.h"

static inline u32 platform_hwb_cfg_read(u32 bdf, u32 offset, u32 width, u32 *value)
{
    return kdebug_import_slots.cfg_read(bdf, offset, width, value);
}

static inline u32 platform_hwb_cfg_write(u32 bdf, u32 offset, u32 width, u32 value)
{
    return kdebug_import_slots.cfg_write(bdf, offset, width, value);
}

#endif
