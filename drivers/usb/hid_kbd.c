#include "drivers/usb/hid_boot.h"
struct hid_kbd {
    struct hid_boot boot;
    u8 prev[128], repeat;
    u32 deadline;
};
static void kbd_done(struct usb_xfer *x)
{
    struct hid_kbd *s = x->ctx;
    const u8 *r = x->buf.va;
    u8 now[128] = {0};
    unsigned int i;
    if (s->boot.stopped) return;
    if (x->error == USB_X_OK && x->actual >= 8) {
        for (i = 2; i < 8; ++i) if (r[i] >= 1 && r[i] <= 3) goto restart;
        for (i = 0; i < 8; ++i)
            if (r[0] & (1u << i)) {
                u8 sc = hid_kbd_sc(0xe0 + i);
                if (sc < 128) ++now[sc];
            }
        for (i = 2; i < 8; ++i) {
            u8 sc = hid_kbd_sc(r[i]);
            if (sc < 128) ++now[sc];
        }
        for (i = 0; i < 128; ++i) {
            if (!!s->prev[i] != !!now[i]) { /* A5: compare presence, not counts. */
                usbp_input_key(s->boot.iface->src, i, now[i] != 0);
                if (now[i] && i < 0x70) {
                    s->repeat = i;
                    s->deadline = usbp_ms() + 500;
                }
            }
            s->prev[i] = now[i];
        }
        if (s->repeat != 0xff && !now[s->repeat]) s->repeat = 0xff;
    }
restart:
    if (x->error == USB_X_OK || x->error == USB_X_SHORT) (void)usb_xfer_start(x);
}
static int kbd_match(struct usb_dev *d, struct usb_iface *f)
{
    return hid_endpoint(d, f, USB_HID_PROTOCOL_KBD) != 0;
}
static int kbd_attach(struct usb_dev *d, struct usb_iface *f)
{
    struct hid_kbd *s = f->sc;
    if (!s) {
        int src;
        s = usbp_mem_alloc(sizeof(*s));
        if (!s) return -USB_E_NOMEM;
        *s = (struct hid_kbd){0};
        src = usbp_input_src_alloc(USBP_INPUT_KBD);
        if (src < 0) { usbp_mem_free(s); return src; }
        f->src = src;
        f->sc = s;
        s->boot.dev = d;
        s->boot.iface = f;
        s->boot.kind = USBP_INPUT_KBD;
        s->repeat = 0xff;
    }
    return hid_progress(&s->boot, 1, kbd_done);
}
static void kbd_tick(struct usb_dev *d, struct usb_iface *f, u32 now)
{
    struct hid_kbd *s = f->sc;
    (void)d;
    if (s && !s->boot.stopped && s->repeat != 0xff && (i32)(now - s->deadline) >= 0) {
        usbp_input_key_repeat(f->src, s->repeat);
        s->deadline = now + 30;
    }
}
const struct usb_class hid_kbd_class = {
    .match = kbd_match, .attach = kbd_attach, .detach = hid_detach,
    .gone = hid_gone, .tick = kbd_tick, .release = hid_release
};
