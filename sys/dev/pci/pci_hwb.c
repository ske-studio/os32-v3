/* PCI configuration mechanism #1; see HW_BASIC_DESIGN §§3, 6 E. */
#include "pci_hwb.h"
#include "platform_hwb_pci.h"

static int valid(u32 bdf, u32 offset, u32 width)
{
    return bdf <= HW_BDF_MASK && (width == 1 || width == 2 || width == 4) &&
           offset < HW_PCI_CONFIG_BYTES && width <= HW_PCI_CONFIG_BYTES - offset &&
           !(offset & (width - 1));
}

u32 pci_hwb_read(u32 bdf, u32 offset, u32 width, u32 *value)
{
    if (!value) return HW_ARG;
    *value = 0;
    if (!valid(bdf, offset, width)) return HW_ARG;
    return platform_hwb_cfg_read(bdf, offset, width, value);
}

u32 pci_hwb_write(u32 bdf, u32 offset, u32 width, u32 value)
{
    if (!valid(bdf, offset, width) ||
        (width == 1 && value > U8_MAX) || (width == 2 && value > U16_MAX)) return HW_ARG;
    return platform_hwb_cfg_write(bdf, offset, width, value);
}
