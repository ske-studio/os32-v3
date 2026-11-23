#ifndef PLATFORM_HWB_ADDR_H
#define PLATFORM_HWB_ADDR_H

static inline u32 platform_hwb_bar(u32 bdf, u32 bar, u32 space, u32 base, u32 offset, u32 bytes, u32 *addr)
{
    (void)bdf;
    (void)bar;
    (void)space;
    (void)bytes;
    *addr = base + offset;
    return HW_OK;
}

#endif
