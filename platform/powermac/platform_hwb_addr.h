#ifndef PLATFORM_HWB_ADDR_H
#define PLATFORM_HWB_ADDR_H

static inline u32 platform_hwb_bar(u32 bdf, u32 bar, u32 space, u32 base, u32 offset, u32 bytes, u32 *addr)
{
    (void)bdf;
    /* HE_BAR_COUNT is the sentinel for an absolute address. */
    if (bar == HE_BAR_COUNT) {
        *addr = offset;
        return HW_OK;
    }
    (void)space;
    (void)base;
    (void)bytes;
    return HW_NOSYS;
}

#endif
