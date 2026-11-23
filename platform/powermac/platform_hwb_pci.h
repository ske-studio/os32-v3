#ifndef PLATFORM_HWB_PCI_H
#define PLATFORM_HWB_PCI_H

static inline u32 platform_hwb_cfg_read(u32 bdf, u32 offset, u32 width, u32 *value)
{
    (void)bdf;
    (void)offset;
    (void)width;
    (void)value;
    return HW_NOSYS;
}

static inline u32 platform_hwb_cfg_write(u32 bdf, u32 offset, u32 width, u32 value)
{
    (void)bdf;
    (void)offset;
    (void)width;
    (void)value;
    return HW_NOSYS;
}

#endif
