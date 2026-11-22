/* PC-98 usb.mod body. Controllers and their IRQ targets live for the boot. */
#include "usb_resident.h"
#include "drivers/usb/ohci_priv.h"
#include "input/hal/usb_input.h"

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

int usbp_input_src_alloc(int kind)
{
    return usb_input_alloc(kind);
}

void usbp_input_usage(u8 src, u8 usage, int down)
{
    usb_input_usage(src, usage, down, 0);
}

void usbp_input_usage_repeat(u8 src, u8 usage)
{
    usb_input_usage(src, usage, 1, 1);
}

void usbp_input_mouse(u8 handle, i16 dx, i16 dy, u8 buttons)
{
    usb_input_mouse(handle, dx, dy, buttons);
}

void usbp_input_release(int kind, u8 handle)
{
    usb_input_release(kind, handle);
}
