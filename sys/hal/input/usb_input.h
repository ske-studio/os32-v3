#ifndef USB_INPUT_H
#define USB_INPUT_H
#include "types.h"

int usb_input_alloc(int kind);
void usb_input_usage(u8 handle, u8 usage, int down, int repeat);
/* Return nonzero when the handle belongs to a live source. */
int usb_input_mouse(u8 handle, i16 dx, i16 dy, u8 buttons);
int usb_input_release(int kind, u8 handle);
int usb_input_mouse_available(void);
#endif
