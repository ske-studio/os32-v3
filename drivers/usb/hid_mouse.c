#include "drivers/usb/hid_boot.h"
/* D3: boot protocol only, no report IDs or wheel; USB HID 1.11 Appendix B.2.
 * FreeBSD input/ums.c:218 (signed motion), :592-603 (protocol selection). */
static void mouse_done(struct usb_xfer *x)
{
    struct hid_boot *b = x->ctx;
    const u8 *r = x->buf.va;
    if (b->stopped) return;
    if ((x->error == USB_X_OK || x->error == USB_X_SHORT) && x->actual >= 3)
        usbp_input_mouse(b->iface->src, (i8)r[1], (i8)r[2], r[0] & 7);
    if (x->error == USB_X_OK || x->error == USB_X_SHORT) (void)usb_xfer_start(x);
}
static int mouse_match(struct usb_dev *d, struct usb_iface *f)
{
    return hid_endpoint(d, f, USB_HID_PROTOCOL_MOUSE) != 0;
}
static int mouse_attach(struct usb_dev *d, struct usb_iface *f)
{
    struct hid_boot *b = f->sc;
    if (!b) {
        int src;
        b = usbp_mem_alloc(sizeof(*b));
        if (!b) return -USB_E_NOMEM;
        *b = (struct hid_boot){0};
        src = usbp_input_src_alloc(USBP_INPUT_MOUSE);
        if (src < 0) { usbp_mem_free(b); return src; }
        f->src = src;
        f->sc = b;
        b->dev = d;
        b->iface = f;
        b->kind = USBP_INPUT_MOUSE;
    }
    return hid_progress(b, 2, mouse_done);
}
const struct usb_class hid_mouse_class = {
    .match = mouse_match, .attach = mouse_attach, .detach = hid_detach,
    .gone = hid_gone, .tick = 0, .release = hid_release
};
