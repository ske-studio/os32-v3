/* FreeBSD ohci.c:688-716,1614-1640; USB_DESIGN §4-6-2.
 * Delayed notifications/unsetup are advanced by the 1C core, never by waiting. */
#include "drivers/usb/ohci_internal.h"
void ohci_xfer_close(struct usb_hc *hc, struct usb_xfer *x)
{
    struct ohci_xp *p = x->hcpriv;
    struct ohci_ed *ed, *prev;
    if (!p || hc->state == HC_DEAD) return;
    ed = p->sets[x->curr_set].ed;
    prev = ed->sw_prev;
    if (!prev) return;
    dma_le32_put(&ed->hw->flags, dma_le32_get(&ed->hw->flags) | OHCI_ED_SKIP);
    ohci_sync(hc, ed->bus, sizeof(*ed->hw), USBP_TO_DEV);
    dma_le32_put(&prev->hw->nexted, dma_le32_get(&ed->hw->nexted));
    prev->sw_next = ed->sw_next;
    if (ed->sw_next) ed->sw_next->sw_prev = prev;
    ohci_sync(hc, prev->bus, sizeof(*prev->hw), USBP_TO_DEV);
    /* Preserve removed ED.NextED: a controller may still hold this ED. */
    ed->sw_prev = NULL;
}

void ohci_xfer_close_done(struct usb_hc *hc, struct usb_xfer *x)
{
    if (hc->state == HC_DEAD) return;
    ohci_xfer_close(hc, x);
    x->state = XS_CB_PENDING;
}
