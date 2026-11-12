#include "endian_dma.h"
#include "usb_platform.h"
#include "drivers/usb/usb_core.h"
#include "drivers/usb/usb_desc.h"
#include "drivers/usb/ohci_regs.h"
#include "drivers/usb/ohci_priv.h"
/* Compile contracts for the fields shared by core, HCD and HID. */
#define SAME_TYPE(expr, type) __builtin_types_compatible_p(__typeof__(expr), type)
STATIC_ASSERT(SAME_TYPE(((struct usb_xfer *)0)->setup, struct usb_setup), xfer_setup);
STATIC_ASSERT(SAME_TYPE(((struct usb_dev *)0)->freeing, struct usb_free_ctx), free_context);
STATIC_ASSERT(SAME_TYPE(((struct usb_hc *)0)->hcpriv, void *), hc_private);
STATIC_ASSERT(SAME_TYPE(((struct usb_hc *)0)->root_child,
                       struct usb_dev *[USB_RH_PORTS_MAX]), root_children);
STATIC_ASSERT(SAME_TYPE(((struct usb_hc *)0)->root_restart,
                       u8[USB_RH_PORTS_MAX]), root_restarts);
STATIC_ASSERT(SAME_TYPE(((struct usb_hc *)0)->explored, u8), first_scan);
STATIC_ASSERT(SAME_TYPE(&usbp_input_release, void (*)(int, u8)), input_release);
STATIC_ASSERT(SAME_TYPE(&usbp_log, void (*)(const char *, ...)), platform_log);
STATIC_ASSERT(SAME_TYPE(&usb_hc_register, int (*)(struct usb_hc *)), hc_register);
STATIC_ASSERT(SAME_TYPE(((struct usb_class *)0)->tick,
                       void (*)(struct usb_dev *, struct usb_iface *, u32)), class_tick);
STATIC_ASSERT(SAME_TYPE(&usb_class_tick, void (*)(u32)), class_tick_entry);
STATIC_ASSERT(SAME_TYPE(usb_classes[0], const struct usb_class *const), class_table);
STATIC_ASSERT(SAME_TYPE(hid_kbd_class, const struct usb_class), kbd_class);
STATIC_ASSERT(SAME_TYPE(hid_mouse_class, const struct usb_class), mouse_class);
STATIC_ASSERT(USB_CFG_BUF_MAX == 2 && USB_CLASS_BUF_MAX == 8 &&
              USB_CLASS_BUF_SIZE == 64 && USB_CTRL_BUF_MAX == 8, buffer_limits);
STATIC_ASSERT(USBP_IRQ_NONE == 0 && USBP_IRQ_HANDLED == 1, isr_result);
STATIC_ASSERT(FD_IDLE == 0 && FD_D0 != FD_IDLE && EX_X0 == 0, state_names);
STATIC_ASSERT(OHCI_RHS_LPS == 1 && OHCI_RHS_LPSC == 0x10000u &&
              OHCI_CTL_CBSR_1_4 == 3 && OHCI_CTL_CBSR_MASK == 3, ohci_control_bits);
STATIC_ASSERT(OHCI_FMI_FI(0xffff2edfu) == 0x2edfu &&
              OHCI_FMI_FSMPS(0xa7782edfu) == 0x2778u &&
              OHCI_TD_CC(0x9abcdef0u) == 9, ohci_extract);
/* Runtime checks use known device bytes independently of the accessor pair:
 * inverse but equally broken get/put implementations must not pass. */
static int run(void)
{
    const u32 values[] = { 0, 1, 0x12345678u, 0x80000000u, 0xffffffffu };
    struct ohci_ed_hw ed;
    unsigned int reg = 0;
    volatile u8 *bytes = (volatile u8 *)&ed.headp;
    volatile u8 *mm = (volatile u8 *)&reg;
    unsigned int i, j;
    for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        u32 v = values[i];
        dma_le32_put(&ed.headp, v);
        mmio_wr32_le(&reg, v);
        for (j = 0; j < 4; ++j) {
            u8 want = (u8)(v >> (8 * j));
            if (bytes[j] != want || mm[j] != want) return 1;
        }
        if (dma_le32_get(&ed.headp) != v || mmio_rd32_le(&reg) != v) return 2;
    }
    bytes[0] = 0xef; bytes[1] = 0xcd; bytes[2] = 0xab; bytes[3] = 0x89;
    mm[0] = 0x10; mm[1] = 0x32; mm[2] = 0x54; mm[3] = 0x76;
    if (dma_le32_get(&ed.headp) != 0x89abcdefu) return 3;
    if (mmio_rd32_le(&reg) != 0x76543210u) return 4;
    return 0;
}

/* No libc: exit through the i386 Linux syscall, as tick_probe_host.c. */
void _start(void)
{
    int r = run();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(r));
    for (;;) {}
}
