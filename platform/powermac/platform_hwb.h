#ifndef PLATFORM_HWB_H
#define PLATFORM_HWB_H

#include "kdebug/hwb_wire.h"

#include "diag_slots.h"

static inline u32 platform_hwb_abort_take(void)
{
    return kdebug_import_slots.abort_take();
}

static inline u32 platform_hwb_phys_check(u32 addr, u32 width, u32 bytes)
{
    return kdebug_import_slots.phys_check(addr, width, bytes);
}

static inline u32 platform_hwb_phys(struct hw_req *req)
{
    return kdebug_import_slots.phys(req);
}

static inline u32 platform_hwb_block(struct hw_block_req *block)
{
    return kdebug_import_slots.block(block);
}

static inline u32 platform_hwb_io(struct hw_req *req)
{
    (void)req;
    return HW_NOSYS;
}

#endif
