/* uPD720101: Renesas S16265EJ5V0DS pp.1,3; FreeBSD ohci_pci_match.
 * Take BAR0 from the boot enumeration cache. Stage (c) owns firmware handoff. */
#include "usb_probe.h"
#include "drivers/pci/pci.h"
#include "drivers/pci/pci_barsize.h"

static u16 blocked_slots[PCI_MAX_DEVS];
static unsigned blocked_count;

int pc98_usb_ohci_function(const struct pci_dev *dev, struct usb_pci_fn *out)
{
    if (dev->vendor != 0x1033 || dev->device != 0x0035 ||
        dev->class != 0x0c || dev->subclass != 0x03 || dev->progif != 0x10)
        return -1;

    for (unsigned i = 0; i < blocked_count; ++i)
        if (blocked_slots[i] == ((dev->bus << 8) | dev->dev)) return -1;

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

#include "drivers/usb/ohci_priv.h"
#include "kprintf.h"

#define EHCI_BAR_BYTES 256u

static int quiesce_function(const struct pci_dev *dev, u32 *blocked)
{
    struct usb_hc hc = {.pci = {.bus=dev->bus, .dev=dev->dev, .fn=dev->fn,
                                .vendor=dev->vendor, .device=dev->device}};
    struct pci_bar_resource bar;
    *blocked = 1;
    if (pci_bar_get(dev->bus, dev->dev, dev->fn, PCI_CFG_BAR0, &bar) ||
        bar.kind != PCI_BAR_MEM32 || bar.prefetch || bar.len != EHCI_BAR_BYTES)
        goto disable;
    if (bar.unassigned || !bar.base) {
        int rc = usbp_cfg_cmd(&hc.pci, 0, PCI_CMD_BUS_MASTER | PCI_CMD_MEM_ENABLE);
        *blocked = rc != 0; /* Without a BAR, reset routing belongs to OHCI. */
        return rc;
    }
    hc.pci.bar_base[0] = bar.base;
    hc.pci.bar_size[0] = bar.len;
    if (usbp_mmio_map(&hc.pci, 0, &hc.regs)) goto disable;
    if (usbp_cfg_cmd(&hc.pci, PCI_CMD_MEM_ENABLE, 0)) {
        usbp_mmio_unmap(&hc.pci, 0);
        goto disable;
    }
    int rc = ehci_early_quiesce(&hc, blocked);
    usbp_mmio_unmap(&hc.pci, 0);
    return rc;
disable:
    usbp_cfg_cmd(&hc.pci, 0, PCI_CMD_BUS_MASTER | PCI_CMD_MEM_ENABLE);
    return -1;
}

void pc98_usb_early_quiesce(void)
{
    struct pci_dev dev;
    blocked_count = 0;
    for (u32 i = 0; pci_get(i, &dev) == 0; ++i) {
        if (dev.vendor != 0x1033 || dev.device != 0x00e0 ||
            dev.class != 12 || dev.subclass != 3 || dev.progif != 0x20) continue;
        u32 blocked;
        int rc = quiesce_function(&dev, &blocked);
        if (blocked) blocked_slots[blocked_count++] = (dev.bus << 8) | dev.dev;
        kprintf(KLOG_ATTR, "[usb] %u:%u.%u EHCI %s, OHCI %s\n", dev.bus, dev.dev, dev.fn,
                rc ? "failed" : "quiet", blocked ? "blocked" : "ready");
    }
}
