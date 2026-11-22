/* Transitional legacy projection. USB service serializes each complete report. */
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
static u8 changes[16];
#endif

void usbp_input_report(u8 src, const u8 old[32], const u8 now[32])
{
    u8 before[16] = {0}, after[16] = {0};

    for (unsigned usage = 1; usage < 256; ++usage) {
        unsigned bit = 1u << (usage & 7);
        if (!((old[usage >> 3] | now[usage >> 3]) & bit)) continue;
        u8 sc = hid_kbd_sc(usage);
        if (sc >= 128) continue;
        if (old[usage >> 3] & bit) before[sc >> 3] |= 1u << (sc & 7);
        if (now[usage >> 3] & bit) after[sc >> 3] |= 1u << (sc & 7);
    }
    /* Legacy delivery starts at modifier sc 0x70 and wraps through zero. */
    for (unsigned i = 0; i < 128; ++i) {
        unsigned sc = (0x70 + i) & 127;
        unsigned bit = 1u << (sc & 7);
        if ((before[sc >> 3] ^ after[sc >> 3]) & bit)
            usbp_input_key(src, sc, !!(after[sc >> 3] & bit));
    }
#ifdef KBOOT_USB_KEY_TRACE
    for (unsigned i = 0; i < sizeof(changes); ++i)
        changes[i] = before[i] ^ after[i];
#endif
}
void usbp_input_usage(u8 src, u8 usage, int down)
{
#ifdef KBOOT_USB_KEY_TRACE
    u8 sc = hid_kbd_sc(usage);
    unsigned bit = 1u << (sc & 7);
    if (sc < 128 && (changes[sc >> 3] & bit)) {
        changes[sc >> 3] &= ~bit;
        key_trace("USB KEY src=", src, sc, usage, down ? " down=1" : " down=0");
    }
#else
    (void)src; (void)usage; (void)down;
#endif
}
void usbp_input_usage_repeat(u8 src, u8 usage)
{
    u8 sc = hid_kbd_sc(usage);
    if (sc >= 128) return;
    usbp_input_key_repeat(src, sc);
#ifdef KBOOT_USB_KEY_TRACE
    key_trace("USB REPEAT src=", src, sc, usage, "");
#endif
}
