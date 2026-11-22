/* Transitional legacy projection. USB service serializes each complete report. */
#include "usb_platform.h"
#include "input/hal/keymap.h"

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

static u8 changes[16];
void usbp_input_report(u8 src, const u8 old[32], const u8 now[32])
{
    u8 before[16] = {0}, after[16] = {0};
    (void)src;
    for (unsigned usage = 1; usage < 256; ++usage) {
        u8 sc = hid_jp_backend.compat_sc[usage];
        if (sc >= 128) continue;
        unsigned bit = 1u << (usage & 7);
        if (old[usage >> 3] & bit) before[sc >> 3] |= 1u << (sc & 7);
        if (now[usage >> 3] & bit) after[sc >> 3] |= 1u << (sc & 7);
    }
    for (unsigned i = 0; i < sizeof(changes); ++i)
        changes[i] = before[i] ^ after[i];
}
void usbp_input_usage(u8 src, u8 usage, int down)
{
    u8 sc = hid_jp_backend.compat_sc[usage];
    unsigned bit = 1u << (sc & 7);
    if (sc < 128 && (changes[sc >> 3] & bit)) {
        changes[sc >> 3] &= ~bit;
        usbp_input_key(src, sc, down);
#ifdef KBOOT_USB_KEY_TRACE
        key_trace("USB KEY src=", src, sc, usage, down ? " down=1" : " down=0");
#endif
    }
}
void usbp_input_usage_repeat(u8 src, u8 usage)
{
    u8 sc = hid_jp_backend.compat_sc[usage];
    if (sc >= 128) return;
    usbp_input_key_repeat(src, sc);
#ifdef KBOOT_USB_KEY_TRACE
    key_trace("USB REPEAT src=", src, sc, usage, "");
#endif
}
