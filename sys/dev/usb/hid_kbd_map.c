#include "sys/dev/usb/usb_core.h"
#include "sys/hal/input/keymap.h"

/* S2h: the generated HID backend owns compatibility and repeat metadata.
 * E3/E7 keep no legacy projection pending the K2 audit. */
u8 hid_kbd_sc(u8 usage)
{
    return hid_jp_backend.compat_sc[usage];
}

u8 hid_kbd_norepeat(u8 usage)
{
    const u8 *bits = hid_jp_backend.norepeat;
    return (bits[usage >> 3] >> (usage & 7)) & 1;
}

u8 hid_kbd_order(u8 usage)
{
    const KmHidExtensions *ext = hid_jp_backend.extensions;
    return (ext->order[usage >> 2] >> ((usage & 3) * 2)) & 3;
}

int hid_kbd_repeatable(u8 usage)
{
    return km_hid_repeatable(&hid_jp_backend, usage);
}
