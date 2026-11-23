#ifndef PLATFORM_HWB_H
#define PLATFORM_HWB_H

#include "kdebug/hwb_wire.h"

static inline u32 platform_hwb_abort_take(void)
{
    return 0;
}

static inline u32 platform_hwb_phys_check(u32 addr, u32 width, u32 bytes)
{
    (void)addr;
    (void)width;
    (void)bytes;
    return HW_NOSYS;
}

static inline u32 platform_hwb_phys(struct hw_req *req)
{
    (void)req;
    return HW_NOSYS;
}

static inline u32 platform_hwb_block(struct hw_block_req *block)
{
    (void)block;
    return HW_NOSYS;
}

static inline u32 platform_hwb_io(struct hw_req *req)
{
    (void)req;
    return HW_NOSYS;
}

#endif
