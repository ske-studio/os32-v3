#include "usb_resident.h"
#include "usb_probe.h"
#include "drivers/pci/pci.h"
#include "kernel/irq/irq.h"

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

int pc98_usb_resident_start(struct module_handle owner, struct pc98_usb_core *core)
{
    struct pci_dev dev;
    unsigned used = 0;
    int active = 0, found = 0;
    for (u32 i = 0; used < PC98_USB_HCS && !pci_get(i, &dev); i++) {
        struct usb_hc *hc = &core->hc[used];
        if (!pc98_usb_is_ohci(&dev)) continue;
        found = 1;
        if (!pc98_usb_ohci_quiet(i) || pc98_usb_ohci_function(&dev, &hc->pci)) continue;
        if (usbp_mmio_map(&hc->pci, 0, &hc->regs)) continue;
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
    return found && !active ? -USB_E_HC : active;
}

/* module_start supplies the validated entry and its owning record. */
int pc98_usb_module_start(struct module_record *record, u32 entry, u32 *exports)
{
    int (*init)(struct module_handle) = (int (*)(struct module_handle))(uptr)entry;
    struct module_handle owner = {record - module_records, record->generation};
    (void)exports;
    return init(owner);
}
