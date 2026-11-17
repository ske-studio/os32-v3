#ifndef USB_INTERNAL_H
#define USB_INTERNAL_H
#include "drivers/usb/usb_core.h"
#include "lib/kstring.h"
#define USB_REQUEST_TIMEOUT_MS 1000u
static inline int usb_due(u32 now, u32 deadline) { return (i32)(now - deadline) >= 0; }
extern u8 usb_busy;
void usb_xfer_service(struct usb_hc *);
void usb_xfer_callbacks(struct usb_hc *);
void usb_req_kick(struct usb_dev *);
void usb_req_callbacks(struct usb_hc *);
int usb_ctrl_owner_drained(struct usb_dev *, struct usb_iface *);
int usb_cfg_parse(struct usb_dev *);
extern const struct usb_class usb_hub_class;
#endif
