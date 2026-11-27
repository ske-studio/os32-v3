#include "sys/input/input_policy.h"
#include "input_usb.h"
#include "usb_boot.h"
#include "sys/hal/input/keymap.h"
#include "sys/hal/input/usb_input.h"
#include "usb_platform.h"

static int keyboard_ready;

void pm_input_keymap_boot(void)
{
    keyboard_ready = km_backend_register(&hid_jp_backend) == 0;
    if (!keyboard_ready) {
        usbp_log("USB HID keymap registration failed");
        return;
    }
    int rc = ikm_policy_init(IKM_PLATFORM_MAC, 0, &hid_jp_backend, input_policy_pressed);
    if (rc) usbp_log("USB HID input policy initialization failed (%d)", rc);
}

void pm_usb_boot(u32 tb_hz)
{
    pm_usb_platform_init(tb_hz);
    pm_input_keymap_boot();
    pm_usb_scan();
}

int usbp_input_src_alloc(int kind)
{
    if (kind == USBP_INPUT_KBD && !keyboard_ready) return -1;
    return usb_input_alloc(kind);
}
