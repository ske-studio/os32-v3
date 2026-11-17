/* USB 2.0 §9.6, USB_DESIGN §4-4-2. */
#include "drivers/usb/usb_internal.h"
int usb_cfg_parse(struct usb_dev *d)
{
    u32 off = 0, n = 0;
    i32 current = -1;
    u8 kind = 0;
    if (!d->cfg || d->cfg_len < USB_CONFIG_DESC_LEN || d->cfg[1] != USB_DESC_CONFIG ||
        le16_rd(d->cfg + 2) != d->cfg_len) return -USB_E_INVAL;
    kmemset(d->ifs, 0, sizeof(d->ifs));
    for (n = 0; n < USB_DEV_IFACE_MAX; n++) d->ifs[n].num = 255;
    n = 0;
    while (off < d->cfg_len) {
        const u8 *p = d->cfg + off;
        u8 min = 2;
        if (d->cfg_len - off < 2 || p[0] < 2 || p[0] > d->cfg_len - off) return -USB_E_INVAL;
        switch (p[1]) {
        case USB_DESC_DEVICE: min = USB_DEVICE_DESC_LEN; break;
        case USB_DESC_CONFIG: min = USB_CONFIG_DESC_LEN; break;
        case USB_DESC_INTERFACE: min = USB_INTERFACE_DESC_LEN; break;
        case USB_DESC_ENDPOINT: min = USB_ENDPOINT_DESC_LEN; break;
        case USB_DESC_HID: min = USB_HID_DESC_MIN_LEN; break;
        case USB_DESC_HUB: min = USB_HUB_DESC_MIN_LEN; break;
        }
        if (p[0] < min) return -USB_E_INVAL;
        if (p[1] == USB_DESC_INTERFACE) {
            current = -1; kind = p[5];
        }
        if (p[1] == USB_DESC_INTERFACE && !p[3]) {
            if (n < USB_DEV_IFACE_MAX && p[4] <= USB_IFACE_EP_MAX) {
                d->ifs[n].num = p[2]; d->ifs[n].alt = 0; current = n; n++;
            } else usbp_log("usb: interface limit");
        }
        if (p[1] == USB_DESC_ENDPOINT && current >= 0 &&
            (kind == USB_CLASS_HID || kind == USB_CLASS_HUB || kind == USB_CLASS_MASS_STORAGE)) {
            u16 mps = le16_rd(p + 4);
            u8 type = p[3] & USB_EP_TYPE_MASK;
            int valid = (p[2] & USB_EP_NUM_MASK) && mps;
            if (kind == USB_CLASS_HID || kind == USB_CLASS_HUB) {
                valid = valid && (p[2] & USB_DIR_IN) && type == USB_EP_INTERRUPT && p[6] &&
                    mps <= (d->speed == USB_SPEED_LOW ? 8 : d->speed == USB_SPEED_FULL ? 64 : 1024) &&
                    (d->speed != USB_SPEED_HIGH || p[6] <= 16);
            } else {
                valid = valid && type == USB_EP_BULK &&
                    ((d->speed == USB_SPEED_FULL && (mps == 8 || mps == 16 || mps == 32 || mps == 64)) ||
                     (d->speed == USB_SPEED_HIGH && mps == 512));
            }
            if (!valid) { d->ifs[current].num = 255; current = -1; }
        }
        off += p[0];
    }
    return 0;
}
