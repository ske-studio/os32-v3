/* EHCI 1.0 §§2.3,4.1; FB f22d6599 ehci.c:183-215 (BSD-2).
 * USB_DESIGN §4-9-1 D8: halt before reset, explicitly hand ports to OHCI. */
#include "drivers/usb/ohci_internal.h"
#define EHCI_CAPLENGTH 0u
#define EHCI_USBCMD 0u
#define EHCI_USBSTS 4u
#define EHCI_USBINTR 8u
#define EHCI_CONFIGFLAG 0x40u
#define EHCI_HCH (1u << 12)
#define EHCI_HCRESET 2u
#define EHCI_W1C 0x3fu
#define EHCI_WAIT_US 7813u

int ehci_quiesce(struct usb_hc *hc, u32 *why)
{
    u32 op = ohci_rd(hc, EHCI_CAPLENGTH) & 0xffu;
    u32 i;
    *why = 0;
    ohci_wr(hc, op + EHCI_USBINTR, 0);
    ohci_wr(hc, op + EHCI_USBCMD, 0);
    for (i = 0; i < 100; ++i) {
        usbp_udelay(EHCI_WAIT_US);
        if (ohci_rd(hc, op + EHCI_USBSTS) & EHCI_HCH) break;
    }
    if (i == 100) goto fail;
    ohci_wr(hc, op + EHCI_USBCMD, EHCI_HCRESET);
    for (i = 0; i < 100; ++i) {
        usbp_udelay(EHCI_WAIT_US);
        if (!(ohci_rd(hc, op + EHCI_USBCMD) & EHCI_HCRESET)) break;
    }
    if (i == 100) goto fail;
    ohci_wr(hc, op + EHCI_CONFIGFLAG, 0);
    if (ohci_rd(hc, op + EHCI_CONFIGFLAG)) goto fail;
    ohci_wr(hc, op + EHCI_USBSTS, EHCI_W1C);
    if (usbp_cfg_cmd(&hc->pci, 0, OHCI_BM) < 0) goto fail;
    hc->state = HC_OFF;
    return 0;
fail:
    *why = ohci_rd(hc, op + EHCI_CONFIGFLAG) ? 1u : 0u;
    ohci_wr(hc, op + EHCI_USBINTR, 0);
    usbp_cfg_cmd(&hc->pci, 0, OHCI_BM);
    hc->state = HC_DEAD;
    return -USB_E_HC;
}
