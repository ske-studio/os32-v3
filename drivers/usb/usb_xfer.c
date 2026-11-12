/* USB_DESIGN §4-6-2; FB usb_transfer.c:1376-1502,1724-1777,2991-3020.
 * The list contains every setup xfer, including idle ones: class teardown
 * must also see an xfer which has never been submitted. */
#include "drivers/usb/usb_internal.h"
struct xfer_record { struct usb_xfer *x; struct xfer_record *next; };
static struct xfer_record *xfers;

int usb_xfer_setup(struct usb_dev *d, struct usb_iface *owner, const u8 *ep,
                   u32 size, struct usb_xfer *x)
{
    struct xfer_record *r;
    int rc;
    if (!d || !x || d->state == DEV_DETACHED || d->hc->state != HC_RUNNING)
        return -USB_E_HC;
    if (ep && (ep[0] < USB_ENDPOINT_DESC_LEN || ep[1] != USB_DESC_ENDPOINT))
        return -USB_E_INVAL;
    r = usbp_mem_alloc(sizeof(*r));
    if (!r) return -USB_E_NOMEM;
    kmemset(x, 0, sizeof(*x));
    x->dev = d; x->owner = owner;
    if (ep) {
        x->pipe.ep = ep[2]; x->pipe.type = ep[3] & USB_EP_TYPE_MASK;
        x->pipe.mps = le16_rd(ep + 4); x->pipe.interval = ep[6];
    } else { x->pipe.type = USB_EP_CONTROL; x->pipe.mps = d->ep0_mps; }
    if (size) {
        rc = usb_dmamem_alloc(d->hc, size, size >= USB_CFG_MAX ? USB_CFG_MAX : 16, &x->buf);
        if (rc < 0) { usbp_mem_free(r); return rc; }
    }
    rc = d->hc->ops->xfer_alloc(d->hc, x);
    if (rc < 0) {
        if (x->buf.va) usb_dmamem_free(d->hc, &x->buf);
        usbp_mem_free(r); return rc;
    }
    x->open = 1; r->x = x; r->next = xfers; xfers = r;
    return 0;
}
int usb_xfer_start(struct usb_xfer *x)
{
    struct usb_hc *hc = x->dev->hc;
    int rc;
    if (hc->state != HC_RUNNING || x->dev->state == DEV_DETACHED) return -USB_E_HC;
    if (!x->open || x->state != XS_IDLE) return -USB_E_BUSY;
    if (x->len > x->buf.size) return -USB_E_INVAL;
    x->did_dma_delay = 0;
    x->curr_set ^= 1;
    x->actual = 0; x->error = USB_X_OK;
    x->state = XS_ACTIVE;
    x->next = hc->active; hc->active = x;
    if (x->buf.va) usbp_dma_sync(&x->buf, 0, x->len, USBP_TO_DEV);
    rc = hc->ops->xfer_enter(hc, x);
    if (rc < 0) { hc->active = x->next; x->next = 0; x->state = XS_IDLE; }
    return rc;
}
/* HCD calls notify only after publishing the unlink. Completion callbacks
 * are deferred until all HC fault/deadline checks in this service round. */
