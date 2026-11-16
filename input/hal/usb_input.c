/* INPUT_KEYMAP_DESIGN §7 S2a, K5: USB IDs are not in2 source IDs. */
#include "usb_input.h"
#include "keymap.h"
#include "in2_int.h"
#include "io.h"
#include "os32_kapi_shared.h"

static struct {
    u8 src;
    u16 generation;
} usb_sources[256];

int input_usb_register(u8 usb_src)
{
    unsigned flags = irq_save();
    u16 generation;
    int src, rc = OS32_ERR_INVAL;
    if (!usb_src || usb_sources[usb_src].src) goto out;
    src = input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_USB, &generation);
    if (src <= 0) { rc = src; goto out; }
    rc = km_source_bind((u8)src, generation, KM_MAP_HID);
    if (rc) {
        input_src_release((u8)src, generation);
        goto out;
    }
    usb_sources[usb_src].src = (u8)src;
    usb_sources[usb_src].generation = generation;
out:
    irq_restore(flags);
    return rc;
}

/* Check the stored generation before handing off or releasing a reused slot. */
static int live(u8 usb_src)
{
    u8 kind, origin;
    return usb_src && usb_sources[usb_src].src &&
        input_src_info(usb_sources[usb_src].src,
                       usb_sources[usb_src].generation, &kind, &origin) &&
        kind == INPUT_KIND_KBD && origin == INPUT_ORIGIN_USB;
}

int input_usb_usage(u8 usb_src, u8 usage, u8 down, u8 repeat)
{
    unsigned flags = irq_save();
    int rc = OS32_ERR_INVAL;
    if (live(usb_src)) {
        KmKey key = { .src = usb_sources[usb_src].src,
            .generation = usb_sources[usb_src].generation,
            .keycode = usage, .down = down, .repeat = repeat };
        rc = input_capture_key(&key);
    }
    irq_restore(flags);
    return rc;
}

void input_usb_release(u8 usb_src)
{
    unsigned flags = irq_save();
    if (live(usb_src))
        input_src_release(usb_sources[usb_src].src, usb_sources[usb_src].generation);
    usb_sources[usb_src].src = 0;
    irq_restore(flags);
}
