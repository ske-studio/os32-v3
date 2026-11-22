/* PC-98 usb.mod body. Controllers and their IRQ targets live for the boot. */
#include "usb_resident.h"
#include "drivers/usb/ohci_priv.h"
#include "input/hal/in2_int.h"

struct module_handle pc98_usb_module_owner;

static int usb_irq(void *arg)
{
    struct usb_hc *hc = arg;
    u32 pending = hc->ops->intr_collect(hc);
    hc->isr_pending |= pending;
    return pending ? USBP_IRQ_HANDLED : USBP_IRQ_NONE;
}

int usb_module_init(struct module_handle owner)
{
    static struct pc98_usb_core core = {
        .ops = &ohci_ops, .isr = usb_irq, .register_hc = usb_hc_register,
        .dead = usb_hc_dead, .service = usb_service
    };
    pc98_usb_module_owner = owner;
    int active = pc98_usb_resident_start(owner, &core);
    return active < 0 ? active : 0;
}

const struct {
    u32 version;
    int (*initialize)(struct module_handle);
} usb_module_ops = {1, usb_module_init};

/* HID gives each kind its own short source number. in2 assigns a shared
 * device ID and generation, both required for delivery and removal. */
static struct usb_input_source {
    u8 id;
    u16 generation;
} keyboards[USB_KBD_MAX], mice[USB_MOUSE_MAX];

int usbp_input_src_alloc(int kind)
{
    struct usb_input_source *slots;
    unsigned count;
    u8 input_kind;
    if (kind == USBP_INPUT_KBD) {
        slots = keyboards;
        count = USB_KBD_MAX;
        input_kind = INPUT_KIND_KBD;
    } else if (kind == USBP_INPUT_MOUSE) {
        slots = mice;
        count = USB_MOUSE_MAX;
        input_kind = INPUT_KIND_MOUSE;
    } else return -1;
    for (unsigned i = 0; i < count; i++) {
        if (slots[i].id) continue;
        int id = input_src_register(input_kind, INPUT_ORIGIN_USB, &slots[i].generation);
        if (id <= 0) return -1;
        slots[i].id = id;
        return i + 1;
    }
    return -1;
}

void usbp_input_release(int kind, u8 src)
{
    struct usb_input_source *s;
    if (kind == USBP_INPUT_KBD && src && src <= USB_KBD_MAX)
        s = &keyboards[src - 1];
    else if (kind == USBP_INPUT_MOUSE && src && src <= USB_MOUSE_MAX)
        s = &mice[src - 1];
    else return;
    if (s->id) input_src_release(s->id, s->generation);
    s->id = 0;
}

void usbp_input_key(u8 src, u8 sc, int down)
{
    struct usb_input_source s = keyboards[src - 1];
    InputEvent e = {.type = INPUT_EVENT_KEY, .src = s.id, .generation = s.generation,
                    .sc = sc, .down = down, .time_ms = in2_time_ms()};
    input_capture(&e);
}

void usbp_input_key_repeat(u8 src, u8 sc)
{
    struct usb_input_source s = keyboards[src - 1];
    input_repeat(s.id, s.generation, sc);
}

void usbp_input_mouse(u8 src, i16 dx, i16 dy, u8 buttons)
{
    struct usb_input_source s = mice[src - 1];
    InputEvent e = {.src = s.id, .generation = s.generation, .time_ms = in2_time_ms()};
    if (dx || dy) {
        e.type = INPUT_EVENT_MOVE;
        e.x = dx;
        e.y = dy;
        input_capture(&e);
    }
    e.type = INPUT_EVENT_BUTTON;
    e.x = e.y = 0;
    e.buttons = buttons;
    input_capture(&e);
}
