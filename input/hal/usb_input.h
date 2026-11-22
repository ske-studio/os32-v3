#ifndef USB_INPUT_H
#define USB_INPUT_H
#include "types.h"

int usb_input_alloc(int kind);
void usb_input_usage(u8 handle, u8 usage, int down, int repeat);
int usb_input_mouse_available(void);
#endif
