/* OpenHCI 1.0a §7.4; FB ohci.c:2302-2320. Reset recovery belongs to
 * the cooperative enumerator: this interface never waits. */
#include "drivers/usb/ohci_internal.h"
int ohci_rh_port(struct usb_hc *hc, u8 port, u8 op, u32 *st)
{
    u32 v;
    if (hc->state != HC_RUNNING) return -USB_E_HC;
    if (!port || port > hc->nports || !st) return -USB_E_INVAL;
    switch (op) {
    case USB_RH_STATUS: v = 0; break;
    case USB_RH_SET_ENABLE: v = OHCI_PORT_PES; break;
    case USB_RH_CLEAR_ENABLE: v = OHCI_PORT_CCS; break;
    case USB_RH_SET_POWER: v = OHCI_PORT_PPS; break;
    case USB_RH_RESET: v = OHCI_PORT_PRS; break;
    case USB_RH_CLEAR_CONNECT: v = OHCI_PORT_CSC; break;
    case USB_RH_CLEAR_ENABLE_CHANGE: v = OHCI_PORT_PESC; break;
    case USB_RH_CLEAR_RESET: v = OHCI_PORT_PRSC; break;
    case USB_RH_CLEAR_OVER_CURRENT: v = OHCI_PORT_OCIC; break;
    default: return -USB_E_INVAL;
    }
    if (v) ohci_wr(hc, OHCI_RH_PORT_STATUS(port), v);
    *st = ohci_rd(hc, OHCI_RH_PORT_STATUS(port));
    return 0;
}
