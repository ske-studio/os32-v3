/* OpenHCI 1.0a §5.1 and FreeBSD ohci.c (ohci_init): request ownership
 * before stopping firmware DMA. The common start owns all subsequent DMA. */
#include "usb_resident.h"
#include "usb_probe.h"
#include "drivers/pci/pci.h"
#include "drivers/usb/ohci_regs.h"
#include "kernel/irq/irq.h"
#include "endian_dma.h"

static struct usb_irq_slot {
    struct module_handle owner;
    struct usb_pci_fn pci;
    int (*isr)(void *);
    void *arg;
} slots[PC98_USB_HCS];

static int dispatch(unsigned irq, void *arg)
{
    struct usb_irq_slot *s = arg;
    (void)irq;
    return s->isr(s->arg) == USBP_IRQ_HANDLED ? IRQ_HANDLED : IRQ_NONE;
}

static int same_function(const struct usb_pci_fn *a, const struct usb_pci_fn *b)
{
    return a->bus == b->bus && a->dev == b->dev && a->fn == b->fn;
}

int usb_resident_irq_attach(struct module_handle owner, const struct usb_pci_fn *f,
                            int (*isr)(void *), void *arg)
{
    struct module_record *r = module_record_get(owner);
    struct usb_irq_slot *slot = NULL;
    for (unsigned i = 0; i < PC98_USB_HCS; i++) {
        if (slots[i].isr && same_function(&slots[i].pci, f)) return -1;
        if (!slots[i].isr) slot = &slots[i];
    }
    if (!slot || !r || !isr || !module_ref_acquire(owner, r->owner, MODULE_REF_IRQ, (uptr)isr))
        return -1;
    *slot = (struct usb_irq_slot){owner, *f, isr, arg};
    if (!irq_register(f->irq, dispatch, slot, IRQ_F_SHARED)) return 0;
    slot->isr = NULL;
    module_ref_release(owner, MODULE_REF_IRQ);
    return -1;
}

int usb_resident_irq_detach(const struct usb_pci_fn *f)
{
    for (unsigned i = 0; i < PC98_USB_HCS; i++) {
        struct usb_irq_slot *s = &slots[i];
        if (!s->isr || !same_function(&s->pci, f)) continue;
        if (irq_unregister(s->pci.irq, dispatch, s)) return -1;
        s->isr = NULL;
        module_ref_release(s->owner, MODULE_REF_IRQ);
        return 0;
    }
    return -1;
}

static int takeover(struct usb_hc *hc)
{
    volatile u8 *regs = hc->regs;
    if (usbp_cfg_cmd(&hc->pci, PCI_CMD_MEM_ENABLE, 0)) return -1;
    if (mmio_rd32_le(regs + OHCI_CONTROL) & OHCI_CTL_IR) {
        mmio_wr32_le(regs + OHCI_COMMAND_STATUS, OHCI_CMD_OCR);
        for (unsigned i = 0; i < 100; i++) {
            usbp_udelay(1000);
            if (!(mmio_rd32_le(regs + OHCI_CONTROL) & OHCI_CTL_IR)) break;
        }
    }
    mmio_wr32_le(regs + OHCI_INTERRUPT_DISABLE, OHCI_INTR_ALL);
    mmio_wr32_le(regs + OHCI_CONTROL, OHCI_CTL_RESET);
    usbp_udelay(100000);
    return usbp_cfg_cmd(&hc->pci, 0, PCI_CMD_BUS_MASTER);
}

int pc98_usb_resident_start(struct module_handle owner, struct pc98_usb_core *core)
{
    struct pci_dev dev;
    unsigned used = 0;
    int active = 0;
    for (u32 i = 0; used < PC98_USB_HCS && !pci_get(i, &dev); i++) {
        struct usb_hc *hc = &core->hc[used];
        if (pc98_usb_ohci_function(&dev, &hc->pci)) continue;
        if (usbp_mmio_map(&hc->pci, 0, &hc->regs)) continue;
        if (takeover(hc)) {
            usbp_mmio_unmap(&hc->pci, 0);
            continue;
        }
        hc->ops = core->ops;
        hc->has_irq = !usb_resident_irq_attach(owner, &hc->pci, core->isr, hc);
        if (!hc->ops->start(hc)) {
            if (!core->register_hc(hc)) {
                active++;
                used++;
                continue;
            }
            core->dead(hc, USB_WHY_START);
        }
        if (hc->has_irq) usb_resident_irq_detach(&hc->pci);
        /* Keep failed HC storage too: quarantine or failed detach can retain it. */
        used++;
    }
    if (active) core->service();
    return active;
}
