#ifndef USB_WALK_H
#define USB_WALK_H
#include "drivers/usb/usb_internal.h"
/* Private to the 1C explorer, enumerator and hub. No class/HCD ABI. */
struct usb_walk {
    struct usb_hc *hc;
    struct usb_walk *next;
    struct usb_dma scratch;
    u32 status, reset_deadline, poll_deadline, resource_deadline;
    u16 cfg_length;
    u8 phase, pending, complete, error, iface, attach_failed, config_retry;
    u8 desc_phase, desc_retry, reset_phase, freeing;
    u32 actual;
};
struct usb_hub_sc {
    struct usb_dev *child[USB_HUB_PORT_MAX];
    u8 restart[USB_HUB_PORT_MAX];
    u8 nports, explored, stage, port, pending, error, closing, free_port;
    struct usb_dev *free_child;
    u32 deadline;
    struct usb_dma buf;
    struct usb_xfer intr;
};
struct usb_hub_sc *usb_hub_sc(struct usb_dev *);
struct usb_dev **usb_port_child(struct usb_hc *, struct usb_dev *, u8);
int usb_port_op(struct usb_walk *, u8 op);
int usb_enum_step(struct usb_walk *);
int usb_walk_request(struct usb_walk *, struct usb_dev *, u8 type, u8 req,
                     u16 value, u16 index, u16 len, struct usb_dma *buf);
void usb_enum_reset(struct usb_walk *);
int usb_hub_children_done(struct usb_dev *);
void usb_dev_return(struct usb_dev *);
#endif
