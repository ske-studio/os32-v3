/* Cooperative replacement of FB controller/usb_controller.c:363-418. */
#include "drivers/usb/usb_internal.h"
#include "drivers/usb/ohci_regs.h"
#include "endian_dma.h"
struct hc_record {
    struct usb_hc *hc;
    u16 frame;
    u32 clock_deadline, poll_deadline;
    struct hc_record *next;
};
static struct hc_record *controllers;
u8 usb_busy;
int usb_hc_register(struct usb_hc *hc)
{
    struct hc_record *r;
    for (r = controllers; r; r = r->next) if (r->hc == hc) return 0;
    r = usbp_mem_alloc(sizeof(*r));
    if (!r) return -USB_E_NOMEM;
    kmemset(r, 0, sizeof(*r)); r->hc = hc;
    r->frame = hc->ops->frame(hc);
    r->clock_deadline = usbp_ms() + USB_DMA_DELAY_MS;
    r->poll_deadline = usbp_ms() + 100;
    r->next = controllers; controllers = r;
    hc->needs_explore = 1;
    return 0;
}
void usb_class_tick(u32 now)
{
    struct hc_record *r;
    u32 flags = usbp_irq_save();
    u8 saved = usb_busy;
    usb_busy = 1;
    for (r = controllers; r; r = r->next) {
        u32 a, i;
        if (r->hc->state != HC_RUNNING) continue;
        for (a = 1; a <= USB_DEV_MAX; a++) {
            struct usb_dev *d = r->hc->devices[a];
            if (!d || d->state == DEV_DETACHED) continue;
            for (i = 0; i < USB_DEV_IFACE_MAX; i++) {
                struct usb_iface *f = &d->ifs[i];
                if (f->attached && f->sc && f->cls && f->cls->tick) f->cls->tick(d, f, now);
                if (r->hc->state != HC_RUNNING) break;
            }
            if (r->hc->state != HC_RUNNING) break;
        }
    }
    usb_busy = saved;
    usbp_irq_restore(flags);
}
void usb_service(void)
{
    u32 flags = usbp_irq_save();
    struct hc_record *r;
    if (usb_busy) { usbp_irq_restore(flags); return; }
    usb_busy = 1;
    for (r = controllers; r; r = r->next) {
        struct usb_hc *hc = r->hc;
        u32 p, now;
        u16 frame;
        if (hc->state != HC_RUNNING) continue;
        p = hc->isr_pending | hc->ops->intr_collect(hc); hc->isr_pending = 0;
        if (p & OHCI_INTR_UE) { usb_hc_dead(hc, USB_WHY_UE); continue; }
        hc->ops->scan(hc, 32);
        if (hc->state != HC_RUNNING) continue;
        usb_xfer_service(hc);
        if (hc->state != HC_RUNNING) continue; /* delay fault */
        now = usbp_ms(); frame = hc->ops->frame(hc);
        if (frame != r->frame) { r->frame = frame; r->clock_deadline = now + USB_DMA_DELAY_MS; }
        else if (usb_due(now, r->clock_deadline)) { usb_hc_dead(hc, USB_WHY_CLOCK); continue; }
        if ((p & OHCI_INTR_RHSC) || usb_due(now, r->poll_deadline)) {
            hc->needs_explore = 1; r->poll_deadline = now + 100;
        }
    }
    for (r = controllers; r; r = r->next) {
        usb_xfer_callbacks(r->hc); usb_req_callbacks(r->hc);
    }
    for (r = controllers; r; r = r->next) {
        struct usb_hc *hc = r->hc;
        if (hc->state != HC_RUNNING) continue;
        usb_explore_step(hc);
        if (hc->state != HC_RUNNING) continue;
        if (hc->regs && hc->explore.step == EX_X9)
            mmio_wr32_le((volatile u8 *)hc->regs + OHCI_INTERRUPT_ENABLE, OHCI_INTR_RHSC);
    }
    usb_class_tick(usbp_ms());
    usb_busy = 0; usbp_irq_restore(flags);
}
