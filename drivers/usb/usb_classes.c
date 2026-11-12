#include "drivers/usb/usb_internal.h"
const struct usb_class *const usb_classes[] = {
    &hid_kbd_class, &hid_mouse_class, &usb_hub_class, 0
};
