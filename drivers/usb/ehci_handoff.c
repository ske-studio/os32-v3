/* EHCI 1.0 §2.1.8 and Appendix B; FreeBSD f22d6599 ehci_pci.c:552-595. */
#include "drivers/usb/ohci_internal.h"

#define EHCI_BIOS_OWNED (1u << 16)
#define EHCI_OS_BYTE 3u
#define EHCI_LEGCTLSTS 4u
#define EHCI_USBINTR 8u
#define EHCI_CF 0x40u

static int take_ownership(struct usb_hc *hc)
{
    u32 ptr = (ohci_rd(hc, 8) >> 8) & 0xff;
    for (unsigned n = 0; ptr && n < 48; ++n) {
        if (ptr < 0x40 || (ptr & 3)) return -1;
        u32 cap = usbp_cfg_read32(&hc->pci, ptr);
        if ((cap & 0xff) == 1) {
            if (ptr > 0xf8) return -1;
            if (cap & EHCI_BIOS_OWNED) {
                if (usbp_cfg_write8(&hc->pci, ptr + EHCI_OS_BYTE, 1)) return -1;
                unsigned polls = 0;
                while (usbp_cfg_read32(&hc->pci, ptr) & EHCI_BIOS_OWNED) {
                    if (polls++ == 500) return -1;
                    usbp_udelay(10000);
                }
            }
            /* SMI enables occupy the low word. Leave the W1C status alone. */
            if (usbp_cfg_write8(&hc->pci, ptr + EHCI_LEGCTLSTS, 0) ||
                usbp_cfg_write8(&hc->pci, ptr + EHCI_LEGCTLSTS + 1, 0)) return -1;
        }
        ptr = (cap >> 8) & 0xff;
    }
    return ptr ? -1 : 0;
}

int ehci_early_quiesce(struct usb_hc *hc, u32 *blocked)
{
    u32 op = ohci_rd(hc, 0) & 0xff;
    *blocked = 1;
    if (op >= 0x10 && !(op & 3) && op + EHCI_CF + 4 <= hc->pci.bar_size[0]) {
        if (take_ownership(hc) == 0) return ehci_quiesce(hc, blocked);
        ohci_wr(hc, op + EHCI_USBINTR, 0);
        *blocked = ohci_rd(hc, op + EHCI_CF) & 1;
    }
    usbp_cfg_cmd(&hc->pci, 0, OHCI_BM);
    return -USB_E_HC;
}