void usb_xfer_notify(struct usb_xfer *x)
{
    struct usb_hc *hc = x->dev->hc;
    if (hc->state != HC_RUNNING || x->state != XS_ACTIVE) return;
    if ((x->error == USB_X_CANCELLED || x->error == USB_X_TIMEOUT) && !x->did_dma_delay) {
        x->dma_delay_frame = hc->ops->frame(hc);
        x->dma_delay_deadline_ms = usbp_ms() + USB_DMA_DELAY_MS;
        x->did_dma_delay = 1; x->state = XS_DMA_DELAY;
    } else x->state = XS_CB_PENDING;
}
void usb_xfer_stop(struct usb_xfer *x)
{
    struct usb_hc *hc = x->dev->hc;
    if (hc->state != HC_RUNNING || x->state != XS_ACTIVE) return;
    x->error = USB_X_CANCELLED;
    hc->ops->xfer_close(hc, x);
    usb_xfer_notify(x);
}
int usb_xfer_drained(const struct usb_xfer *x) { return x->state == XS_IDLE; }
int usb_xfer_unsetup_done(const struct usb_xfer *x) { return x->unsetup == UNSETUP_DONE; }
void usb_xfer_unsetup_begin(struct usb_xfer *x)
{
    struct usb_hc *hc = x->dev->hc;
    if (hc->state != HC_RUNNING || x->unsetup != UNSETUP_NONE) return;
    x->open = 0; x->unsetup = UNSETUP_WAIT;
    x->unsetup_next = hc->unsetup_list; hc->unsetup_list = x;
    usb_xfer_stop(x);
}
void usb_xfer_unsetup_step(struct usb_xfer *x)
{
    struct usb_hc *hc = x->dev->hc;
    struct usb_xfer **p;
    struct xfer_record **r;
    if (hc->state != HC_RUNNING) return;
    if (x->unsetup == UNSETUP_WAIT && usb_xfer_drained(x)) {
        x->unsetup_frame = hc->ops->frame(hc);
        x->unsetup_deadline_ms = usbp_ms() + USB_DMA_DELAY_MS;
        x->unsetup = UNSETUP_DELAY;
    }
    if (x->unsetup != UNSETUP_DELAY) return;
    if ((u16)(hc->ops->frame(hc) - x->unsetup_frame) < USB_DMA_DELAY_FRAMES) {
        if (usb_due(usbp_ms(), x->unsetup_deadline_ms)) usb_hc_dead(hc, USB_WHY_UNSETUP);
        return;
    }
    hc->ops->xfer_free(hc, x);
    if (x->buf.va) usb_dmamem_free(hc, &x->buf);
    for (p = &hc->unsetup_list; *p; p = &(*p)->unsetup_next)
        if (*p == x) { *p = x->unsetup_next; break; }
    for (r = &xfers; *r; r = &(*r)->next)
        if ((*r)->x == x) { struct xfer_record *old = *r; *r = old->next; usbp_mem_free(old); break; }
    x->unsetup = UNSETUP_DONE;
}
int usb_class_unsetup_done(struct usb_dev *d, struct usb_iface *iface)
{
    struct xfer_record *r;
    for (r = xfers; r; r = r->next)
        if (r->x->dev == d && r->x->owner == iface) return 0;
    return usb_ctrl_owner_drained(d, iface);
}
void usb_xfer_service(struct usb_hc *hc)
{
    struct usb_xfer *x, *next;
    for (x = hc->active; x; x = x->next) {
        if (x->state == XS_ACTIVE && x->deadline_ms && usb_due(usbp_ms(), x->deadline_ms)) {
            x->error = USB_X_TIMEOUT; hc->ops->xfer_close(hc, x); usb_xfer_notify(x);
        }
        if (x->state == XS_DMA_DELAY) {
            if ((u16)(hc->ops->frame(hc) - x->dma_delay_frame) >= USB_DMA_DELAY_FRAMES)
                x->state = XS_CB_PENDING;
            else if (usb_due(usbp_ms(), x->dma_delay_deadline_ms)) {
                usb_hc_dead(hc, USB_WHY_DMA_DELAY); return;
            }
        }
    }
    for (x = hc->unsetup_list; x; x = next) {
        next = x->unsetup_next; usb_xfer_unsetup_step(x);
        if (hc->state == HC_DEAD) return;
    }
}
void usb_xfer_callbacks(struct usb_hc *hc)
{
    /* Remove before callback: done may immediately reinsert this xfer. */
    struct usb_xfer **p = &hc->active;
    while (*p && hc->state == HC_RUNNING) {
        struct usb_xfer *x = *p;
        if (x->state != XS_CB_PENDING) { p = &x->next; continue; }
        *p = x->next;
        if (hc->scan_next == x) hc->scan_next = x->next;
        x->next = 0; x->state = XS_IDLE;
        if (x->buf.va) usbp_dma_sync(&x->buf, 0, x->actual, USBP_TO_CPU);
        if (x->done) x->done(x);
    }
}
