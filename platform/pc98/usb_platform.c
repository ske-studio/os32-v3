/* uPD720101: Renesas S16265EJ5V0DS pp.1,3; FreeBSD ohci_pci_match.
 * Take BAR0 from the boot enumeration cache. Stage (c) owns firmware handoff. */
#include "usb_probe.h"
#include "drivers/pci/pci.h"
#include "drivers/pci/pci_barsize.h"

int pc98_usb_ohci_function(const struct pci_dev *dev, struct usb_pci_fn *out)
{
    if (dev->vendor != 0x1033 || dev->device != 0x0035 ||
        dev->class != 0x0c || dev->subclass != 0x03 || dev->progif != 0x10)
        return -1;

    struct pci_bar_resource bar;
    if (pci_bar_get(dev->bus, dev->dev, dev->fn, PCI_CFG_BAR0, &bar) ||
        bar.kind != PCI_BAR_MEM32 || bar.prefetch || bar.unassigned ||
        !bar.base || !bar.len)
        return -1;

    *out = (struct usb_pci_fn){
        .bus = dev->bus, .dev = dev->dev, .fn = dev->fn, .irq = dev->irq_line,
        .vendor = dev->vendor, .device = dev->device,
        .bar_base = {bar.base}, .bar_size = {bar.len},
    };
    return 0;
}

u32 usbp_cfg_read32(const struct usb_pci_fn *f, u32 reg)
{
    return pci_cfg_read32(f->bus, f->dev, f->fn, reg);
}
