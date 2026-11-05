/* PCI configuration mechanism #1; see HW_BASIC_DESIGN §§3, 6 E. */
#include "pci_hwb.h"
#include "pci_decode.h"
#include "io.h"

static int valid(u32 bdf, u32 offset, u32 width)
{
    return bdf <= HW_BDF_MASK && (width == 1 || width == 2 || width == 4) &&
           offset < HW_PCI_CONFIG_BYTES && width <= HW_PCI_CONFIG_BYTES - offset &&
           !(offset & (width - 1));
}

static u32 access_cfg(u32 bdf, u32 offset, u32 width, u32 *value, int write)
{
    unsigned int flags = irq_save();
    u32 saved = inpd(PCI_CFG_ADDR_PORT), a, b;
    outpd(PCI_CFG_ADDR_PORT, PCI_MECH1_PROBE_A);
    a = inpd(PCI_CFG_ADDR_PORT);
    outpd(PCI_CFG_ADDR_PORT, PCI_MECH1_PROBE_B);
    b = inpd(PCI_CFG_ADDR_PORT);
    if (a == PCI_MECH1_PROBE_A && b == PCI_MECH1_PROBE_B) {
        u32 address = PCI_CFG_ADDR_ENABLE |
            (HW_BDF_BUS(bdf) << PCI_CFG_ADDR_BUS_SHIFT) |
            (HW_BDF_DEV(bdf) << PCI_CFG_ADDR_DEV_SHIFT) |
            (HW_BDF_FN(bdf) << PCI_CFG_ADDR_FN_SHIFT) |
            (offset & PCI_CFG_ADDR_REG_MASK);
        unsigned int port = PCI_CFG_DATA_PORT + (offset & 3u);
        outpd(PCI_CFG_ADDR_PORT, address);
        if (write) {
            if (width == 1) outp(port, *value);
            else if (width == 2) outpw(port, *value);
            else outpd(port, *value);
        } else {
            if (width == 1) *value = inp(port);
            else if (width == 2) *value = inpw(port);
            else *value = inpd(port);
        }
    }
    outpd(PCI_CFG_ADDR_PORT, saved);
    irq_restore(flags);
    return (a == PCI_MECH1_PROBE_A && b == PCI_MECH1_PROBE_B) ? HW_OK : HW_NOSYS;
}

u32 pci_hwb_read(u32 bdf, u32 offset, u32 width, u32 *value)
{
    if (!value) return HW_ARG;
    *value = 0;
    if (!valid(bdf, offset, width)) return HW_ARG;
    return access_cfg(bdf, offset, width, value, 0);
}

u32 pci_hwb_write(u32 bdf, u32 offset, u32 width, u32 value)
{
    if (!valid(bdf, offset, width) ||
        (width == 1 && value > U8_MAX) || (width == 2 && value > U16_MAX)) return HW_ARG;
    return access_cfg(bdf, offset, width, &value, 1);
}
