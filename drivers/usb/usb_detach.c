/* FB usb_device.c:2289-2376; USB_DESIGN §4-7. */
#include "drivers/usb/usb_walk.h"
void usb_free_device_begin(struct usb_dev *d)
{
    if (!d || d->hc->state != HC_RUNNING || d->freeing.step != FD_IDLE) return;
    d->freeing.step = FD_D0; d->freeing.iface = 0; d->freeing.child_port = 0;
}
int usb_free_device_step(struct usb_dev *d)
{
    struct usb_free_ctx *c = &d->freeing;
    struct usb_iface *f;
    if (d->hc->state != HC_RUNNING) return 0;
    switch (c->step) {
    case FD_D0:
        d->state = DEV_DETACHED; c->step = FD_D1; return 0;
    case FD_D1:
        if (c->iface == USB_DEV_IFACE_MAX) { c->step = FD_D2; return 0; }
        f = &d->ifs[c->iface];
        if (f->cls) {
            if (!c->child_port) { f->cls->detach(d, f); c->child_port = 1; }
            if (!usb_class_unsetup_done(d, f)) return 0;
            if (f->cls == &usb_hub_class && !usb_hub_children_done(d)) return 0;
            if (f->cls->release) f->cls->release(d, f);
            f->cls = 0; f->sc = 0;
        }
        c->child_port = 0; c->iface++; return 0;
    case FD_D2:
        if (d->cfg) usbp_mem_free(d->cfg);
        d->cfg = 0; d->cfg_len = 0; kmemset(d->ifs, 0, sizeof(d->ifs)); c->step = FD_D3; return 0;
    case FD_D3:
        *usb_port_child(d->hc, d->parent, d->port) = 0;
        if (d->hc->devices[d->addr] == d) d->hc->devices[d->addr] = 0;
        c->step = FD_D4; usb_ctrl_cancel_owner(d, 0); usb_xfer_unsetup_begin(&d->ctrl); return 0;
    case FD_D4:
        if (!usb_xfer_unsetup_done(&d->ctrl)) return 0;
        c->step = FD_D5; return 0;
    case FD_D5:
        usb_dev_return(d); return 1;
    default: return 0;
    }
}
