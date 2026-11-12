/* USB_DESIGN §4-2-2, R7-01/02: death retains CPU memory as well as DMA. */
#include "drivers/usb/ohci_internal.h"
void usb_hc_dead(struct usb_hc *hc, u32 why)
{
    u32 i, j;
    if (hc->state == HC_DEAD) return;
    hc->state = HC_DEAD;
    hc->dead_why = why;
    hc->ops->kill(hc);
    for (i = 0; i < USB_HC_DMA_MAX; ++i)
        if (hc->dma[i].va) usbp_dma_quarantine(&hc->dma[i]);
    for (i = 0; i <= USB_DEV_MAX; ++i) {
        struct usb_dev *d = hc->devices[i];
        if (!d) continue;
        d->state = DEV_DETACHED;
        for (j = 0; j < USB_DEV_IFACE_MAX; ++j) {
            struct usb_iface *iface = &d->ifs[j];
            if (iface->cls && iface->cls->gone) iface->cls->gone(d, iface);
        }
    }
    usbp_log("usb: HC dead why=%lu", why);
}
