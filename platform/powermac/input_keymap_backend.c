#include "input_usb.h"
#include "input/hal/keymap.h"
#include "input/hal/usb_input.h"
#include "usb_platform.h"

static int keyboard_ready;

void pm_input_keymap_boot(void)
{
    keyboard_ready = km_backend_register(&hid_jp_backend) == 0;
    if (!keyboard_ready)
        usbp_log("USB HID keymap registration failed");
}

int usbp_input_src_alloc(int kind)
{
    if (kind == USBP_INPUT_KBD && !keyboard_ready) return -1;
    return usb_input_alloc(kind);
}
