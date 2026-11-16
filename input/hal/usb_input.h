#ifndef INPUT_HAL_USB_INPUT_H
#define INPUT_HAL_USB_INPUT_H
#include "types.h"

/* Caller owns the USB source namespace (nonzero u8). Not wired until S2m.
 * Serialize allocation, delivery and release; these functions also mask IRQs.
 * Allocation fails closed: no mapping survives a failed map2 bind. */
int input_usb_register(u8 usb_src);
int input_usb_usage(u8 usb_src, u8 usage, u8 down, u8 repeat);
void input_usb_release(u8 usb_src);
#endif
