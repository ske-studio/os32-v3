/* uPD720101: Renesas S16265EJ5V0DS pp.1,3; FreeBSD ohci_pci_match.
 * Take BAR0 from the boot enumeration cache. Stage (c) owns firmware handoff. */
#include "usb_probe.h"
#include "sys/dev/pci/pci.h"
#include "sys/dev/pci/pci_barsize.h"

static u16 blocked_slots[PCI_MAX_DEVS];
static unsigned blocked_count;

static int ohci_resource(const struct pci_dev *dev, struct usb_pci_fn *out)
{
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

int pc98_usb_ohci_function(const struct pci_dev *dev, struct usb_pci_fn *out)
{
    if (!pc98_usb_is_ohci(dev)) return -1;

    for (unsigned i = 0; i < blocked_count; ++i)
        if (blocked_slots[i] == ((dev->bus << 8) | dev->dev)) return -1;

    return ohci_resource(dev, out);
}

u32 usbp_cfg_read32(const struct usb_pci_fn *f, u32 reg)
{
    return pci_cfg_read32(f->bus, f->dev, f->fn, reg);
}

#include "sys/dev/usb/ohci_priv.h"
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

/* OpenHCI 1.0a §5.1, FreeBSD ohci.c (ohci_init). */
#include "sys/dev/usb/ohci_regs.h"

static u8 ohci_quiet[PCI_MAX_DEVS];

static struct ohci_firmware firmware[PCI_MAX_DEVS];

int pc98_usb_ohci_quiet(u32 index, struct ohci_firmware *saved)
{
    if (index >= PCI_MAX_DEVS || !ohci_quiet[index]) return 0;
    *saved = firmware[index];
    return 1;
}

static int takeover(struct usb_hc *hc)
{
    volatile u8 *regs = hc->regs;
    if (usbp_cfg_cmd(&hc->pci, PCI_CMD_MEM_ENABLE, 0)) return -1;
    u32 ctl = mmio_rd32_le(regs + OHCI_CONTROL);
    hc->firmware = ohci_firmware_read(regs, ctl);
    if (ctl & OHCI_CTL_IR) {
        mmio_wr32_le(regs + OHCI_COMMAND_STATUS, OHCI_CMD_OCR);
        for (unsigned i = 0; i < 100; i++) {
            usbp_udelay(1000);
            if (!(mmio_rd32_le(regs + OHCI_CONTROL) & OHCI_CTL_IR)) break;
        }
    }
    int owned = !(mmio_rd32_le(regs + OHCI_CONTROL) & OHCI_CTL_IR);
    /* FreeBSD controller/ohci.c:ohci_controller_init */
    if (!owned)
        kprintf(KLOG_ATTR, "[usb] %u:%u.%u warning: IR remains after OCR\n",
                hc->pci.bus, hc->pci.dev, hc->pci.fn);
    mmio_wr32_le(regs + OHCI_INTERRUPT_DISABLE, OHCI_INTR_ALL);
    /* NetBSD dev/usb/ohci.c:ohci_init preserves firmware RWC. */
    mmio_wr32_le(regs + OHCI_CONTROL, (ctl & OHCI_CTL_RWC) | OHCI_CTL_RESET);
    usbp_udelay(100000);
    if (usbp_cfg_cmd(&hc->pci, 0, PCI_CMD_BUS_MASTER)) return -1;
    return owned && !(usbp_cfg_read32(&hc->pci, PCI_CFG_COMMAND) & PCI_CMD_BUS_MASTER) &&
           !(mmio_rd32_le(regs + OHCI_CONTROL) & OHCI_CTL_HCFS_MASK) &&
           !(mmio_rd32_le(regs + OHCI_INTERRUPT_ENABLE) & OHCI_INTR_MIE) ? 0 : -1;
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
    /* Stop even companions whose EHCI routing prevents a later start. */
    for (u32 i = 0; pci_get(i, &dev) == 0; ++i) {
        ohci_quiet[i] = 0;
        if (!pc98_usb_is_ohci(&dev)) continue;
        struct usb_hc hc = {.pci = {.bus=dev.bus, .dev=dev.dev, .fn=dev.fn}};
        if (!ohci_resource(&dev, &hc.pci) && !usbp_mmio_map(&hc.pci, 0, &hc.regs)) {
            ohci_quiet[i] = takeover(&hc) == 0;
            firmware[i] = hc.firmware;
            usbp_mmio_unmap(&hc.pci, 0);
        }
        if (!ohci_quiet[i]) {
            usbp_cfg_cmd(&hc.pci, 0, PCI_CMD_BUS_MASTER | PCI_CMD_MEM_ENABLE);
            kprintf(KLOG_ATTR, "[usb] %u:%u.%u OHCI quiesce failed\n", dev.bus, dev.dev, dev.fn);
        }
    }
}
