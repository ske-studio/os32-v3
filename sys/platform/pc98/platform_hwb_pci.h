#ifndef PLATFORM_HWB_PCI_H
#define PLATFORM_HWB_PCI_H

#include "sys/dev/pci/pci_decode.h"
#include "io.h"

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

static inline u32 platform_hwb_cfg_read(u32 bdf, u32 offset, u32 width, u32 *value)
{
    return access_cfg(bdf, offset, width, value, 0);
}

/* Pass the caller's value directly to the shared read/write transaction. */
#define platform_hwb_cfg_write(bdf, offset, width, value) \
    access_cfg((bdf), (offset), (width), &(value), 1)

#endif
