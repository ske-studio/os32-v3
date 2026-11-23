#ifndef PLATFORM_HWB_ADDR_H
#define PLATFORM_HWB_ADDR_H
#include "shell.h"

static inline u32 platform_hwb_bar(u32 bdf, u32 bar, u32 space, u32 base, u32 offset, u32 bytes, u32 *addr)
{
    /* HE_BAR_COUNT is the sentinel for an absolute address. */
    if (bar == HE_BAR_COUNT) {
        *addr = offset;
        return HW_OK;
    }
    struct hw_range_req q = {{HW_VERSION, 7, space, 1, bdf, base, 0, 0}, bar, offset, bytes};
    if (g_api->kdebug_call(HW_OP, &q, sizeof(q))) return HW_NOSYS;
    if (q.req.status == HW_OK) *addr = q.req.value;
    return q.req.status;
}

#endif
