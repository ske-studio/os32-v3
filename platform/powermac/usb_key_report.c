#include "input/hal/in2_int.h"
#include "input/hal/usb_input.h"
#include "usb_platform.h"
#include "drivers/usb/usb_core.h"

#ifdef KBOOT_USB_KEY_TRACE
#include "port_service.h"
/* The acceptance driver synchronizes QMP input with ESCC, independently of LAN. */
static void key_trace(const char *prefix, u8 src, u8 sc, u8 usage, const char *suffix)
{
    static const char hex[] = "0123456789abcdef";
    while (*prefix) port_putchar_kapi(*prefix++);
    port_putchar_kapi('0' + src); /* source slots are 1..4 */
    const char *field = " sc=";
    while (*field) port_putchar_kapi(*field++);
    port_putchar_kapi(hex[sc >> 4]);
    port_putchar_kapi(hex[sc & 15]);
    while (*suffix) port_putchar_kapi(*suffix++);
    field = " usage=";
    while (*field) port_putchar_kapi(*field++);
    port_putchar_kapi(hex[usage >> 4]);
    port_putchar_kapi(hex[usage & 15]);
    port_putchar_kapi('\n');
}
#endif

void usbp_input_usage(u8 src, u8 usage, int down)
{
    usb_input_usage(src, usage, down, 0);
#ifdef KBOOT_USB_KEY_TRACE
    key_trace("USB KEY src=", src, hid_kbd_sc(usage), usage, down ? " down=1" : " down=0");
#endif
}

void usbp_input_usage_repeat(u8 src, u8 usage)
{
    usb_input_usage(src, usage, 1, 1);
#ifdef KBOOT_USB_KEY_TRACE
    key_trace("USB REPEAT src=", src, hid_kbd_sc(usage), usage, "");
#endif
}

void usbp_input_mouse(u8 handle, i16 dx, i16 dy, u8 buttons)
{
    if (usb_input_mouse(handle, dx, dy, buttons))
        usbp_log("USB MOUSE src=%u dx=%d dy=%d b=%x", handle, dx, dy, buttons);
}

void usbp_input_release(int kind, u8 handle)
{
    if (!usb_input_release(kind, handle)) return;
    if (kind == USBP_INPUT_MOUSE && !usb_input_mouse_available()) {
        i16 x, y;
        mouse_ext_read_legacy(&x, &y);
    }
    usbp_log("USB RELEASE kind=%d src=%u", kind, handle);
}
