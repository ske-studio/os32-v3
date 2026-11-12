/* FB usb_hub.c:185-198,1398,1771-1784; USB 2.0 §11. */
#include "drivers/usb/usb_walk.h"
struct usb_hub_sc *usb_hub_sc(struct usb_dev *d)
{
    if (!d) return 0;
    for (u32 i = 0; i < USB_DEV_IFACE_MAX; i++)
        if (d->ifs[i].cls == &usb_hub_class) return d->ifs[i].sc;
    return 0;
}
static const u8 *hub_iface(struct usb_dev *d, struct usb_iface *f, int endpoint)
{
    u32 off;
    int selected = 0;
    for (off = 0; off + 2 <= d->cfg_len; off += d->cfg[off]) {
        const u8 *p = d->cfg + off;
        if (p[0] < 2 || p[0] > d->cfg_len - off) break;
        if (p[1] == USB_DESC_INTERFACE) {
            selected = p[2] == f->num && p[3] == f->alt;
            if (selected && !endpoint) return p;
        }
        if (selected && endpoint && p[1] == USB_DESC_ENDPOINT && p[0] >= 7 &&
            (p[2] & USB_DIR_IN) && (p[2] & USB_EP_NUM_MASK) &&
            (p[3] & USB_EP_TYPE_MASK) == USB_EP_INTERRUPT &&
            le16_rd(p + 4) >= 1 && le16_rd(p + 4) <= (d->speed == USB_SPEED_LOW ? 8 : 64) &&
            p[6] && (d->speed != USB_SPEED_HIGH || p[6] <= 16)) return p;
    }
    return 0;
}
static int hub_match(struct usb_dev *d, struct usb_iface *f)
{
    const u8 *p = hub_iface(d, f, 0);
    return p && p[5] == USB_CLASS_HUB && d->depth < USB_HUB_DEPTH_MAX;
}
static void hub_req_done(struct usb_ctrl_req *r)
{
    struct usb_hub_sc *s = r->ctx;
    s->error = r->error == USB_X_SHORT ? USB_X_OK : r->error;
    if (r->setup.request == USB_REQ_GET_DESCRIPTOR && r->actual < USB_HUB_DESC_MIN_LEN) s->error = USB_X_IO;
    s->pending = 2;
}
static int hub_req(struct usb_dev *d, struct usb_iface *f, u8 req, u16 value, u16 index, u16 len)
{
    struct usb_hub_sc *s = f->sc;
    struct usb_setup setup;
    int rc;
    if (s->pending == 1) return 0;
    if (s->pending == 2) { s->pending = 0; return 1; }
    kmemset(&setup, 0, sizeof(setup)); setup.request = req;
    setup.type = USB_REQ_CLASS | (len ? USB_DIR_IN : USB_RECIP_OTHER);
    le16_wr(setup.value, value); le16_wr(setup.index, index); le16_wr(setup.length, len);
    s->pending = 1;
    rc = usb_ctrl_async(d, f, &setup, &s->buf, 0, hub_req_done, s);
    if (rc < 0) { s->pending = 0; s->error = USB_X_IO; return 1; }
    return 0;
}
static void hub_intr(struct usb_xfer *x)
{
    if (!x->open || x->dev->state == DEV_DETACHED) return;
    x->dev->hc->needs_explore = 1;
    x->len = 2; x->deadline_ms = 0;
    (void)usb_xfer_start(x);
}
static void hub_detach(struct usb_dev *d, struct usb_iface *f)
{
    struct usb_hub_sc *s = f->sc;
    if (!s || s->closing) return;
    s->closing = 1;
    if (s->intr.open) usb_xfer_unsetup_begin(&s->intr);
    usb_ctrl_cancel_owner(d, f);
}
static int hub_attach(struct usb_dev *d, struct usb_iface *f)
{
    struct usb_hub_sc *s = f->sc;
    const u8 *ep;
    int rc;
    if (!s) {
        s = usbp_mem_alloc(sizeof(*s));
        if (!s) return -USB_E_NOMEM;
        kmemset(s, 0, sizeof(*s)); f->sc = s;
        if (usb_dmamem_alloc(d->hc, USB_CTRL_BUF_SIZE, 16, &s->buf) < 0) goto fail;
    }
    if (s->stage == 0) {
        u8 *b = s->buf.va;
        if (!hub_req(d, f, USB_REQ_GET_DESCRIPTOR, USB_DESC_HUB << 8, 0, USB_CTRL_BUF_SIZE)) return -USB_E_BUSY;
        if (s->error || b[0] < 7 || b[1] != USB_DESC_HUB || !b[2]) goto fail;
        s->nports = b[2] > USB_HUB_PORT_MAX ? USB_HUB_PORT_MAX : b[2];
        s->deadline = (u32)b[5] * 2; s->port = 1; s->stage = 1;
    }
    if (s->stage == 1) {
        if (s->port <= s->nports) {
            if (!hub_req(d, f, USB_REQ_SET_FEATURE, USB_PORT_POWER, s->port, 0)) return -USB_E_BUSY;
            if (s->error) goto fail;
            s->port++; return -USB_E_BUSY;
        }
        s->deadline += usbp_ms(); s->stage = 2;
    }
    if (s->stage == 2) {
        if (!usb_due(usbp_ms(), s->deadline)) return -USB_E_BUSY;
        ep = hub_iface(d, f, 1);
        if (!ep) goto fail;
        rc = usb_xfer_setup(d, f, ep, USB_CLASS_BUF_SIZE, &s->intr);
        if (rc < 0) goto fail;
        s->intr.done = hub_intr; s->intr.len = 2; s->intr.short_ok = 1;
        if (usb_xfer_start(&s->intr) < 0) goto fail;
        s->stage = 3;
    }
    return 0;
fail:
    hub_detach(d, f); return -USB_E_INVAL;
}
int usb_hub_children_done(struct usb_dev *d)
{
    struct usb_hub_sc *s = usb_hub_sc(d);
    if (!s) return 1;
    /* FB first drains the interrupt pipe, then frees children in port order. */
    if (s->intr.dev && s->intr.unsetup && !usb_xfer_unsetup_done(&s->intr)) return 0;
    while (s->free_port < s->nports) {
        if (!s->free_child) {
            s->free_child = s->child[s->free_port];
            if (!s->free_child) { s->free_port++; continue; }
            usb_free_device_begin(s->free_child);
        }
        /* D3 clears child[port-1] before D4/D5 complete. Keep the child
         * independently until its ep0 and ordinary memory have returned. */
        if (!usb_free_device_step(s->free_child)) return 0;
        s->free_child = 0; s->free_port++;
    }
    return 1;
}
static void hub_release(struct usb_dev *d, struct usb_iface *f)
{
    struct usb_hub_sc *s = f->sc;
    if (!s) return;
    if (s->buf.va) usb_dmamem_free(d->hc, &s->buf);
    usbp_mem_free(s); f->sc = 0;
}
static void hub_gone(struct usb_dev *d, struct usb_iface *f) { (void)d; (void)f; }
const struct usb_class usb_hub_class = {
    hub_match, hub_attach, hub_detach, hub_gone, 0, hub_release
};
