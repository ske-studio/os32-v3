#ifndef USB_HID_BOOT_H
#define USB_HID_BOOT_H
#include "drivers/usb/usb_core.h"
/* USB HID 1.11 §7.2, Appendix B; FreeBSD ukbd.c:1344-1360,
 * ums.c:592-603. Cooperative control completion replaces synchronous waits. */
struct hid_boot {
    struct usb_xfer x;
    struct usb_dev *dev;
    struct usb_iface *iface;
    u8 stage, stopped, opened, kind;
};
u8 hid_kbd_sc(u8 usage);
static const u8 *hid_endpoint(struct usb_dev *d, struct usb_iface *f, u8 proto)
{
    u32 off = 0;
    int selected = 0;
    while (off + 2 <= d->cfg_len) {
        const u8 *p = d->cfg + off;
        if (p[0] < 2 || off + p[0] > d->cfg_len) return 0;
        if (p[1] == USB_DESC_INTERFACE) {
            selected = p[0] >= 9 && p[2] == f->num && p[3] == f->alt &&
                p[5] == USB_CLASS_HID && p[6] == USB_HID_SUBCLASS_BOOT && p[7] == proto;
        } else if (selected && p[1] == USB_DESC_ENDPOINT && p[0] >= 7 &&
                   (p[2] & USB_DIR_IN) && (p[3] & 3) == USB_EP_INTERRUPT)
            return p;
        off += p[0];
    }
    return 0;
}
static void hid_control_done(struct usb_ctrl_req *r)
{
    struct hid_boot *b = r->ctx;
    if (!b->stopped) ++b->stage; /* SET_PROTOCOL/SET_IDLE errors are ignored (FB). */
}
static void hid_stop(struct hid_boot *b)
{
    if (!b || b->stopped) return;
    b->stopped = 1;
    usbp_input_release(b->kind, b->iface->src);
    usb_ctrl_cancel_owner(b->dev, b->iface);
    if (b->opened) usb_xfer_unsetup_begin(&b->x);
}
static int hid_progress(struct hid_boot *b, u8 proto, void (*done)(struct usb_xfer *))
{
    int rc;
    if (b->stage == 0 || (proto == 1 && b->stage == 2)) {
        struct usb_setup s = { .type = USB_REQ_CLASS | USB_RECIP_INTERFACE,
            .request = b->stage == 0 ? USB_HID_SET_PROTOCOL : USB_HID_SET_IDLE };
        le16_wr(s.index, b->iface->num);
        ++b->stage;
        rc = usb_ctrl_async(b->dev, b->iface, &s, 0, 0, hid_control_done, b);
        if (rc < 0) { hid_stop(b); return rc; }
        return -USB_E_BUSY;
    }
    if (b->stage == 1 || (proto == 1 && b->stage == 3)) return -USB_E_BUSY;
    if (!b->opened) {
        rc = usb_xfer_setup(b->dev, b->iface, hid_endpoint(b->dev, b->iface, proto),
                            proto == 1 ? 8 : 3, &b->x);
        if (rc < 0) { hid_stop(b); return rc; }
        b->opened = 1;
        b->x.done = done;
        b->x.ctx = b;
        b->x.len = proto == 1 ? 8 : 3;
        b->x.short_ok = 1;
        rc = usb_xfer_start(&b->x);
        if (rc < 0) { hid_stop(b); return rc; }
    }
    return 0;
}
static void hid_gone(struct usb_dev *d, struct usb_iface *f)
{
    struct hid_boot *b = f->sc;
    (void)d;
    if (b && !b->stopped) {
        b->stopped = 1;
        usbp_input_release(b->kind, f->src);
    }
}
static void hid_detach(struct usb_dev *d, struct usb_iface *f)
{
    (void)d;
    hid_stop(f->sc);
}
static void hid_release(struct usb_dev *d, struct usb_iface *f)
{
    (void)d;
    usbp_mem_free(f->sc);
    f->sc = 0;
}
#endif
