#include "usb_input.h"
#include "usb_platform.h"
#include "in2_int.h"
#include "keymap.h"
#include "kbd.h"

void ring3_abort_request(void);

/* USB callbacks serialize allocation, delivery and release. */
struct usb_source { u8 src; u16 generation; };
static struct usb_source slots[2][4];

static struct usb_source *lookup(int kind, u8 handle)
{
    if ((unsigned)kind >= 2 || handle < 1 || handle > 4) return 0;
    struct usb_source *s = &slots[kind][handle - 1];
    return s->src ? s : 0;
}

int usb_input_alloc(int kind)
{
    if ((unsigned)kind >= 2) return -1;
    for (unsigned i = 0; i < 4; ++i) {
        if (slots[kind][i].src) continue;
        u16 gen;
        int src = input_src_register(kind == USBP_INPUT_KBD ? INPUT_KIND_KBD : INPUT_KIND_MOUSE,
                                     INPUT_ORIGIN_USB, &gen);
        if (src < 0) return src;
        if (kind == USBP_INPUT_KBD && km_source_bind(src, gen, KM_MAP_HID)) {
            input_src_release(src, gen);
            return -1;
        }
        slots[kind][i] = (struct usb_source){src, gen};
        return i + 1;
    }
    return -1;
}

void usb_input_usage(u8 handle, u8 usage, int down, int repeat)
{
    struct usb_source *s = lookup(USBP_INPUT_KBD, handle);
    if (!s) return;
    KmKey key = { .src = s->src, .generation = s->generation, .keycode = usage,
                  .down = down, .repeat = repeat };
    int pause = usage == 0x48 && down && !repeat && !input_held_key(s->src, usage);
    if (!input_capture_key(&key) && pause && input_key_pressed(KEY_CTRL) &&
        !(kbd_gui_mode && kbd_stop_suppressed)) ring3_abort_request();
}

int usb_input_mouse_available(void)
{
    for (unsigned i = 0; i < 4; ++i)
        if (slots[USBP_INPUT_MOUSE][i].src) return 1;
    return 0;
}

int usb_input_mouse(u8 handle, i16 dx, i16 dy, u8 buttons)
{
    struct usb_source *s = lookup(USBP_INPUT_MOUSE, handle);
    if (!s) return 0;
    InputEvent e = { .src = s->src, .generation = s->generation, .time_ms = in2_time_ms(),
                     .type = INPUT_EVENT_MOVE, .x = dx, .y = dy };
    if (dx || dy) input_capture(&e);
    e.type = INPUT_EVENT_BUTTON;
    e.x = e.y = 0;
    e.buttons = buttons & 7;
    input_capture(&e);
    return 1;
}

int usb_input_release(int kind, u8 handle)
{
    struct usb_source *s = lookup(kind, handle);
    if (!s) return 0;
    input_src_release(s->src, s->generation);
    *s = (struct usb_source){0};
    return 1;
}
