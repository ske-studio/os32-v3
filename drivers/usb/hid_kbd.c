#include "drivers/usb/hid_boot.h"
struct hid_kbd {
    struct hid_boot boot;
    u8 prev[32];
    u16 repeat;
    u32 deadline;
};
static void kbd_done(struct usb_xfer *x)
{
    struct hid_kbd *s = x->ctx;
    const u8 *r = x->buf.va;
    u8 now[32] = {0};
    if (s->boot.stopped) return;
    if (x->error == USB_X_OK && x->actual >= 8) {
        now[0xe0 >> 3] = r[0];
        for (unsigned i = 2; i < 8; ++i) {
            u8 usage = r[i];
            if (usage >= 1 && usage <= 3) goto restart;
            if (usage && !(usage >= 0xe0 && usage <= 0xe7))
                now[usage >> 3] |= 1u << (usage & 7);
        }
        for (unsigned order = 0; order < 3; ++order) {
            for (unsigned i = 0; i < 256; ++i) {
                unsigned bit = 1u << (i & 7);
                if (!((s->prev[i >> 3] ^ now[i >> 3]) & bit) ||
                    hid_kbd_order(i) != order) continue;
                int down = !!(now[i >> 3] & bit);
                usbp_input_usage(s->boot.iface->src, i, down);
                if (down && hid_kbd_repeatable(i)) {
                    s->repeat = i;
                    s->deadline = usbp_ms() + 500;
                } else if (!down && s->repeat == i) {
                    s->repeat = 0xffff;
                }
            }
        }
        for (unsigned i = 0; i < sizeof(now); ++i) s->prev[i] = now[i];
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
    if (s && !s->boot.stopped && s->repeat != 0xffff &&
        hid_kbd_repeatable(s->repeat) && (i32)(now - s->deadline) >= 0) {
        usbp_input_usage_repeat(f->src, s->repeat);
        s->deadline = now + 30;
    }
}
const struct usb_class hid_kbd_class = {
    .match = kbd_match, .attach = kbd_attach, .detach = hid_detach,
    .gone = hid_gone, .tick = kbd_tick, .release = hid_release
};
