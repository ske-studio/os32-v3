/* Serialized ep0 requests, FB usb_request.c:415; USB_DESIGN §4-4. */
#include "drivers/usb/usb_internal.h"
static struct usb_ctrl_req *cancelled;
static void defer_done(struct usb_ctrl_req *r)
{
    struct usb_ctrl_req **p = &cancelled;
    while (*p) p = &(*p)->next;
    r->next = 0; *p = r;
}
int usb_ctrl_owner_drained(struct usb_dev *d, struct usb_iface *owner)
{
    struct usb_ctrl_req *r;
    for (r = d->ctrl_q; r; r = r->next) if (r->owner == owner) return 0;
    for (r = cancelled; r; r = r->next) if (r->dev == d && r->owner == owner) return 0;
    return 1;
}
void usb_req_callbacks(struct usb_hc *hc)
{
    struct usb_ctrl_req **p = &cancelled;
    while (*p && hc->state == HC_RUNNING) {
        struct usb_ctrl_req *r = *p;
        struct usb_dev *d = r->dev;
        if (d->hc != hc) { p = &r->next; continue; }
        *p = r->next;
        if (r->done) r->done(r);
        if (hc->state == HC_DEAD) return;
        usbp_mem_free(r); usb_req_kick(d);
    }
}
static void ctrl_done(struct usb_xfer *x)
{
    struct usb_dev *d = x->dev;
    struct usb_ctrl_req *r = d->ctrl_q;
    if (!r) return;
    r->actual = x->actual; r->error = x->error;
    kmemset(&x->buf, 0, sizeof(x->buf));
    d->ctrl_q = r->next;
    if (r->done) r->done(r);
    if (d->hc->state == HC_DEAD) return;
    usbp_mem_free(r);
    usb_req_kick(d);
}
void usb_req_kick(struct usb_dev *d)
{
    struct usb_ctrl_req *r = d->ctrl_q;
    struct usb_xfer *x = &d->ctrl;
    if (!r || !usb_xfer_drained(x) || !x->open || d->state == DEV_DETACHED || d->hc->state != HC_RUNNING) return;
    x->setup = r->setup; x->pipe.mps = d->ep0_mps;
    x->len = le16_rd(r->setup.length); x->short_ok = 1;
    x->done = ctrl_done; x->deadline_ms = usbp_ms() + USB_REQUEST_TIMEOUT_MS;
    if (r->buf) {
        x->buf = *r->buf; x->buf.va = (u8 *)r->buf->va + r->off;
        x->buf.bus += r->off; x->buf.size -= r->off;
    }
    if (usb_xfer_start(x) < 0) {
        if (d->hc->state == HC_DEAD) return;
        /* Allocation/submission failures still obey asynchronous notification. */
        r->error = USB_X_IO; r->actual = 0;
        d->ctrl_q = r->next; kmemset(&x->buf, 0, sizeof(x->buf)); defer_done(r);
    }
}
int usb_ctrl_async(struct usb_dev *d, struct usb_iface *owner, const struct usb_setup *s,
                   struct usb_dma *b, u32 off, void (*done)(struct usb_ctrl_req *), void *ctx)
{
    struct usb_ctrl_req *r, **p;
    u32 len = le16_rd(s->length);
    if (d->state == DEV_DETACHED || d->hc->state != HC_RUNNING) return -USB_E_HC;
    if (len > USB_CFG_MAX || (len && !b) || (b && (off > b->size || len > b->size - off))) return -USB_E_INVAL;
    r = usbp_mem_alloc(sizeof(*r));
    if (!r) return -USB_E_NOMEM;
    kmemset(r, 0, sizeof(*r));
    r->dev = d; r->owner = owner; r->setup = *s; r->buf = b; r->off = off; r->done = done; r->ctx = ctx;
    for (p = &d->ctrl_q; *p; p = &(*p)->next) {}
    *p = r; usb_req_kick(d); return 0;
}
void usb_ctrl_cancel_owner(struct usb_dev *d, struct usb_iface *owner)
{
    struct usb_ctrl_req **p = &d->ctrl_q;
    if (d->hc->state != HC_RUNNING) return;
    while (*p) {
        struct usb_ctrl_req *r = *p;
        if (r->owner != owner) { p = &r->next; continue; }
        if (r == d->ctrl_q && !usb_xfer_drained(&d->ctrl)) {
            usb_xfer_stop(&d->ctrl); p = &r->next; continue;
        }
        *p = r->next; r->actual = 0; r->error = USB_X_CANCELLED;
        defer_done(r);
    }
    usb_req_kick(d);
}
int usb_xfer_sync(struct usb_xfer *x, u32 timeout_ms)
{
    int rc;
    u32 flags = usbp_irq_save();
    if (usb_busy) { usbp_irq_restore(flags); return -USB_E_CTX; }
    x->deadline_ms = usbp_ms() + timeout_ms;
    rc = usb_xfer_start(x);
    usbp_irq_restore(flags);
    if (rc >= 0) for (;;) {
        if (x->dev->hc->state == HC_DEAD) { rc = -USB_E_HC; break; }
        if (usb_xfer_drained(x)) {
            rc = (x->error == USB_X_OK || (x->error == USB_X_SHORT && x->short_ok)) ? 0 : -USB_E_INVAL;
            break;
        }
        usb_service();
    }
    x->deadline_ms = 0;
    return rc;
}
