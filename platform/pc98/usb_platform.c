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

/* Renesas S16265EJ5V0DS; EHCI 1.0 Appendix B;
 * FreeBSD f22d6599 ehci_pci.c:552-595. */
#include "drivers/usb/ohci_priv.h"
#include "kprintf.h"

#define EHCI_HCCPARAMS 8u
#define EHCI_BIOS_OWNED (1u << 16)
#define EHCI_OS_BYTE 3u
#define EHCI_USBINTR 8u
#define EHCI_CF 0x40u
#define EHCI_BAR_BYTES 256u

/* The parallel resource stage supplies these two existing platform hooks. */
#pragma weak usbp_mmio_map
#pragma weak usbp_mmio_unmap

static int take_ownership(struct usb_hc *hc)
{
    volatile u8 *regs = hc->regs;
    u32 ptr = (mmio_rd32_le(regs + EHCI_HCCPARAMS) >> 8) & 0xff;
    for (unsigned n = 0; ptr && n < 48; ++n) {
        if (ptr < 0x40 || (ptr & 3)) return -1;
        u32 cap = usbp_cfg_read32(&hc->pci, ptr);
        if ((cap & 0xff) == 1 && (cap & EHCI_BIOS_OWNED)) {
            if (usbp_cfg_write8(&hc->pci, ptr + EHCI_OS_BYTE, 1)) return -1;
            unsigned polls = 0;
            while (usbp_cfg_read32(&hc->pci, ptr) & EHCI_BIOS_OWNED) {
                if (polls++ == 500) return -1;
                usbp_udelay(10000);
            }
        }
        ptr = (cap >> 8) & 0xff;
    }
    return ptr ? -1 : 0;
}

static int quiesce_function(const struct pci_dev *dev, u32 *blocked)
{
    struct usb_hc hc = {.pci = {.bus=dev->bus, .dev=dev->dev, .fn=dev->fn,
                                .vendor=dev->vendor, .device=dev->device}};
    struct pci_bar_resource bar;
    *blocked = 1;
    if (pci_bar_get(dev->bus, dev->dev, dev->fn, PCI_CFG_BAR0, &bar) ||
        bar.kind != PCI_BAR_MEM32 || bar.prefetch || bar.len != EHCI_BAR_BYTES)
        return -1;
    if (bar.unassigned || !bar.base) {
        *blocked = 0; /* Reset routing, no firmware MMIO on this function. */
        return usbp_cfg_cmd(&hc.pci, 0, PCI_CMD_BUS_MASTER | PCI_CMD_MEM_ENABLE);
    }
    hc.pci.bar_base[0] = bar.base;
    hc.pci.bar_size[0] = bar.len;
    if (usbp_mmio_map(&hc.pci, 0, &hc.regs)) return -1;
    int rc = -1;
    if (usbp_cfg_cmd(&hc.pci, PCI_CMD_MEM_ENABLE, 0) == 0) {
        volatile u8 *regs = hc.regs;
        u32 op = mmio_rd32_le(regs) & 0xff;
        if (op >= 0x10 && !(op & 3) && op + EHCI_CF + 4 <= bar.len) {
            if (take_ownership(&hc) == 0)
                rc = ehci_quiesce(&hc, blocked);
            else {
                mmio_wr32_le(regs + op + EHCI_USBINTR, 0);
                *blocked = mmio_rd32_le(regs + op + EHCI_CF) & 1;
                usbp_cfg_cmd(&hc.pci, 0, PCI_CMD_BUS_MASTER);
            }
        }
    }
    usbp_mmio_unmap(&hc.pci, 0);
    return rc;
}

void pc98_usb_early_quiesce(void)
{
    struct pci_dev dev;
    blocked_count = 0;
    if (!usbp_mmio_map || !usbp_mmio_unmap) return;
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
