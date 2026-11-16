#include "drivers/usb/hid_boot.h"
struct hid_kbd {
    struct hid_boot boot;
    u8 prev[256];
    u16 repeat;
    u32 deadline;
};
static void kbd_done(struct usb_xfer *x)
{
    struct hid_kbd *s = x->ctx;
    const u8 *r = x->buf.va;
    u8 now[256] = {0};
    unsigned int i, order;
    if (s->boot.stopped) return;
    if (x->error == USB_X_OK && x->actual >= 8) {
        for (i = 2; i < 8; ++i) if (r[i] >= 1 && r[i] <= 3) goto restart;
        for (i = 0; i < 8; ++i)
            if (r[0] & (1u << i)) now[0xe0 + i] = 1;
        for (i = 2; i < 8; ++i)
            if (r[i]) ++now[r[i]];
        /* INPUT_KEYMAP_DESIGN §7 S2u・注 :214-217:
         * modifiers, locks, then other usages; ascending within each group. */
        for (order = 0; order < 3; ++order) {
            for (i = 0; i < 256; ++i) {
                if (hid_kbd_order(i) != order) continue;
                if (!!s->prev[i] != !!now[i]) {
                    usbp_input_usage(s->boot.iface->src, i, now[i] != 0);
                    if (now[i] && !hid_kbd_norepeat(i)) {
                        s->repeat = i;
                        s->deadline = usbp_ms() + 500;
                    }
                }
                s->prev[i] = now[i];
            }
        }
        if (s->repeat != 0xffff && !now[s->repeat]) s->repeat = 0xffff;
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
        s->repeat = 0xffff;
    }
    return hid_progress(&s->boot, 1, kbd_done);
}
static void kbd_tick(struct usb_dev *d, struct usb_iface *f, u32 now)
{
    struct hid_kbd *s = f->sc;
    (void)d;
    if (s && !s->boot.stopped && s->repeat != 0xffff && !hid_kbd_norepeat(s->repeat) && (i32)(now - s->deadline) >= 0) {
        usbp_input_usage_repeat(f->src, s->repeat);
        s->deadline = now + 30;
    }
}
const struct usb_class hid_kbd_class = {
    .match = kbd_match, .attach = kbd_attach, .detach = hid_detach,
    .gone = hid_gone, .tick = kbd_tick, .release = hid_release
};
